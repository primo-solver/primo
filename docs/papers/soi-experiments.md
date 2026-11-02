# SOI experiments

This chapter is for reviewers deciding whether the optional SOI profile is
ready for use. It answers how inputs and parameters were frozen, which
mechanisms were isolated, how correctness was checked, and what the measurements
support. Prerequisites are [LRA operations](../implementation/lra.md) and the
[SOI algorithm](../implementation/soi.md). Read the
[paper guide](soi-guide.md) for the algorithmic basis and the
[dated audit](soi-implementation-audit.md) for the preserved baseline findings.
Sparse remains the solver default. The {ref}`lifecycle follow-up <soi-fallback-followup>`
separately evaluates suspending inactive SOI bookkeeping during Bland repair.

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

The retained real-seed gate, jobs `324263` and `334724`, independently
classified all 480 unique mutants from 24 seeds: **52 reference agreements and
428 invalid generated nonlinear inputs**. The 52 agreements comprise 16 SAT and
36 UNSAT answers. Nine inputs initially appeared as reference timeouts paired
with Primo's nonliteral-multiplication rejection. Replay retained Z3's partial
output: every one already contained an explicit nonlinear-logic rejection
before Z3 timed out while processing the remaining assertions. Those identical
input hashes are now classified as invalid generated input; no native rejection
remains unresolved. Even with restricted generation signatures, typefuzz can
mutate an existing scalar factor. Invalid inputs are separate from passes.
The [mutation record](../coverage/soi-mutations-20261102.json) preserves the
initial counts, replay evidence and final classifications. Earlier unretained
runs with zero bug triggers are not qualification evidence.

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

The actual timing hosts were compute nodes 03–08, all Intel Xeon E5-2690 v4
2.60 GHz. Affinity contained the two SMT siblings of one reserved physical
core; no other worker used that core. The established process protocol allows
a two-second cancellation grace after its 15-second deadline. Model replay
allows 90 seconds for the solver and 30 seconds for Dolmen.

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

The {ref}`overall pivot-policy table <lra-simplex-overall-current>`
places the combined SOI arm alongside Bland, Dantzig and Sparse, with a common
1,000-input score and explicit study provenance.

All 84,000 timing rows completed the protocol, with zero solver errors,
unsupported results, known-label wrong answers or cross-profile disagreements.
All **54,400 successful SAT rows** were independently replayed with native model
checking and direct Dolmen validation; every replay passed. The one frozen
unknown-label input remained unsolved by every profile in all repetitions.

The [compact process report](../coverage/soi-results-20261102.json) retains all
configurations, outcomes, families, stable process-time distributions and paired
comparisons. The [qualification and diagnostic report](../coverage/soi-qualification-20261102.json)
joins complete model coverage and separately measured work. Raw-report hashes
and SLURM artifact paths are retained there and in `docs/run-log.json`.

| Profile | Fixed off: stable | Fixed off: PAR2 s | Fixed on: stable | Fixed on: PAR2 s |
| --- | ---: | ---: | ---: | ---: |
| Original Sparse | 996 | 528.677 | 996 | 524.440 |
| Sparse, new binary | 996 | 528.312 | 996 | 524.092 |
| Original SOI | 915 | 4216.876 | 913 | 3858.308 |
| Historical, new binary | 913 | 4314.293 | 913 | 3879.773 |
| Paper | 919 | 4118.096 | 941 | 2987.392 |
| Short | 947 | 2516.991 | 978 | 1404.766 |
| Cache | 944 | 2595.536 | 977 | 1446.863 |
| Linear / no-growing | 948 | 2443.050 | 978 | 1390.600 |
| Final | 948 | 2447.731 | 978 | 1390.515 |
| No-short | 921 | 3877.790 | 951 | 2688.720 |
| No-cache | 951 | 2355.469 | 979 | 1343.198 |
| No-linear | 945 | 2590.041 | 977 | 1446.491 |
| Deletion | 948 | 2443.214 | 978 | 1395.737 |
| None | 948 | 2447.610 | 978 | 1395.298 |

