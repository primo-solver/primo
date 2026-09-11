# Profiling the incremental QF_UF gap to Z3

This is a diagnostic study of the qualified assumptions implementation, not a
solver change or new performance qualification. The source is `8573f60`, whose
solver implementation is unchanged from `fa2fe5c`. The full-corpus timings and
model qualification remain those in [](assumption-incremental-uf).

## The gap is concentrated

The original SLURM comparison uses 1,778 native sessions, 30 seconds per session,
and a 10,000 theory-conflict budget per Primo query. Among the 1,772 sessions
completed by assumptions and Z3, the median Primo/Z3 runtime ratio is **1.186**,
the 90th percentile is **2.743**, 51 sessions are at least 5 times slower, and four
are at least 10 times slower. Paired total runtime is 498.485 versus 290.248
seconds. The family medians are 1.055 for Goel and 1.359 for CLEARSY.

Mean PAR2 is 0.4828 seconds for assumptions versus 0.1691 for Z3. Six Goel
`rushhour` sessions return unknown after roughly 2.6–5.0 seconds because of the
query conflict budget. Their six 60-second penalties account for 360 of Primo's
858.485 total PAR2 seconds. Comparing their penalties with Z3's actual times
accounts for about 62.7% of the aggregate PAR2 gap. These are resource-limited
misses; increasing the budget has not been evaluated here.

The repository ranker selected the largest fast-reference gaps. Absolute excess
time and long-session coverage supplied the other representative profiles:

| Index / session | Primo assumptions (s) | Z3 (s) | Ratio | Main finding |
| --- | ---: | ---: | ---: | --- |
| 14: `adding.5.prop1_ab_min_max` | 4.997 | 0.139 | 36.03 | Pairwise expansion and frontend work |
| 267: `firewire_tree.5.prop3_ab_core_max` | 23.456 | 3.607 | 6.50 | Search and EUF assignment/merge traffic |
| 852: CLEARSY `0002/00126` | 4.533 | 1.860 | 2.44 | Repeated preparation and working-state construction |
| 1688: CLEARSY `0023/00268` | 0.864 | 0.089 | 9.74 | Small per-check costs repeated 373 times |
| 656: `rushhour.1.prop1_ab_min_max` | unknown after 3.027 | 1.301 | unqualified | Second query reaches the conflict budget |

These times are the original SLURM measurements. Local profiling times below
are not substituted into this table.

## Pairwise distinct expansion

`TermBuilder::dispatch_distinct` in `src/smt/frontend.cpp` constructs every
unordered pair as a separate disequality. In case 14, the two large flat
`distinct` expressions have arities **1,051 and 73**, producing
`1051*1050/2 + 73*72/2 = 554403` pairwise constraints. By the first check,
Primo has 556,973 theory atoms, 558,006 SAT variables and 1,121,604 core terms,
although structural EUF preparation needs only 3,560 flat terms.

The qualified two-query phase totals explain where the time goes:

- Frontend: **3.046 s**, including **1.861 s** preprocessing and **0.415 s**
  clausification. These nested times must not be added again.
- Preparation/registration: **0.519 s**; fresh theory state and reconnection:
  **0.540 s**.
- Actual solve phase: **0.307 s**.

The fresh gprof run records 2.25 million term-table lookup calls, 1.13 million
term interning calls and 1.67 million flat disequality assertion calls. Its
sampled functions include term hash tables, vector growth, disequality insertion
and backend variable initialization. This is a representation/setup problem
before it is a difficult search problem.

A separate diagnostic with the exact Z3 5.1.0 binary reports first-query
`mk-bool-var=1800` and `solve-eqs-elim-vars=2118`; the second query reports
`mk-bool-var=6766`. These are Z3's own counters, not a claim that its internal
objects correspond one-to-one to Primo SAT variables. They support investigating
both compact distinct handling and equation simplification. No Z3 implementation
source was used.

There is a secondary amplification: `simplify_extended_terms_round` in
`src/smt/preprocessing.cpp` initializes arrays sized by the entire retained term
manager for each invocation. On this session, `simplify-extended-terms` alone
costs **1.396 s** across 2,354 assertion invocations. The global
`extended_term_count()` fast path stays open after any retained extended term
has appeared, including when a later assertion is small. Per-traversal touched
storage is a candidate for reducing this initialization cost without introducing
cross-scope semantic caches.

## Search and missing incremental equation elimination

Case 267 spends **18.104 of 23.456 s** in the solve phase, and **4.031 s** in
preprocessing nested inside 4.461 s of frontend work. It processes 8.59 million
EUF assignment notifications. The instrumented executable records 7.50 million
`merge_classes` calls and 59.83 million trail-entry restorations.

Within sampled executable time, the leading functions are:

| Function | Self samples |
| --- | ---: |
| `OnlineEufPropagator::apply_assignment` | 23.29% |
| `CaDiCaL::Internal::propagate` | 22.44% |
| `FlatCongruenceClosure::merge_classes` | 15.81% |
| `FlatCongruenceClosure::restore_trail_entry` | 4.49% |
| `FlatCongruenceClosure::process_use_list` | 3.85% |

Fresh state/reconnection costs only 0.117 s here. Optimizing copies alone cannot
close this case's gap.

Z3's first-query statistics report **22,931 eliminated variables**, 38,511
created Boolean variables and 308 conflicts. Primo's current assumptions
pipeline deliberately excludes equation elimination because it lacks the
incremental substitution/restoration proof and machinery. These observations
make equation elimination a substantial missing capability to investigate;
they do not isolate its causal speedup from the solvers' other differences.
A first-check Z3 counter must not be replaced with the final `-st` counter:
Z3 reports a different solver-statistics context after the next query.

