# Incremental UF: scan only newly appended atoms

Commit `7ac555c` implements and qualifies the atom-cursor part of
[lead 22](#lead-incremental-uf-scans), based on merged `605062f`. The fresh scoped,
default and full native model gates pass. The change was merged into
`main` by `40adb28`, preserving the exact tree of
`perf/incremental-uf-atom-scans` at `3a96e86`. Rebuild remains the default.

## Representation and invariant

The original pre-implementation checklist is in [](assumption-incremental-uf.md).
`BooleanClausifier::Implementation` now keeps a numeric cursor into its append-only
atom registry. Registration walks each newly appended root, including atoms
appended by registration itself, and advances after completing that root. Term
structure is immutable, and Boolean-argument registration is independent of
scope activity and polarity. Pop therefore does not rewind the cursor; reset
recreates its owner. Child IDs are copied before helpers can grow atom storage.

`IncrementalUfEngine` owns a second cursor and passes only the new registry suffix
to `PreparedFlatTheory::ensure_theory_atoms`. The helper accepts a read-only span;
its arbitrary-batch deduplication and definition-installation semantics remain
available to mixed-theory callers. The engine requires one prepared atom per
new registry entry before advancing. The borrowed span stays valid because
preparation does not mutate the clausifier registry.

The only persistent additions are two `size_t` cursors. The visited-term set
remains local to each registration call, so shared subterms can still be revisited
under later roots. This is not a claim of linear total DAG traversal. Whole-manager
scratch initialization and working-state structural copies remain open leads.
All atom observation, protection, root-assignment replay, working closure reset,
phase selection, assignment alignment and model construction retain their prior
behavior. No new SAT clause or theory inference is introduced.

Per-query counters are `:session.euf-registration-atoms-scanned` and
`:session.euf-preparation-atoms-scanned`. On the 413-query CLEARSY `0002/00126`
profile, the old code visits 908,809 retained registry entries across checks;
the candidate scans 2,459 roots/inputs in each of the two stages. The local
preparation total falls from 317,002 to 7,306 microseconds. This mechanism
measurement does not replace whole-session SLURM timing.

## Measured effect

Full native timing job **142774** uses the original 1,778-session archive,
30 seconds per whole session, and 10,000 theory conflicts per query. Source,
archive and manifest identities are pinned in the metadata. Baseline `fa2fe5c`
has the same solver sources as merged `605062f`; its previously qualified
full-archive timings are reused, together with recorded Z3/cvc5 references.

| Metric | Baseline assumptions | Candidate assumptions |
| --- | ---: | ---: |
| Completed sessions | 1,772 | 1,772 |
| Resource-limited sessions | 6 | 6 |
| Completed queries | 29,978 | 29,978 |
| Mean PAR2 (s) | 0.48284 | 0.47196 |
| Completed-session total time (s) | 498.485 | 479.143 |
| CLEARSY total time (s) | 98.412 | 87.015 |
| Maximum measured RSS (KiB) | 919,232 | 925,480 |

There are no gained or lost completed sessions and no definite-answer disagreements.
Mean PAR2 falls 2.25%, completed-session time 3.88%, and CLEARSY total time 11.58%.
The paired per-session median candidate/baseline ratio is 0.970. Recorded Z3 mean
PAR2 remains 0.16909 seconds: this change does not close the search gap or solve
the six hard `rushhour` sessions. The maximum observed RSS is about 0.7% higher;
there is no memory-reduction claim.

A separate fresh paired job **142771** alternates baseline/candidate ordering
across three repetitions on each original profiling case. These medians help
separate the mechanism's effect from changes between full-archive runs:

| Native index / case | Baseline median (s) | Candidate median (s) | Candidate / baseline |
| --- | ---: | ---: | ---: |
| 14: `adding.5` | 4.943 | 4.802 | 0.971 |
| 267: `firewire_tree.5.prop3` | 23.762 | 23.947 | 1.008 |
| 852: CLEARSY `0002/00126` | 4.374 | 3.445 | 0.788 |
| 1688: CLEARSY `0023/00268` | 0.866 | 0.788 | 0.910 |
| 656: `rushhour.1.prop1` | unknown | unknown | unqualified |

The long-session improvements repeat on SLURM. `firewire` remains approximately
flat, as expected from its search-dominated profile. The noisy local three-pair
probe gives smaller median gains on the two CLEARSY cases (about 11%); it is
retained as diagnostic evidence, not substituted for these measurements.

Two CLEARSY rows (914 and 1151) were 51% and 12% slower than their recorded
baseline in the full run. Fresh paired rechecks in job **142815**, three rounds
per case, do not reproduce those regressions: median candidate/baseline ratios
are 0.881 and 0.874. The original full-run rows remain in the aggregate; the
rechecks do not replace them, and no precise cause for the variation is claimed.

## Correctness and qualification boundary

Fresh gate **142757** passes 1,000 generated and 1,000 native-seed-mutated scoped
sessions at `7ac555c`: 16,000 reference-matching answers and 9,432 independently
validated actual SAT models. There are no wrong answers, crashes, parser/type
errors, timeouts, unsupported results or reference disagreements. It exercises
the exact Auto assumptions profile used for timing. Off and Online/Conflict
schedules retain focused unit coverage.

The deterministic regression job **142756** passes five test targets and nine
Boolean/model witness sessions (19 queries, 13 SAT models). The default-preservation
jobs **142762/142763** pass all 25 real files, 200 queries and 121 SAT model checks,
with no lost solve. Their file-time total is 16.540 seconds versus the prior
16.395 seconds; this small diagnostic difference does not establish a default
speed change.

Full native validation **142793** is separate from timing, with 120-second
response/model-check budgets. Array concurrency was raised from eight to sixteen
after checking available CPU/memory capacity; per-task limits stayed unchanged.
All 1,772 completed sessions pass, covering 29,978 queries and 25,027 actual SAT
models. Every replayed answer matches timing; all 16 shards match their timed
binary, driver, manifest and solver-argument identities. There are no missing
models, failures, changed answers or validation timeouts. The six incomplete
timed sessions remain unqualified, including their earlier SAT answers. These
results qualify the scan optimization for opt-in use; they do not justify
promoting assumptions mode to the default.

Local `make check` passes 34 C++/CLI and 166 Python tests; `make asan-test` passes
34 tests. Lint, docs, focused clang-tidy/analyzer and advisory include scans pass.
The whole-tree review/static baseline remains that of the unchanged `fa2fe5c`
files; the three modified translation units receive fresh scans and ownership,
hygiene and efficiency review. The new regression checks zero scans on repeated
queries, registration-induced growth, a previously scanned Boolean term later
used as a UF argument, 96 scoped redeclarations, retired-root reuse and reset.
Existing tests retain cancellation/resource-limit recovery coverage.

## Reproduction and evidence

The gate, native stdin timing/model driver and default-preservation scripts are
unchanged. `scripts/slurm_incremental_uf_pair.sbatch` records the paired diagnostic;
it uses the same native driver and exact archive indices. The preservation and
submission commands, binary/tool hashes, seeds, limits and job IDs are recorded
in `docs/run-log.json` and `benchmarks/incremental-uf-atom-scans.json`. Raw local
diagnostics and collected SLURM metadata are
under `results/uf-atom-scans-605062f`.

Collection jobs **142810/142818** produced six local archives totaling
190,335,408 bytes. All checksums, compressed streams and tar listings pass.
The native-model parts contain exactly 25,027 model files and 29,978 query contexts;
the gate part contains 9,432 model files. The archived candidate binary matches
the timing/replay SHA. All 120 recorded job/step rows completed successfully.
Archive hashes and the terminal audit are included in the metadata and artifacts.

This is an original reduction of repeated work under the existing append-only
invariants, not a new paper-backed congruence-closure algorithm. Its algorithmic
basis and remaining callback/model obligations are the local design note above
and [](assumption-incremental-uf.md). The earlier profile remains
[](uf-z3-profiling.md).

## Additional OpenSMT reference

[](opensmt-incremental-comparison.md) adds OpenSMT 2.9.2 using the same native
inputs and session limits. The Primo and existing reference timings here remain
unchanged; the new report separates completed sessions, errors, partial query
progress and model-qualification coverage.