The frozen final arm reduces production-setting PAR2 from **4,216.876 to
2,447.731 seconds (42.0%)** and increases stable solves from **915 to 948**.
It gains 35 stable solves and loses two. Across 460 parent groups, the paired
mean PAR2 change is −1.769 seconds/input, with a 95% parent-bootstrap interval
[−2.161, −1.402]. With fixed elimination on, it gains 67 and loses two stable
solves; the mean change is −2.468 seconds/input, interval [−3.435, −1.651].
These intervals concern **mean seconds/input**, whereas the table reports the
sum across 1,000 inputs.

**The final arm fails the zero-stable-loss promotion condition.** Historical
selection remains the default within opt-in SOI; Sparse remains the solver
default. The new arms remain experimental. Sparse retains 996 stable solves in
both binaries and elimination settings, and remains substantially faster than
the final SOI arm on this corpus. This comparison qualifies implemented Primo
profiles, not the competitiveness of the published algorithm in another solver.

The stable gains concentrate in LassoRanker (86 to 103), clock_synchro (94 to
102), miplib (92 to 96), tropical-matrix (93 to 96) and the selected Heizmann
family (107 to 109). The 17 selected latendresse inputs fall from one stable
solve to zero. Families with unchanged stable counts can still improve elapsed
time: selected `sc` and `uart` PAR2 totals decrease by 180.574 and 94.393 seconds.
The compact report retains all 12 families; this is not a uniform family win.

### Stable losses and runtime distribution

Each lost input completed in every original-SOI repetition and timed out in
every final repetition. New-binary historical SOI also solves all four listed
configuration/input pairs, so these losses cannot be dismissed as only a
binary-wide observability cost.

| Fixed elimination | Parent problem | Input hash prefix |
| --- | --- | --- |
| Off | `miplib/vpm2-10.smt2` | `f2517a86e811` |
| Off | `latendresse/ecoliMILPglycerolYices3-5000.smt2` | `fdb4e04e83e3` |
| On | `latendresse/shewanella-compact2b.smt2` | `ee2ee1f93bcf` |
| On | `latendresse/ecoliMILPglycerolYices3-5000.smt2` | `fdb4e04e83e3` |

The report contains full names and stable gains/losses for every comparison.
Runtime distributions use per-input median repetition times among stable solves;
p50 is the median, and p90/p99 use the nearest rank. They are conditional on
completion and do not replace PAR2 or stability comparisons.

| Fixed-off profile | p50 s | p90 s | p99 s |
| --- | ---: | ---: | ---: |
| original-sparse | 0.0570 | 0.6374 | 8.4378 |
| original-soi | 0.5484 | 5.9345 | 12.3907 |
| historical | 0.5679 | 6.3972 | 12.0353 |
| paper | 0.5981 | 6.0907 | 12.2221 |
| short | 0.0924 | 3.0759 | 11.1678 |
| linear | 0.0897 | 2.9925 | 11.1442 |
| final | 0.0898 | 2.9670 | 11.1760 |

### Leave-one-out and minimizer comparisons

The entries below compare each variant with the frozen final arm. Negative
mean PAR2 changes favor the variant. Intervals are the 95% parent-bootstrap
intervals in seconds/input. Loss/gain counts refer to final's stable solves.

| Variant | Off: mean change [interval] | Off: losses/gains | On: mean change [interval] | On: losses/gains |
| --- | ---: | ---: | ---: | ---: |
| no-short | +1.4301 [+1.1238, +1.7585] | 28/1 | +1.2982 [+0.8967, +1.7640] | 30/3 |
| no-cache | -0.0923 [-0.1563, -0.0386] | 0/3 | -0.0473 [-0.0758, -0.0258] | 0/1 |
| no-linear | +0.1423 [+0.0758, +0.2232] | 3/0 | +0.0560 [+0.0309, +0.0875] | 1/0 |
| linear | -0.0047 [-0.0154, +0.0009] | 0/0 | +0.0001 [-0.0005, +0.0007] | 0/0 |
| deletion | -0.0045 [-0.0153, +0.0010] | 0/0 | +0.0052 [-0.0002, +0.0163] | 0/0 |
| none | -0.0001 [-0.0008, +0.0006] | 0/0 | +0.0048 [-0.0007, +0.0161] | 0/0 |

