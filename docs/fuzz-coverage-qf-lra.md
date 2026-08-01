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
`tropical-matrix` at all. Those iterations run but cannot yield a verdict.
Raising `--solver-timeout` is the lever, at a proportional cost in throughput.
That residue is genuinely covered by the differential benchmark runs, where the
division's `:status` fields give an oracle needing no reference solver.

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

450 local cases across profiles 1-3 against z3: all PASS, zero disagreements.
Job 118543 reported 0 bug triggers, but over 22 of 34 seeds, for the reason in
[](#sec-invalid-seeds) -- it is superseded, not evidence for the widened
corpus. See `docs/run-log.json` for the per-run entries.
