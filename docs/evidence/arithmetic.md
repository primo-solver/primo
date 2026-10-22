# Arithmetic engineering and retained evidence

Arithmetic development produced both implemented mechanisms and rejected
alternatives. The current [Real](../implementation/lra.md),
[integer](../implementation/lia.md), [preprocessing](../implementation/preprocessing.md),
and [combination](../implementation/combination.md) chapters own the algorithms.
This page records the associated decisions without treating historical timings
as current qualification.

## Mechanism and evidence map

H identifiers come from the original heuristics register; they are not API names.
All named run IDs below are in the {source}`run ledger <docs/run-log.json>`.

| Mechanism | Current explanation | Retained evidence and decision |
| --- | --- | --- |
| H1 direct bounds and shared polynomial rows | LRA preparation and normalization | {source}`H1 design and comparisons <docs/qf-lra-heuristics.md#L217-L340>` retain the incremental design progression; do not combine its differently configured promotion bars. |
| H2 small rationals | LRA exact-number representation | {source}`H2 ablation <docs/qf-lra-heuristics.md#L341-L439>` preserves the allocation motivation and measured comparison. |
| H3 sparse entering selection | LRA sparse pivoting and finite fallback | {source}`H3 investigation <docs/qf-lra-heuristics.md#L440-L538>` separates entering-variable sparsity from the optional leaving-variable choices. |
| H7 fixed-basic elimination | [Fixed-basic elimination](../implementation/lra.md#fixed-basic-elimination) | `2026-07-30-qf-lra-fixed-variable-elimination-h7`: the 595-file/60-second comparison did not justify pure-LRA promotion; row freezing was removed. The later `2026-08-08-convex-no-fixed-elimination-ablation-121439` concerns a different mixed route. |
| H8 sorted row merge | LRA sparse row update | `2026-07-31-qf-lra-h8-pivot-merge` and `2026-07-31-qf-lra-h8-scrambled-595`, `e6020ca`: accepted replacement of append/sort work. ASan has a separately recorded scope. |
| H9 bounds-only rollback | LRA backtracking; SOI exception | {source}`H9 evidence <docs/qf-lra-heuristics.md#L846-L876>` preserves its exact earlier baseline; basis and assignment need not be restored when bounds are undone. |
| H10 occurrence maintenance | LRA affected-row maintenance | `2026-08-01-qf-lra-h10-delta-occurs-in`, `aa44320` against `4f9b741`: 398→439 solved on 595 files at 60 seconds, +41/−0 within that comparison. Earlier unsuccessful occurrence structures remain in the old register. |
| H5 phase hints | [Model phase hints](../implementation/lra.md#model-phase-hints) | Initial local diagnostic and fuzz records are separate from `2026-08-26-lra-model-phase-monotone-promotion-test`, `3c2fbf1`; the full-corpus result differs substantially by logic. |
| SOI and its repairs | LRA objective, breakpoint walk and conflict explanation | {source}`arithmetic history <docs/arithmetic.md>` and {source}`paper deviation map <docs/papers/fast-linear-arithmetic-dpllt.md>` distinguish the optional algorithm from the default repair loop. |
| Integer patching, GCD, cuts and branching | LIA chapter and its [preservation map](../coverage/lia.md) | The original integer record retains the scaling/auxiliary/infinitesimal defects, mutation failures, and later repair gates; early clean random campaigns did not cover all real-seed shapes. |

## Integer-driver model audit (7 October 2026)

The model-coverage audit for candidate `25b44cc` is complete. It reconciles
successful SAT measurements with separate Dolmen replays of the same binary,
input hashes and solver options. The audit checks exact shard and row coverage,
replay identities, process exits, and saved assertion/model hashes. Timing data
and PAR2 remain unchanged.

| Measurement scope | SAT measurements | Direct Dolmen passes | Explicit syntax exceptions |
| --- | ---: | ---: | ---: |
| Full QF_LIA, two rounds | 13,187 | 13,187 | 0 |
| Full QF_UFLIA, two rounds | 814 | 814 | 0 |
| Preservation profiles, reruns and probes | 584 | 564 | 20 |
| Previously audited lost-case reruns | 50 | 50 | 0 |

The 20 exceptions are LRA inputs whose constant-denominator syntax Dolmen
rejects under their declared logic. Dolmen accepted the same saved assertions
and models with its logic setting widened to QF_NRA. A fresh recheck records
the validator executable hash and exact command. These remain separately
classified; they are not direct passes under the original logic. Across the
selected records there are no missing SAT measurements, invalid models, or
changed replay answers. Repairing 21 failed setup attempts supplied 213 of the
preservation passes above; their original failed attempts remain available.

The recorded-reference join found 20,796 agreeing full-corpus measurements and
no disagreements. Another 261 measurements, representing 138 distinct inputs,
have no decisive recorded reference answer. The join includes completed rows
from an interrupted cvc5 campaign for answer comparison only. Model checks do
not certify UNSAT answers, and missing reference answers are not counted as
agreements.

This closes the outstanding model-coverage reconciliation, not the migration's
performance acceptance. The repeated `prp-67-50` cutoff loss and the recorded
small-suite runtime flags remain as previously measured. Generated and retained
session gates retain their separate audits. Exact commands, hashes, resources,
SLURM jobs and artifact locations are recorded in the run ledger under
`2026-10-07-integer-model-coverage-audit`, linked to
`2026-10-06-integer-completion-unification`.

## Different routes can justify different choices

At `3c2fbf1`, the model-phase/monotone comparison recorded 1,655 default QF_LRA
solves against 1,678 with model phase, with 34 gains and 11 losses. Mixed Real
results lost three solves with model phase. QF_LIA gained 178 and lost 137,
for a much smaller net gain. These are configuration-specific historical
results, not a universal speedup or a reason to change today's defaults.

Likewise, the H7 pure-LRA result does not determine its mixed-theory default.
Keep route, binary, input set, timeout and correctness coverage attached to
each conclusion. Historical local or incomplete-sanitizer checks remain such;
they are not rewritten as equivalent to later cluster gates.

## Combination evidence

The {source}`general-component migration record <docs/n-way-combination-migration.md>`
retains paired QF_UFLIA runs, family results, fuzzing and separate model checks.
The {source}`Real coordinator record <docs/nelson-oppen.md>` and
`2026-09-16-qf-uflra-forced-equality-extraction` / `...-forced-equality-merge`
retain their own revisions and comparisons. Generalizing the Real route remains
an open implementation lead, not a consequence of those evaluations.

The {source}`combination regression investigation <docs/nelson-oppen-qf-lra-regression.md>`
is a dated incident and diagnosis. It must not introduce the current chapter
as though the original regression were still an untriaged defect.

## Published results and unresolved provenance

The [published QF_LRA report](../papers/primo-qflra-results.md) preserves its
tables, cutoffs, hardware and configurations. Exact table-to-run mappings are
still missing. They belong in the [evidence work list](../work/evidence.md),
with the missing commit/manifest/command/artifact identities stated explicitly.
Matching a corpus size or option pair is insufficient to infer a run identity.

## Migration-branch option ablation

The October 10 recheck compares the migration implementation with both options
still off by default. Four profiles use the same binary: default, model phase,
monotone elimination, and both. The earlier October promotion on `main` measured
a different implementation and does not qualify this branch.

The full 1,753-instance corpus is the primary target for default selection and
regression assessment. Competition-selection results are supplementary; this
campaign does not select a competition portfolio. On the full corpus, both
flags together lead with 65 additional solved inputs and an 18.7% PAR2 reduction.

The {ablationsource}`driver <scripts/qualification/lra_ablation/run.py>` and
{ablationsource}`SLURM wrapper <scripts/qualification/lra_ablation/run.sbatch>`
record binary and input identities, exact arguments, CPU and allocation metadata. Generated answer/model
gates and tracked regression seeds precede timings. The timing sets are all
1,753 QF_LRA inputs at 30 seconds and the 519-input competition selection at
1,200 seconds. Each profile's successful SAT rows receive a separate direct
Dolmen replay; model checking does not contribute to runtime or PAR2.
Generated FuzzSMT model checks explicitly use the QF_NRA semantic superset for
constant-denominator syntax; real-corpus validation retains the original logic.

The fresh gates cover 4,000 generated cases per profile and 14,423 direct SAT
model checks across the profiles. Independent replay also covers all 3,922
retained mutations per profile. Dolmen and Z3 identify 399 of those inputs as
invalid: 314 have unbound identifiers and 85 contain forbidden nonlinear
arithmetic. These inputs are recorded by hash and excluded from pass counts;
timeout-only rows are also counted separately. The original failed replay jobs
and their diagnostics remain in the artifact trail.

The {ablationworkflow}`timing pipeline <scripts/qualification/lra_ablation/queue_followups.py>`
audited all four full-corpus profiles before starting
the competition selection. Both timing sets finished before model replay started.
Failed dependencies stop later stages, and a hard answer failure detected by the
{ablationworkflow}`guarded wrapper <scripts/qualification/lra_ablation/guarded.sbatch>` cancels its
timing array. Timing, model and selection-reference audits have completed;
triage of repeated performance losses remains necessary before promotion.

Both timing arrays and all separate SAT-model audits completed successfully on
the frozen `4200525` binary. The solver sources are equivalent to `df42f03`.
The measured results are:

| Profile | Full corpus solved, 30 s | Selection solved, 1,200 s | Full PAR2 reduction | Selection PAR2 reduction |
| --- | ---: | ---: | ---: | ---: |
| Default | 1,466 / 1,753 | 489 / 519 | — | — |
| Model phase | 1,511 / 1,753 | 501 / 519 | 13.7% | 29.3% |
| Monotone elimination | 1,487 / 1,753 | 496 / 519 | 5.2% | 19.1% |
| Both | 1,531 / 1,753 | 499 / 519 | 18.7% | 28.5% |

PAR2 reductions compare each profile with default at the same timeout. All
4,779 successful SAT rows across the two datasets passed direct Dolmen model
validation, with no answer disagreements among profiles. The selection also
matches the input hashes of the stored compute-node reference runs: OpenSMT
497 solved, Yices 489, Z3 476, and cvc5 471. All 28 pairwise joins among the
four native and four reference profiles have zero decisive disagreements.

![Full QF_LRA corpus ablation at 30 seconds](../_static/qf-lra-migration-ablation-full30.svg)

![Competition selection ablation at 1,200 seconds](../_static/qf-lra-migration-ablation-selection1200.svg)

The aggregate gains do not settle default promotion. Relative to default, the
full run has 28 lost solves with model phase, 9 with monotone elimination, and
23 with both; each profile loses one solve on the selection. The
{ablationworkflow}`paired driver <scripts/qualification/lra_ablation/paired.py>` and
{ablationworkflow}`batch wrapper <scripts/qualification/lra_ablation/paired.sbatch>`
run three repetitions of each lost solve on a single physical CPU per pair,
with alternating profile order and separate model replay. Full-corpus model
replay waits for its three paired timing arrays, so supplementary selection
reruns do not delay that correctness stage. Both flags together are the leading
candidate under the full-corpus priority; their repeated losses still need
assessment before changing defaults.

Run `2026-10-10-migration-lra-option-ablation` in the ledger records the completed
timings, model coverage and pending loss triage. Options remain off by default.
The live ledger remains in use until the separate results-storage migration
is implemented.

Rejected prototypes and unimplemented H4/H6/H11 directions remain discoverable
through the [research register](../leads/index.md). Keeping their negative
results prevents a later proposal from mistaking an already rejected design
for an untested idea.
