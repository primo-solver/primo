# SOI experiments

This chapter is for reviewers deciding whether the optional SOI profile is
ready for use. It answers how inputs and parameters were frozen, which
mechanisms were isolated, how correctness was checked, and what the measurements
support. Prerequisites are [LRA operations](../implementation/lra.md) and the
[SOI algorithm](../implementation/soi.md). Read the
[paper guide](soi-guide.md) for the algorithmic basis and the
[dated audit](soi-implementation-audit.md) for the preserved baseline findings.
Sparse remains the solver default.

## Scope and immutable baseline

The corpus is the frozen 1,000 conjunctions from the
[earlier call-extraction study](lra-call-ablation.md). Inputs, manifest and
reference labels retain their content hashes. A cold process reconstructs each
conjunction: it does not reproduce the original warm tableau, learned clauses,
Boolean search or assertion order of its parent SMT run. Parent identifiers are
retained for clustered uncertainty estimates.

The immutable historical solver is source `d4944eb`, binary SHA-256
`710929c58c18970cf4fccf7eb479fb95bb1410e6b46559433e4af80b55a9eee8`.
The final experimental solver is `9a1efcf`, SHA-256
`41b3cb83e8b1cd7b5962301cbc188b9298bd8e5c88ac2f7e62328fec33cfe30b`.
New mechanisms are explicit flags. The new binary's historical and Sparse
profiles also run, separating binary-wide observability cost from an algorithm
change. Historical audit artifacts and binary are preserved.

## Qualification before timing

Each implementation milestone has a separate source commit and SLURM-built
binary, normal solver tests, applicable ASAN tests, selected-premise conflict
refutation fixtures and a fresh reference-checked generated gate. Final
qualification includes all 24 candidate profiles (12 mechanisms by two fixed
elimination settings), 1,000 cases per profile, plus a separate 3,000-case
combined-profile campaign and real-seed typefuzz mutations. Wrong answers,
invalid models, crashes and untriaged disagreements block acceptance.
Timeouts are separate outcomes and are not counted as passing cases.

The conflict fixtures reconstruct original equations and selected premises,
including strict bounds, multi-row cancellation, non-monotone subsets and a
propagated bound expanded to its original antecedents. Z3 must return exactly
`unsat` with a successful exit. This focused independent check does not claim
to certify every runtime theory lemma.

Successful SAT timing rows are replayed separately with native model checking
and direct Dolmen validation. Replays preserve the original assertions and use
the exact binary/profile that produced the timing row. Model costs do not enter
speed measurements. The ledger records counts and tool identities; a timing
result alone is not a completed correctness qualification.

(parameter-pilot)=
## Parameter pilot

`select_soi_pilot.py` froze 100 inputs with seed 20261102, stratified by family,
reference outcome, historical pivot range and previous SOI fallback/censoring.
The selected files and their strata are in
{soifinalsource}`the selection <benchmarks/qf-lra-simplex-ablation/soi-improvements-pilot.json>`.
Before execution, the grid fixed chunk budgets 4/16/64 and stall limits 8/16/32,
a paper-selector control, three repetitions, Gaussian elimination on, fixed
elimination off, and a 15-second process limit.

SLURM jobs `323177_0–19` ran the grid; `323198` summarized it. The paper control
had 76 stable solves and process PAR2 837.458 seconds. Every short-column arm
had 88 stable solves and lost none of the control's stable solves. The
predeclared rule chose **budget 64, stall limit 8**, PAR2 501.415 seconds.
PAR2 here sums per-input mean repetition costs with a 30-second penalty for
unsolved repetitions. These are pilot results, not full-corpus acceptance.
Budget differences were small and do not establish a statistically superior
chunk size; the budget changes sorting work, whereas the stall limit changes
the repair path.

The [parameter record](../coverage/soi-pilot-20261102.json) and
{soifinalsource}`frozen choice <benchmarks/qf-lra-simplex-ablation/soi-parameters.json>`
precede the full run. Public defaults remain chunk/stall 16/16, historical
selection, caching off and linear maintenance off.

## Frozen full comparison

The committed plan is
{soievalsource}`soi-full-plan.json <benchmarks/qf-lra-simplex-ablation/soi-full-plan.json>`.
Each profile uses Gaussian elimination, each fixed-elimination setting, all
1,000 inputs, three repetitions and the same 15-second process limit. This
produces 84,000 uninstrumented process runs across 28 configurations.

| Profile | Added mechanism or comparison |
| --- | --- |
| Original Sparse, original SOI | Immutable archived binary controls |
| Sparse, historical | Corresponding profiles in the new binary |
| Paper | Exact reference score, complete crossings and early row conflicts |
| Short | Paper plus short columns and the frozen 64/8 fallback parameters |
| Cache | Short plus maintained activity counts and conflict preference |
| Linear | Cache plus objective merge, support-only incidence, coefficient reuse and convex pruning |
| Final | Linear plus Figure 3.6 row-growing explanations |
| No-short, no-cache, no-linear | Remove one mechanism from final |
| Linear (also no-growing) | Final with historical recursive reduction |
| Deletion, none | Final geometry with alternative explanation reduction |

Each input's configurations and repeats run on one allocated physical CPU,
with deterministic shuffled configuration order. Inputs are distributed over
compute nodes; metadata records host, CPU, affinity, SLURM job, resource limits,
commands, source and binary hashes. Pairing avoids comparing different binaries
on different hosts for the same input; it does not eliminate within-host load
or thermal noise. Speed jobs reserve one physical CPU and 8 GiB and do not run
on the SSH login host.

A separate one-repetition diagnostic run enables `--lra-call-profile`.
Its times are not headline process times. It exposes check duration, selected
and zero-progress steps, candidates examined, crossings generated and examined,
objective updates/terms, prefix and fallback pivots, cache work, minimizer
predicate/term work, retained rows and reason literals. Fixed-elimination
pivots during assertions remain visible. Check-local counters alone must not be
interpreted as whole-process work.

## Analysis and acceptance rule

A stable solve completes decisively in all three repetitions. Whole-process
PAR2 uses measured elapsed time for each successful repetition and twice the
15-second limit for each unsolved repetition, then averages repetitions within
an input and sums inputs. Unknown labels are not silently turned into trusted
reference answers; decisive cross-profile disagreements are checked separately.

Paired uncertainty resamples parent SMT problems, retaining all selected
conjunctions from each sampled parent. The summary reports a 95% parent-bootstrap
interval and stable gains/losses. Promotion within opt-in SOI requires **no lost
stable solves** against historical SOI and a negative PAR2-change interval
excluding zero on the production fixed-elimination-off setting. Sparse remains
the overall default. The same report shows fixed elimination on, successive
mechanisms and leave-one-out comparisons against final. A failed promotion
criterion leaves the new arms experimental, with the result committed.

## Full results

Timing job `323313`, separate model replays `323314` and diagnostics `323315`
implement the frozen plan. Their final summaries, independent model coverage and
acceptance decision are recorded here after collection. Until that evidence is
complete, no full-corpus speedup or default qualification is claimed.
