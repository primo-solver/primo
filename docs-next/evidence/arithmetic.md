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

Rejected prototypes and unimplemented H4/H6/H11 directions remain discoverable
through the [research register](../leads/index.md). Keeping their negative
results prevents a later proposal from mistaking an already rejected design
for an untested idea.