Short columns and linear maintenance each materially help this frozen candidate.
Cached conflict preference hurts process PAR2 here: removing it gains three
stable solves with fixed elimination off and one with it on, with no losses
against final. The combined profile was not retuned after these results.
Row-growing reduction greatly reduces measured certificate work below, but its
process-time difference from the recursive arm has an interval crossing zero.
Deletion and no-minimization likewise show no established process-time advantage
against final. These results support retaining separate experimental controls.

### What the diagnostic work shows

The diagnostic run contains 28,000 processes with clocks enabled. The following
sums use **879 inputs completed by every diagnostic configuration**, one
repetition each. These are matched, instrumented complete cases, not the whole
corpus or the stable-process population above. Prefix traces from timed-out
processes remain censored; missing interrupted-check work is not counted as
zero. Exact per-profile complete/prefix coverage is in the diagnostic report.
Counter sums include only published work. A terminal assertion conflict can
finish before the next deferred check publishes objective updates; an absent
field does not prove zero assertion work. The report exposes absent-field
counts separately. Whole-process measurements still include that work.

| Fixed-off arm | Check s | Selected steps | Zero progress | Entering candidates | Prefix / fallback pivots |
| --- | ---: | ---: | ---: | ---: | ---: |
| historical | 1173.586 | 3,345,043 | 3,306,387 | 104,667,835 | 3,344,422 / 191,552 |
| paper | 1116.194 | 3,748,626 | 3,714,871 | 133,038,620 | 36,703 / 197,889 |
| short | 291.241 | 53,446 | 6,594 | 1,138,277 | 47,485 / 225,309 |
| cache | 314.353 | 53,446 | 6,594 | 1,138,277 | 47,488 / 225,309 |
| linear | 267.358 | 53,446 | 6,594 | 1,138,277 | 47,488 / 225,309 |
| final | 267.318 | 53,446 | 6,594 | 1,138,277 | 47,488 / 225,309 |

Paper selection alone spends many selections on zero-progress own-bound events;
the finite guard still matters. Short pricing plus the stall limit cuts this
prefix sharply. Final still performs 225,309 fallback pivots out of 272,797
check pivots on this population. Its improvement is therefore not evidence of
an unrestricted paper-algorithm execution. Sparse takes 168,811 check pivots and
8.800 check seconds on the same fixed-off inputs.

| Fixed-off maintenance arm | Generated crossings | Examined crossings | Objective incidence changes | Objective phase s |
| --- | ---: | ---: | ---: | ---: |
| cache | 9,291,982 | 9,291,982 | 1,585,163,537 | 96.427 |
| linear | 9,291,982 | 4,388,903 | 9,445,573 | 52.025 |
| final | 9,291,982 | 4,388,903 | 9,445,573 | 52.135 |

Convex stopping reduces examined events while generation still visits all
crossings. Support-only incidence changes remove most objective occurrence
rewrites; full old-row trail copies remain. These counts exclude generic pivot
incidence changes and rollback rebuilds. Objective phase time includes published
assertion work and overlaps check time when updates occur inside check; the
columns must not be added. Only three cached conflict preferences fire on these
matched fixed-off inputs, despite maintaining activity through every relevant
operation. This helps explain the observed cache cost, without proving that
caching is unhelpful on other workloads.

| Fixed-off minimizer | Predicate calls | Restricted terms | Selected certificate rows | Raw certificate reason literals |
| --- | ---: | ---: | ---: | ---: |
| Recursive | 2,342 | 864,465 | 1,170 | 1,684 |
| Row-growing | 169 | 48,694 | 52 | 212 |
| Deletion | 1,185 | 459,918 | 1,170 | 1,687 |
| None | 0 | 5,060 | 1,185 | 1,694 |