The `rushhour` probe shows the same search-heavy category. Its second Primo
query reaches 10,005 theory conflicts (the callback budget can overshoot
slightly) and returns unknown. Z3 returns SAT; its first-query statistics report
6,272 eliminated variables. Neither raising the conflict limit nor changing
search/preprocessing options was evaluated in this study.

## Repeated work in long sessions

For the 413-query CLEARSY case 852, the qualified totals are:

| Phase | Seconds |
| --- | ---: |
| Frontend, including preprocessing | 0.047 |
| Preparation / Boolean-argument registration | 0.714 |
| Working-state construction and reconnection | 1.140 |
| Solve | 1.462 |
| Whole-session wall time | 4.533 |

Preparation plus state construction is approximately **41%** of wall time.
The gprof call counts confirm 413 copies each of `FlatTermIndex` and
`FlatCongruenceClosure`. Trail vector growth, structural copies and hash-table
construction are prominent sampled costs. Frontend rebuilding is already gone:
there is one frontend and one engine throughout the session.

Two specific retained-data scans remain in the current code:

- `BooleanClausifier::register_boolean_argument_terms` creates a fresh visited
  set and walks all retained theory atoms on every query.
- `PreparedFlatTheory::ensure_theory_atoms` attempts a map insertion for every
  retained atom on each query, although only new atoms are flattened.

For case 1688, 373 queries incur 0.082 s preparation, 0.167 s reset/reconnection
and 0.231 s solve time within a 0.864 s session. Its gprof run has only 16 samples,
so function percentages are too coarse to rank precisely; the phase timings and
373 structural copies are more useful evidence.

Some wall time lies outside these named phases, including teardown, CLI work and
other setup. Three local paired probes with statistics on/off were noisy: median
case-852 times were 2.000/2.057 s, and case-1688 times were 0.388/0.407 s. They show
no consistent benefit from suppressing output. Do not attribute the remaining
gap to statistics printing on this evidence.

## Recommended implementation order

1. Remove repeated work over old atoms and full-manager traversal scratch
   initialization. Use append-only preparation/registration cursors and bounded
   per-traversal scratch storage, with allocation/scope tests. Preserve complete
   Boolean argument registration and the backend reconnection/root-replay contract.
2. Design compact handling for large `distinct` assertions. This targets the
   four worst completed-session ratios and avoids creating hundreds of thousands
   of binary atoms. It needs an original design, congruence-conflict explanations,
   Boolean/scoped semantics and independent model tests before implementation.
3. Add incrementally sound equation elimination with explicit substitution and
   restoration support, grounded in the literature already listed in
   [](assumption-incremental-lra). Re-enabling the existing whole-goal tactic is
   not a valid implementation. Then reevaluate `firewire` and `rushhour`.
4. Investigate sharing immutable flat structure and reducing working-state
   allocation. Keep fresh query facts, valid SAT learning and callback lifetimes
   intact; no selective root-replay shortcut is justified by this profile.

These are ranked targets, not implemented optimizations or measured speedups.

## Reproduction and evidence scope

`scripts/profile_native_uf_gaps.py` reuses the tested native stdin runner and
classifier, checks input/reference/binary identities, writes each result before
moving on, and keeps nonzero exits/errors above apparent answers. It exports
whole-session eligibility to `scripts/rank_qfuf_gaps.py`; the legacy CSV answer
column contains the last answer, while full definite sequences are checked first.

The local machine reports an Intel Core Ultra 7 155U. The gprof build is
RelWithDebInfo (`-O2 -g -pg`, link `-pg`), CaDiCaL `3.0.0-7b99c07`, and the existing
profiling configuration has warnings-as-errors disabled. `perf stat -e task-clock
-- true` failed because no supported task-clock PMU event is exposed. No permission
or system configuration was changed. gprof samples cover the executable and its
statically linked SAT code; shared-library/instrumentation costs and inlining
limit attribution. Percentages refer to sampled executable time, not wall time.

```bash
python3 scripts/profile_native_uf_gaps.py \
  --timing results/uf-native-timing-fa2fe5c \
  --root tmp/native-uf-inputs --manifest tmp/native-uf-inventory/manifest.json \
  --solver results/uf-z3-profile-8573f60/primo-gprof-8573f60 \
  --expected-git 8573f60 --indices 14 267 852 1688 656 \
  --timeout 120 --out results/uf-z3-profile-replay
```

Add `--reference-stats PATH_TO_PINNED_Z3` to collect per-query reference counters.
That diagnostic inserts only `(get-info :all-statistics)` after each check and
preserves the transformed input alongside its original hash. An initial final
`-st` probe was superseded by per-query collection because final counters can
hide the first query's work. Reference performance timings were not rerun as a
benchmark. `--plain` and `--omit-stats` reproduce the CLI-output diagnostic with
the preserved uninstrumented binary and `--expected-git fa2fe5c`.

All five gprof runs exit normally: four complete sessions and the known
resource-limited session, with 791 definite answers matching the reference and
one unknown. Both reference-statistics probes complete all five sessions and
792 answers. The 12 plain on/off runs complete their sessions and match the
reference. Every process has a 120-second wall bound; native queries additionally
retain the 10,000-conflict limit. None times out or disagrees.

The metadata is `benchmarks/uf-z3-profile-8573f60.json`; raw profiles, gmon files,
preserved binaries, driver snapshots, transformed reference inputs and build
logs are collected under `results/uf-z3-profile-8573f60`. Exact commands, hashes,
verification and experiment limitations are also recorded in `run-log.json`.
No new SLURM jobs were submitted for these bounded local profiling probes.
