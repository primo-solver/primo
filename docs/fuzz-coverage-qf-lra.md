# QF_LRA fuzz coverage

Two fuzzers guard QF_LRA, and they fail in opposite directions. FuzzSMT
generates formulas from a grammar, so it covers whatever its *parameters* say
and nothing else. YinYang's `opfuzz` and `typefuzz` mutate a seed directory, so
they cover whatever is *in that directory* and nothing else. Both defaults were
narrower than anyone had checked, and this page records what was found on
2026-08-01, what was changed, and what was measured rather than assumed.

(sec-seed-corpus)=
## The mutation seed corpus

`benchmarks/seeds/qf_lra_real` held 14 files drawn from three of the
non-incremental division's 17 families -- `check`, `DTP-Scheduling`,
`keymaera` -- plus `gasburner`, which is not in the division at all. Fourteen
families had never been mutated. The omissions included the two that matter
most to recent work:

- **`latendresse`** -- pure LP, zero decisions, very large pivot counts. This
  is the shape items [H8](qf-lra-heuristics.md#h8), [H9](qf-lra-heuristics.md#h9)
  and [H10](qf-lra-heuristics.md#h10) each rewrote.
- **`LassoRanker`** -- 419 files, the largest single source of Primo's lead over
  the reference solvers.

A benchmark run does exercise every family, but it only checks the final
answer. Mutation fuzzing perturbs the formula, which is what turns a family's
*shape* into coverage.

Seeds are committed, so the fix is not to add a megabyte of benchmarks.
`scripts/build_fuzz_seed_corpus.py` builds the corpus from a benchmark root
instead:

```
python3 scripts/build_fuzz_seed_corpus.py --root tmp/QF_LRA \
    --out benchmarks/seeds/qf_lra_division --per-family 2 --max-bytes 200000
```

It takes the smallest files per family, so mutation stays cheap while every
family is represented, and selection is deterministic (smallest first, ties by
path) so a given root always yields the same corpus. On the full division that
is **17 families, 34 seeds, 1.2 MB**.

One deliberate exception: a family whose smallest file exceeds `--max-bytes`
still contributes that file. `latendresse`'s smallest is 122 KB, and skipping
it would recreate the exact blind spot the script exists to remove.

(sec-invalid-seeds)=
### The size limit that undid the fix

The first campaign over the family-complete corpus (job 118543) reported, in
every one of its five tasks:

```
34 seeds processed, 22 valid, 12 invalid
0 bug triggers found
```

That "12 invalid" reads like a well-sortedness result. It is not. `typefuzz`'s
`-L` flag is a **seed file-size limit**, default 20000 bytes in
`scripts/yinyang_fuzz.sh`, and it silently drops every seed above it. The 12
dropped seeds were exactly the 12 files over 20 KB:

| family | seed size |
|---|---|
| `tropical-matrix` | 160 KB, 180 KB |
| `latendresse` | 125 KB |
| `TM` | 77 KB, 81 KB |
| `2017-Heizmann` | 73 KB, 78 KB |
| `miplib` | 70 KB |
| `LassoRanker` | 45 KB |

So the corpus that had just been widened from 3 families to 17 was truncated
back to 11 -- **discarding `latendresse` and `LassoRanker` again**, the two
families the whole exercise existed to cover. The campaign was clean, and it
was clean over almost the old blind spot.

Two changes, since one alone would leave the trap armed:

- `scripts/slurm_qfuf_fuzz_gate.sbatch` passes `--file-size-limit 200000`,
  matching the `--max-bytes` that `build_fuzz_seed_corpus.py` selects under, so
  the corpus builder decides the corpus and the fuzzer's limit no longer does.
- `scripts/yinyang_fuzz.sh` counts oversized seeds up front and names them on
  stderr, so a truncated corpus appears in the campaign log as a warning rather
  than as an arithmetic discrepancy nobody reads.

There is a second, independent effect worth knowing when reading mutation
results on these families: at a 20-second reference timeout, z3 5.0.0 does not
answer the unmutated seed from `LassoRanker`, `latendresse`, `miplib`, or
`tropical-matrix` at all -- 8 of the 34 seeds. Those iterations run but cannot
yield a verdict. That is what [](#sec-yices) addresses.

(sec-yices)=
## Yices as a second reference

A reference solver that cannot answer a seed is not a reference for it. Since
the seeds without an oracle were concentrated in the families the corpus had
just been widened to include, it was worth asking whether a different reference
does better. Measured 2026-08-01 on the corpus seeds:

| seed | primo | z3 @60s | yices @60s |
|---|---|---|---|
| `latendresse-compact2b` | sat, 3.2s | **timeout** | **sat, 15.9s** |
| `latendresse-compact2` | unsat, 2.7s | **timeout** | **unsat, 15.1s** |
| `miplib_pp08a-9000` | sat, 54.2s | **timeout** | **sat, 4.7s** |
| `miplib_pp08a-7350` | unknown | timeout | timeout |
| `tropical-matrix` x2 | unknown | timeout | timeout |
| `LassoRanker` x2 | unsat, ~3s | timeout | unsat, 109s |
| `TM` x2, `Heizmann` x2 | fast | fast | fast |

At the 20s mutation timeout **yices oracles 29 of 34 seeds against z3's 26**,
and its failures are a strict subset of z3's, so the union is also 29. The
three it adds are both `latendresse` seeds -- the family the widening existed
to cover -- and one `miplib` seed.

Yices does not subsume z3: both time out on `LassoRanker` at 20s and on
`tropical-matrix` outright. So it is added as a second reference, not a
replacement, which also buys reference cross-checking: `classify()` in
`scripts/fuzz_external.py` reports `REF_DISAGREEMENT` when two references
disagree with each other.

It is also the cheaper reference. On 150 wide-coefficient generator cases it
agreed with z3 on all 150 and ran 6x faster (0.3s against 1.9s).

Two asides the measurements forced out, both worth remembering rather than
re-deriving. `miplib_pp08a-9000`: yices 4.7s against primo's 54.2s -- `miplib`
was already the one division family z3 leads on, and yices looks stronger there
still. Conversely `LassoRanker`: primo 3s against yices 109s, about 36x the
other way.

The five seeds nobody oracles are covered by the differential benchmark runs
instead, where the division's `:status` fields are an oracle needing no
reference solver at all.

### What adding a reference broke

Two defects in `scripts/fuzz_external.py` were latent under one reference and
live under two:

- `classify()` returned `UNKNOWN_INVOLVED` whenever *any* reference answered
  unknown. With two references, one timing out while the other answers
  concretely is the ordinary case on hard inputs, so a genuine disagreement
  with the concrete reference would have been filed as inconclusive. Now only
  our own unknown, or no concrete reference at all, is inconclusive.
- A reference missing from `PATH` was dropped silently, so a run requested with
  two references and given one read as a complete result -- the same shape as
  the "fuzzsmt is not installed; skipping" no-op gate. It now reports the
  missing reference and exits non-zero if none is left, and the gate's
  required-tool check covers every configured reference rather than just z3.

### The clean-room line

Yices is installed by `contrib/install_yices.py` (`make contrib-yices`) into
`contrib/local`, which is gitignored and excluded from the cluster rsync. It is
a reference solver only: run as a separate process, never linked, source never
read.

That boundary is sharper here than for z3 or cvc5. Yices is the solver Dutertre
and de Moura wrote alongside "Integrating Simplex with DPLL(T)", the paper
Primo's QF_LRA theory follows. Reading the paper is the whole point; reading
the implementation would be a clean-room violation. Yices 2 is GPLv3 -- running
a binary imposes no obligation, linking would, so the installer deliberately
installs neither `lib/` nor `include/`.

(sec-fuzzsmt-profiles)=
## The FuzzSMT parameter ladder

Every QF_LRA gate recorded in `docs/run-log.json` before 2026-08-01 ran:

```
-ms 1 -Ms 3 -mv 1 -Mv 3 -mf 1 -mp 1 -ma 1 -Ma 2
```

For QF_LRA that string is FuzzSMT's **stock default**. `-ms`/`-Ms` are sort
counts and `-mf`/`-mp`/`-ma`/`-Ma` are uninterpreted-function, predicate and
arity knobs; FuzzSMT applies all of them only to the `QF_UF*` logics and
silently ignores them here, so nothing ever complained. What actually applied
was `-Mv 3`, and the untouched defaults `-Mc 3` and `-Mbw 4`.

`-Mbw` bounds the bit-width of the integer literals FuzzSMT emits, which become
coefficients and, via `(/ c1 c2)`, the rational constants. At the default
`-Mbw 4` **every coefficient in this project's entire generator-fuzz history
lay in [1,15]**. The GMP bignum paths in `DeltaRational` and in Farkas
coefficient combination were never reached by the generator.

`scripts/slurm_qfuf_fuzz_gate.sbatch` now assigns a ladder of profiles to
shards round-robin when `PRIMO_FUZZ_FUZZSMT_LOGIC=QF_LRA`:

| # | profile | what it buys |
|---|---|---|
| 0 | `-mv 1 -Mv 3 -mc 1 -Mc 3 -Mbw 4` | the historical setting, kept for continuity |
| 1 | `… -Mbw 16` | max literal 15 → 54430, at unchanged cost |
| 2 | `-mv 2 -Mv 5 -mc 1 -Mc 4 -Mbw 16` | ~9x the rows; 18s/100 cases, 1% timeouts |
| 3 | profile 2 `+ -bool-and` | 100% unsat: conflicts and Farkas certificates |
| 4 | profile 2 `+ -bool-or` | 100% sat: model construction |
| 5 | `-mv 1 -Mv 3 -mc 1 -Mc 3 -Mbw 32` | very wide coefficients at the small size |

Profile 3 earns its shard. The random Boolean layer yields only ~11% unsat, and
unsat is the discriminating outcome, since it is the one that exercises
conflict detection and certificate construction rather than model search.
Profile 4 is its complement and costs about the same.

Setting `PRIMO_FUZZ_FUZZSMT_ARGS` still forces a single profile, so every
historical `docs/run-log.json` command line stays reproducible. The QF_UF
default is unchanged.

(sec-rejected-knobs)=
### Knobs that do not pay, with the measurements

Cranking FuzzSMT indiscriminately does not work -- output size grows
superlinearly and throughput collapses. Measured at 10-150 cases per setting on
2026-08-01:

| setting | effect |
|---|---|
| `-Mbw 16` | **free**: no size or time cost, 4000x wider coefficients |
| `-Mc 10` | 3x file size |
| `-Mv 15` | 9x file size |
| `-ref 2` | 7x file size |
| `-ref 3` | 20x file size; generation itself starts hitting the wall |
| `-Mv 15 -ref 2` | ~2 MB **per case** -- the knobs compose |

`-bool-cnf` is unusable for a different reason: FuzzSMT 0.3 emits a malformed
`let` block for it, and z3 rejects the output too (`invalid let declaration`).
That is a generator bug, not something to work around here. All 150 cases at
`-bool-cnf 3` were rejected by Primo's parser, correctly.

The one caution on reading these numbers: FuzzSMT's output is seed-driven, and
changing a parameter changes the random stream, so per-setting file sizes
wander. A first measurement suggested `-Mbw 16` was *cheaper* than `-Mbw 4`
(4.2s versus 22.7s per 150 cases) and doubled the unsat rate; a second seed
showed 3.4s versus 3.7s and no meaningful unsat difference. The defensible
claim is that `-Mbw 16` costs nothing, not that it wins.

## Status

Job 118548 ran the full ladder: 6 shards x 500 cases, all six shard CSVs
present. **2959 PASS, 41 TIMEOUT, zero wrong answers and zero crashes.**
Timeouts by profile were 2, 7, 19, 4, 5, 4 -- concentrated in profile 2, the
widest tableau, as intended.

Those 41 are throughput artifacts, not defects: sampling one, it is a 330 KB
8,928-line formula that Primo answers `sat` in under 60s, agreeing with z3.
`fuzz_external.py` copies every non-`PASS` case into `bugs/`, so **a non-empty
`bugs/` directory is not a bug count** -- check the outcome column before
reading it as one.

The earlier job 118543 reported 0 bug triggers, but over 22 of 34 seeds, for
the reason in [](#sec-invalid-seeds); it is superseded, not evidence for the
widened corpus. Job 118548's own mutation tasks still used z3 as the only
reference, predating [](#sec-yices).

See `docs/run-log.json` for the per-run entries.