Row-growing performs 117 sign-driven growth additions on these inputs. Counts
cover restricted-objective certificates, including singleton sets, not ordinary
early-row conflicts or every SMT explanation. Raw literals precede SMT-level
antecedent expansion/deduplication. Arithmetic terms exclude witness comparisons
and GMP bit cost. Reduced certificate size does not establish minimality or a
Boolean-search speedup: these are cold conjunction replays.

Assertion-time elimination remains visible. On the same matched inputs, final
with fixed elimination on performs 146,177 check pivots **plus 155,755 assertion
elimination pivots**, versus 272,797 check pivots with it off. Check time drops
from 267.318 to 172.187 seconds while the sum of these pivot categories rises.
The whole-process table includes assertion costs; check-only work cannot be
used as its substitute.

## Reproduction and remaining limits

Timing array `323313`, SAT-model array `323314`, diagnostic array `323315`,
process summary `324124` and model/diagnostic join `324125` all completed.
Generated campaign `323208_0–5` has 2,959 PASS and 41 timeout cases; the separate
24-profile merge gate `323209_0–95` has 23,372 PASS and 628 timeout cases out of
24,000. Both have zero hard correctness failures. Every profile's counts and
all milestone commands are recorded in the run log. Whole-source clang-tidy and
advisory include review `323316` completed all 130 C++ translation units; known
analyzer/paired-header advisories are triaged there.

Use the committed plan with `scripts/qualification/soi_improvements/measure.sbatch`
for timing/profile/models. Binary-map paths and SSH details remain private;
logical identities and binary hashes are frozen. The analysis is
{soianalysis}`summarize_soi_improvements.py <scripts/summarize_soi_improvements.py>`;
model joining is {soiqualsource}`summarize_soi_qualification.py <scripts/summarize_soi_qualification.py>`.
The independent retained-mutant classifier is
{soimutationsource}`classify_soi_mutations.py <scripts/classify_soi_mutations.py>`.
The evaluated final command uses:

```bash
primo --quiet --stats --gaussian-elimination --lra-pivoting-rule=soi \
  --lra-soi-selection=short-column --lra-soi-pricing-candidates=64 \
  --lra-soi-stall-limit=8 --lra-soi-cached-conflicts \
  --lra-soi-linear-maintenance --lra-soi-minimize=row-growing INPUT.smt2
```

Add `--lra-fixed-elimination` for the other measured setting. Public defaults
are unchanged. The corpus selection favors extracted numerical checks and does
not represent arbitrary SMT-LIB Boolean workloads. Fuzzing and independent
model/explanation checks establish bounded evidence, not a universal correctness
proof. Full trailing, unfused activity refresh and source-specific safeguards
remain explicit deviations. The next performance experiment would need its own
frozen hypothesis and gates; this result does not authorize a new default.

(soi-fallback-followup)=
## Bland fallback lifecycle follow-up

This follow-up is for readers assessing the cost of inactive heuristics.
It assumes the [fallback lifecycle and rollback proof](../implementation/soi.md#bland-fallback-lifecycle).
The preceding mechanism study remains evidence for its archived `9a1efcf`
binary; this experiment isolates the later lifecycle change in `8faa775`.
Read the {ref}`overall comparison <lra-simplex-overall-current>`
for the earlier policy ranking.

The candidate binary SHA-256 is
`18b3182d9f95698dc7684ade226cbdaf091e691c648640971ef20b34d5274bd0`.
The old/new registry retains `9a1efcf` as an immutable control. Within the
candidate, the eager arm adds `--no-lra-soi-suspend-on-fallback`; the suspended
arm adds `--lra-soi-suspend-on-fallback`. All selector, pricing, cache,
maintenance and minimizer arguments otherwise match. Historical SOI and the
combined 64/8 profile are separate arms. No SOI parameter was retuned.

The {soifallbackworkflow}`frozen 16-configuration pilot plan <benchmarks/qf-lra-simplex-ablation/soi-fallback-pilot-plan.json>`
uses 100 inputs, three repetitions, a 15-second process limit,
Gaussian elimination on and fixed elimination both ways. Each input's versions
and repetitions share one SLURM CPU allocation. The
[pilot report](../coverage/soi-fallback-pilot-20261102.json) records machine
identities, family summaries, outcomes and parent-bootstrap comparisons.
Negative mean score changes favor suspension; uncertainty resamples the 85
original parents, using 2,000 bootstrap draws with seed 20261102.

| Fixed elimination | SOI profile | Eager stable | Suspended stable | Eager PAR2 (s) | Suspended PAR2 (s) |
| --- | --- | ---: | ---: | ---: | ---: |
| Off | Historical | 76 | 77 | 842.334 | 811.695 |
| Off | Combined | 88 | 89 | 497.011 | 455.866 |
| On | Historical | 83 | 83 | 693.043 | 687.970 |
| On | Combined | 90 | 91 | 414.508 | 385.635 |

No stable solve is lost in these four paired comparisons. Each paired 95%
interval for mean per-input PAR2 change excludes zero in the improving
direction. Against the archived binary, production fixed-off historical SOI
improves from 841.397 to 811.695 seconds and combined SOI from 496.084 to
455.866 seconds, again with no stable losses. These are pilot results, not
full-corpus acceptance.

The pilot therefore permits the predeclared full run. Its
{soifallbackworkflow}`24-configuration full plan <benchmarks/qf-lra-simplex-ablation/soi-fallback-plan.json>` adds
paired old/new Bland and Dantzig controls to the 16 pilot configurations,
for 24 full configurations. This addition was committed before inspecting the
pilot summary or executing the full run: the common Bland leaving scan also
stops computing unused violation magnitudes. Sparse controls were already in
the pilot. All 1,000 inputs, three repetitions, the 15-second limit and both
fixed-elimination settings remain unchanged. Model validation and phase
profiling run separately from process timing.

Qualification before this full run includes 45 C++ suites, 275 Python tests,
45 ASAN suites and 62 independently refuted selected-premise conflict fixtures.
Fresh 3,000-case QF_LRA FuzzSMT campaigns produce 2,877 PASS/123 timeout cases
for historical SOI and 2,956 PASS/44 timeout cases for combined SOI, with no
hard errors. These timeouts do not count as passes. Real-seed mutation
classification and full-corpus native/Dolmen replay remain separate gates.

A standard GDB sampler on the preselected objective-heavy input
`fa718a03e97b909188f63f2803428b9f89ec4f7f1dc3b2d5b786ce31f42a79b7`
provides supporting diagnostic evidence. Of 210 old samples, 71 include
`set_soi_direction`; of 139 candidate samples, six include it. Inclusive
`check` samples fall from 88 to 23, while substantial solved-constant
preprocessing remains (94 versus 95 inclusive samples). Sample counts overlap
and GDB perturbs execution; its 12.152/7.851-second wall times are not benchmark
speedups. The process table and separate counters are the performance evidence.
The run log retains commands, binary hashes, seeds, jobs and artifact paths.


(soi-fallback-full-results)=
### Full lifecycle comparison

All 1,000 timing shards completed: 72,000 process runs across 24 configurations.
They contain 46,746 SAT, 22,353 UNSAT and 2,901 timeout results, with zero solver
errors, unsupported results, normal unknown responses or answer disagreements.
The unresolved-label input times out in all 72 runs; it contributes the same
30-second mean-repeat PAR2 penalty to every configuration. The
[full process report](../coverage/soi-fallback-results-20261102.json) retains
family summaries, stable-runtime distributions and loss identities.

| SOI profile | Fixed elimination | Archived: stable / PAR2 s | Eager: stable / PAR2 s | Suspended: stable / PAR2 s |
| --- | --- | ---: | ---: | ---: |
| Historical | Off | 913 / 4315.720 | 912 / 4330.439 | 926 / 3887.574 |
| Combined | Off | 948 / 2444.629 | 948 / 2455.251 | 958 / 2012.218 |
| Historical | On | 913 / 3878.995 | 912 / 3891.745 | 913 / 3822.117 |
| Combined | On | 978 / 1396.639 | 978 / 1401.538 | 982 / 1188.174 |

Archived means the immutable `9a1efcf` binary. Eager and suspended use the same
`8faa775` binary, isolating lifecycle behavior from other binary changes.
Suspension loses no stable solves against either corresponding control in
either fixed-elimination setting. In production fixed-off, historical SOI gains
13 stable solves against the archive and lowers process PAR2 by 9.9%; combined
SOI gains ten and lowers it by 17.7%. Against same-binary eager controls, the
respective gains are 14 and ten, with PAR2 reductions of 10.2% and 18.0%.

| Fixed elimination | SOI profile | Mean PAR2 change vs eager (s/input) | Parent-bootstrap 95% interval |
| --- | --- | ---: | --- |
| Off | Historical | -0.443 | [-0.671, -0.243] |
| Off | Combined | -0.443 | [-0.646, -0.255] |
| On | Historical | -0.070 | [-0.113, -0.034] |
| On | Combined | -0.213 | [-0.325, -0.121] |

These are mean per-input score changes, not intervals for total PAR2 or
percent speedup. Negative favors suspension. Resampling uses 460 original
parents, 2,000 draws and seed 20261102. Production comparisons to the archive
also exclude zero: historical [-0.659, -0.230], combined [-0.627, -0.249]
seconds per input. Fixed-on archived comparisons retain all stable solves;
historical remains at 913 and combined rises from 978 to 982.

Ordinary old/new controls keep identical stable counts. Their total PAR2 scores
differ by at most 0.35%, separating the substantial SOI lifecycle effect from
small binary-wide shifts. This is a fresh paired comparison of Bland, Dantzig,
Sparse and SOI; the {ref}`overall table <lra-simplex-overall-current>` uses its
current rows. Dantzig still solves more inputs than combined SOI (968 vs 958
with fixed off), and Sparse retains 996. Removing inactive work does not imply
that the remaining SOI prefix chooses better bases or costs less than those
policies on this corpus.

Historical SOI's 13 production gains comprise five clock-synchronization and
eight tropical-matrix inputs. The compact report gives all 12 family summaries.
Combined SOI still loses `fdb4e04e83e390b2edfd2cd5bf9dbe257a35c8a59fba261b6bcdd75a938012fc`
from the historical profile's stable set. With fixed elimination on it also
loses `ee2ee1f93bcfb44b80ab1123b5e1e19ed177ca5cc766fb64b3e88367994eb343`.
It therefore remains experimental under the zero-loss selector-replacement
rule. The lifecycle is accepted as the default within opt-in SOI: historical
selection is retained, while its Bland suffix suspends unused SOI bookkeeping.
The global solver default remains Sparse.


### Separate model qualification and remaining limits

All 46,746 successful SAT timing rows passed native model checking and direct
Dolmen 0.10 validation with the exact binary, profile and input that produced
the row. There are zero invalid models, changed answers, solver/validator
timeouts, unsupported validator inputs or missing selected rows. Validation
allowed Primo 90 seconds and Dolmen 30 seconds per selected row; both budgets
are separate from the 15-second speed limit and excluded from process PAR2.
The [joined qualification report](../coverage/soi-fallback-qualification-20261102.json)
records per-profile coverage and validator identities. All 24,000 diagnostic
scheduled diagnostic processes have records; timeout prefixes remain censored.

The fresh retained mutation gate completed with 52 exact agreements (16 SAT,
36 UNSAT) and 428 explicitly witnessed invalid generated nonlinear inputs.
All 480 distinct input hashes and process classifications are in the
[mutation report](../coverage/soi-fallback-mutations-20261102.json). Invalid
generation is not a passing solver case. No wrong answer, invalid model, crash
or untriaged disagreement remains in this qualification.

A saved-set comparison to the original `d4944eb` audit baseline also retains
every historical stable solve: the new historical arm gains 11 fixed-off
solves and has the same fixed-on stable set. This is a cross-study set check,
not a paired speed estimate. The combined arm still loses the same one/two
inputs against that archive, so its experimental status is unchanged.

(soi-fallback-diagnostic-work)=
### Lifecycle diagnostic work

The separate one-repetition profiles have **883 inputs completing every
configuration**. The following sums use only that matched population with
fixed elimination off. Published phase counters are separate from the
uninstrumented process scores.

| Profile | Check time (s) | Pivots | Prefix pivots | Bland pivots |
| --- | ---: | ---: | ---: | ---: |
| Historical eager | 1212.841 | 3,551,546 | 3,354,858 | 196,688 |
| Historical suspended | 1086.595 | 3,551,546 | 3,354,858 | 196,688 |
| Combined eager | 278.716 | 281,753 | 49,180 | 232,573 |
| Combined suspended | 166.078 | 281,753 | 49,180 | 232,573 |

| Profile | Incremental objective terms | Objective phase (s) | Selection phase (s) | Activity contributions |
| --- | ---: | ---: | ---: | ---: |
| Historical eager | 927,835,812 | 94.786 | 754.257 | 0 |
| Historical suspended | 25,521,472 | 2.199 | 753.874 | 0 |
| Combined eager | 961,960,591 | 53.453 | 6.251 | 1,018,425,512 |
| Combined suspended | 25,550,937 | 1.379 | 6.280 | 45,034,803 |

The matched pivot counts are unchanged. Combined executes 232,573 Bland pivots
without individual assignment/pivot records, using 797 snapshots containing
2,021,191 stored row terms; reconstruction visits 160,473 ordinary row terms.
Historical uses 621 snapshots with 1,812,001 stored terms and 169,341 rebuild
term visits. These counters measure snapshot creation/reconstruction, not all
bytes or undo/destruction cost. Combined activity-maintenance contributions
fall from 1,018,425,512 to 45,034,803; the cache is rebuilt at suffix exit.

Dantzig uses 111,315 pivots and 34.950 seconds of check time on these same
inputs; Sparse uses 171,823 pivots and 9.448 seconds. The remaining SOI repair
trajectory is more expensive on this population. Historical selection alone
still consumes 753.874 seconds. Combined has a much smaller prefix, but its
total pivot count remains about 2.53 times Dantzig's. Pivot counts do not
measure row sizes or exact-arithmetic bit cost, and the phase clocks do not
fully separate every ordinary repair cost. The results support eliminating
inactive bookkeeping; they do not imply that SOI dominates simpler policies.

Assertion-time work stays visible. Combined with fixed elimination on has
160,524 check pivots **plus 156,666 assertion elimination pivots**, and check
time 130.400 seconds. Fixed off has 281,753 check pivots and 166.078 seconds.
The process table includes both assertion and check costs. Objective clocks include assertion-time updates, snapshot creation and
boundary reconstruction; they are not an additive partition of check or
process time. They exclude snapshot
release/undo and generic pivot work. Missing or unsupported counters must not
be interpreted as a measured zero; the joined report exposes field coverage.

All full timing/model/diagnostic/summary jobs (`336029`, `336030`, `336031`,
`336032`, `336033`) completed. Independent diagnostic aggregation `339267`
agrees exactly with the joined diagnostic result. Timing used one physical CPU
(two SMT siblings in the affinity mask) and 8 GiB per input task on CIIRC
`node-02` through `node-09`, all Intel Xeon E5-2690 v4 at 2.60 GHz. The corpus,
clock scope, binary hashes, limits, commands and machine counts are retained
in the run log and compact reports. The command/profile flags remain those
frozen before the full run. The benchmark still consists of cold conjunctions;
it does not qualify warm whole-parent SMT performance, snapshot rollback cost
or a different elimination/default profile.
