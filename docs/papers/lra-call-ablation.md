# QF_LRA call distribution and conjunction extraction

**Status: The first source-input distribution and a 33-input conjunction pilot
are qualified.** Work is isolated on `experiment/qf-lra-call-ablation`.
This note defines the measured call, records its pivot/time distribution, and
provides frozen inputs for the next ablation stage.

## What constitutes a call?

The dedicated rebuilt QF_LRA online path retains a tableau across the SAT trail.
At a quiescence point, `OnlineLraPropagator::run_deferred_check_if_needed` calls
`LraTableau::check()` to repair the assignment or find a numerical conflict.
The observer measures that episode, not the entire SMT input, not every bound
assertion, and not a solver rebuilt from scratch on the current conjunction.

For each completed episode, record checker/call IDs, the pivot-counter delta,
steady-clock nanoseconds, row-update and row-term-operation deltas, and whether
`check()` returned a conflict. These are **warm** measurements: basis,
assignments, elimination state and previous assertions can influence the work.
The timer surrounds only `check()`; logging and export happen afterwards.
Clock resolution matters for very short zero-pivot calls, and the surrounding
observer can still perturb caches and which calls a wall-limited run reaches.
Instrumented whole-input times are not a performance comparison.

Immediate contradictory-bound rejections, assertion-time elimination pivots,
propagation scans, equality probes, final model reconstruction, mixed-theory
components and retained assumptions engines are outside this first observer.
A source may also finish during preprocessing. Such inputs remain in the
source-input denominator with no recorded calls, rather than disappearing.

## Ownership, lifetime and trace completeness

`CheckSatOptions` holds a shared pointer to `LraCallProfiler`, whose single
output stream and export budget span sequential queries in one process.
The dedicated online propagator registers its base tableau and owns the returned
checker descriptor. It passes const tableau queries to the recorder; no
assertion, pivot, term or SAT literal is created by the observer. Other online
propagator constructors use the null default and remain uninstrumented.

The JSONL stream has `begin`, `checker`, completed `check`, and `end` records.
An end record counts all registered checkers, completed calls and exported files.
Normal completion flushes the stream. An end marker alone is insufficient:
require command success, no diagnostic errors, and agreement between checker/
call events, their sequence numbers and terminal counts. Error unwinding may
close a stream after an export failed; such count mismatches are invalid evidence. During search, flush after every 1024
completed calls; a killed process may omit an unfinished call and buffered tail.
Treat a missing end or partial final line as a **censored prefix**. Its observed
maximum pivot count is not a bound on the uncompleted call, and a timeout is
not a completed long-duration observation.

The implementation owners are
{callsource}`the options <src/smt/check_sat.hpp>`,
{callsource}`the CLI <src/main.cpp>` and
{callsource}`the online callback <src/smt/solver_engines.cpp>`.
The {callsource}`diagnostic helper LraCallProfiler <src/smt/lra_call_profiler.cpp>`
owns serialization and export budgets.
The caller retains shared ownership until all callbacks have been destroyed.

## Exported numerical conjunction

A requested export combines two pieces:

1. The base tableau's defining row equalities over fresh Real variables `v0`,
   `v1`, and so on, captured at checker construction.
2. Each currently installed lower and upper bound, read after the completed
   check. Pivot repair changes the basis and assignment, not these bounds.

Every assertion is an equality or inequality literal. The file has one
`check-sat` and no propositional branching, `ite`, quantifier or uninterpreted
function. The variables represent the numerical tableau coordinates, including
auxiliary variables; original source names and declaration order are not kept.
Row transformations preserve the initial linear system, so its defining
conjunction together with the installed bounds describes the numerical
feasibility problem checked by that episode.

Strict bounds use their delta-rational sign: an upper $q+d\delta$ with $d<0$
is printed as $v<q$, a lower bound with $d>0$ as $v>q$, and zero coefficients
produce non-strict bounds. Rational coefficients and thresholds remain exact,
including negative fractions. Unexpected outward infinitesimal directions or
a changed variable count cause an explicitly recorded export skip. They do not
change the solver result. The ordinary Real bound assertions use inward
infinitesimals; this is not an arbitrary delta-arithmetic serialization API.

Exports contain the strongest installed bounds, not every original trail
literal. Replaced weaker bounds are numerically redundant. Disequality splitting
and the source Boolean context are not reconstructed: this is a corpus of
**convex numerical feasibility calls**, not a claim that the original SMT
assertions are a conjunction or that every SAT assignment was already feasible
with its disequalities. Bound-reason graphs and original explanation literals
are also omitted; studying explanation minimization on the original propagation
graph would require additional state capture. A feasible exported conjunction can come from an
ultimately UNSAT parent input. Its expected status comes from the numerical
check, not the parent's `:status`.

The optional exporter keeps the first qualifying calls up to a per-process
budget; it makes no diversity or representativeness claim. Identical files can
be deduplicated by content hash offline while retaining all occurrence metadata.
Original extraction order and the number omitted by a budget must remain clear.

## First experiment and later ablation

Start with the existing 237-input QF_LRA stratified development manifest, fixed
before measurement. Use one native profile with ordinary production defaults
and a bounded per-input wall limit; record input hashes, native binary/source,
profile completeness and parent-input outcome. The same calls need both pivot
and time measurements, including zero-pivot calls. Report per-input and family
coverage as well as call-weighted histograms, quantiles and joint pivot/time
bins; a single input with many cheap calls must not masquerade as broad coverage.

Use the observed distribution to choose a second, bounded extraction pass.
Validate exported syntax as literal conjunctions, compare every exported
expected answer with a reference, and independently validate all successful SAT
replays. Keep validation outside timings. Replaying SMT-LIB files rebuilds the
state and can add parsing, preparation and representation costs; compare fresh
replay pivots and times separately from warm-call measurements. Exported files
are inputs for a future ablation, not warm tableau checkpoints.

Solver builds, qualification, fuzzing, profiling and corpus replays run on SLURM.
Record source/binary/input identities, exact flags, seeds, limits and jobs in the
run log before citing results. Stop on incorrect exports, invalid models,
crashes or untriaged reference disagreements. No final corpus cutoff, algorithmic
winner, default change or performance improvement is claimed by the first probe.

## Pilot results: 237 source inputs

The fixed, seed-42 stratified development sample covers 17 families, with at most
15 inputs per family. Native source is `97a3229`, using production defaults (sparse pivot selection with its Bland fallback,
Gaussian elimination enabled) plus `--quiet --stats --lra-call-profile FILE`; Z3 is an answer reference. Both receive
five seconds per source input, with a two-second termination grace. Eight SLURM
tasks each use one physical CPU and 8 GiB on node-09. These are instrumented
observations, not a solver speed comparison. Exact hashes, versions, job IDs and
commands are in the [run log](../run-log.json).

Primo finished 171 inputs (90 SAT, 81 UNSAT), with 66 timeouts and no errors or
unsupported results. Z3 finished 159, with 78 timeouts. The 158 jointly decisive
answers agree; no observed answer contradicts declared status. Every one of the
90 successful native SAT inputs passed a separate native `--check-model` replay
and direct Dolmen validation, outside the timing stage.

There are **4,078,921 observed completed numerical checks**, from 194 inputs.
The remaining 43 inputs stay in the denominator with zero observed calls.
Of the observations, 1,866,111 calls come from completed source inputs and
2,212,810 from timeout prefixes. The latter omit unfinished calls and possibly
buffered tails; they are not complete distributions for those 66 inputs.
Total recorded check time is 217.868 seconds. Call-weighted pivot quantiles are
**median 0, p90 1, p99 6**, with an observed maximum of **4,450**.

| Pivots per warm check | Observed calls | Total check time (s) | Mean check time (ms) |
| --- | ---: | ---: | ---: |
| 0 | 3,658,626 | 0.604 | 0.000165 |
| 1 | 212,437 | 26.723 | 0.126 |
| 2–7 | 181,168 | 77.852 | 0.430 |
| 8–31 | 24,873 | 61.790 | 2.484 |
| 32–127 | 1,428 | 20.186 | 14.136 |
| 128–1023 | 358 | 15.489 | 43.266 |
| 1024 and above | 31 | 15.225 | 491.121 |

![Warm numerical-check pivot counts, time by pivot bin and joint timing density](../_static/lra-call-distribution-20261030.png)

The left panel separates completed sources from censored prefixes. The middle
panel sums only recorded `check()` time. The right panel groups durations into
powers-of-two nanosecond buckets; color shows `log10(calls + 1)`. The mean of
zero-pivot timings is close to timer overhead and is not a calibrated cost of a
mathematical operation. No per-input medians are averaged into these means.
The [portable data](../coverage/lra-call-distribution-20261030.json) retain
per-input hashes, outcomes, checker dimensions, pivot histograms, timing bins,
call maxima and family summaries. Raw durable-prefix traces remain in the
SLURM artifact directory recorded in the ledger.

A **128-pivot** first extraction stratum contains 389 observed calls across
36 source inputs in seven families: only 0.00954% of calls, but 14.10% of
recorded check time. This is a useful way to find difficult episodes, not a
sufficient definition of the final ablation corpus. Calls below 128 pivots still
account for 85.90% of recorded time; row size, rational arithmetic and operation
counts can make lower-pivot calls costly. Most recorded time (176.827 seconds)
comes from timeout prefixes, so longer runs may change the tail substantially.

The bounded second pass selected up to two parents per qualifying family, ordered
by descending observed maximum pivots and then relative path: 13 parents.
Its committed manifest is
{callsource}`benchmarks/qf-lra-call-extraction-pilot.txt`.
It allowed 15 seconds per solver per parent and exported the first five checks
with at least 128 pivots per process. This deliberately biased pilot tests
extraction and fresh replay; it is not yet the ablation benchmark selection.

Qualification for the native observer includes 44 release and 44 ASan CTest
groups, 242 Python tests, 12 documentation checks, and 1,000 generated
Boolean/arithmetic cases compared with Z3 and checked with Dolmen. The whole-tree
133-translation-unit analyzer scan found no new analyzer warning class; the
advisory include scan is triaged separately in the ledger. These checks qualify
the diagnostic path; they do not establish an algorithmic performance improvement.

## Frozen conjunction pilot and fresh replay

The corrected `791a179` exporter produced **33 distinct conjunctions** from
those 13 parents: **13 SAT and 20 UNSAT**, with no export skips. Across the
observed source traces, 99 checks qualified for export; 33 were saved and
66 were omitted by the per-process budget. Further qualifying calls in an
uncompleted search remain unknown. The source input/call IDs, hashes and warm
measurements remain attached to each file.

The committed inputs and baseline subsets are:

- {callbench}`All 33 conjunctions <benchmarks/qf-lra-call-pilot/manifest.txt>`.
- {callbench}`23 fresh checks with at least 128 pivots <benchmarks/qf-lra-call-pilot/fresh-ge128.txt>`:
  10 SAT and 13 UNSAT.
- {callbench}`10 lower-pivot controls <benchmarks/qf-lra-call-pilot/fresh-lt128-controls.txt>`.
- {callbench}`Per-input provenance and measurements <benchmarks/qf-lra-call-pilot/metadata.json>` and
  {callbench}`original export occurrences <benchmarks/qf-lra-call-pilot/occurrences.json>`.

Every file consists solely of Real declarations, linear equality/inequality
assertions, status metadata and one `check-sat`. The assertions together form
one conjunction. The generated-format audit checks linearity and concrete
coefficient syntax. All 33 native fresh replays returned the expected answers;
**every replay performed exactly one completed numerical check**. The native
binary, default options and one-CPU/8-GiB node-09 allocation match the corrected
extraction; the fresh per-solver limit was 15 seconds.

| Fresh-check metric | Minimum | Median | p90 | Maximum |
| --- | ---: | ---: | ---: | ---: |
| Pivots | 30 | 333 | 946 | 3,682 |
| Check time (ms) | 0.062 | 16.778 | 242.227 | 5,566.897 |

The 33 checks sum to 17,713 pivots and 7.259 seconds inside `check()`;
whole native processes sum to 11.126 seconds. These are different timing
boundaries. One fresh check activated the default sparse policy's Bland
fallback: the 3,682-pivot, 5.567-second maximum includes that fallback work.
It is identified by `bland_fallbacks` in the per-input metadata.

Z3 agreed on **all 20 UNSAT and 11 SAT** cases; its other two SAT attempts
timed out. All **13 SAT** cases passed separate native `--check-model` replays
and direct Dolmen checks under their unchanged QF_LRA annotation. Thus the two
reference timeouts have independent passing model checks; they are not counted
as Z3 agreements. All five successful SAT parents in the corrected extraction
also passed independent model checks. No wrong answer, invalid model or missing
SAT validation remains in this pilot.

Fresh reconstruction changed the pivot stratum: all saved warm calls used at
least 128 pivots, but only 23 fresh checks did. A pivot cutoff belongs to a
specified baseline and state, not inherently to the logical conjunction.
Freeze the manifest and content hashes **before comparing ablation variants**;
reselecting by each variant's own pivot count would change the benchmark set.
Keep lower-pivot controls and broader source coverage when designing the full
study: this pilot is biased toward early qualifying calls of parents with high
observed maxima.

The [fresh report](../coverage/lra-call-fresh-20261030.json) retains numerical
call measurements and source-independent replay outcomes.
{callbench}`scripts/qualify_lra_call_corpus.py` checks every file's hash,
joins every successful SAT row to its direct-Dolmen result, verifies the exact
command-adapted assertion text, and writes the two baseline subsets.
The [submission record](../coverage/lra-call-jobs-20261030.json) contains the
historical argument vectors with workspace/CMake placeholders; reruns require
fresh dependency job IDs and the repository's read-only preflight before each
submission. Use {callsource}`scripts/slurm_lra_call_ablation.sbatch` to collect
these files on SLURM, setting the corpus root and manifest through its environment
parameters. The plotting dependency is optional for collection and aggregation.

Qualification of the repaired native snapshot passed 44 release and 44 ASan
CTest groups, 246 Python tests, 12 documentation checks and a fresh 1,000-case
Boolean/arithmetic gate. The 133-translation-unit analyzer scan matched every
current C++ source hash, introduced no warnings, and confirmed removal of the
ten new direct-include advisories. The ledger distinguishes existing analyzer
advisories, the rejected initial export attempt and these accepted results.

### Export syntax repair found during validation

The initial `97a3229` exporter wrote a negative fraction as `(- (/ n d))`.
This has the intended Real value: Primo replayed all 33 initial exports with
the expected answers, and Z3 agreed on 31 with two reference timeouts. However,
11 of the 13 SAT exports failed Dolmen's
input-conformance check before model validation: a multiplication coefficient
must follow the [SMT-LIB QF_LRA concrete-coefficient grammar](https://smt-lib.org/logics-all.shtml#QF_LRA).
The conforming form is `(/ (- n) d)`, with a nonzero positive numeral denominator.
The exporter now uses that form, and the generated-format audit checks concrete
multiplication coefficients explicitly. The regression includes a negative
fractional row coefficient as well as a negative fractional bound.

The original traces, files, fresh runs and rejected validator outputs remain as
rejected extraction evidence. Fresh qualification, extraction and replay of the
repaired exporter passed as recorded above. The accepted first source-input distribution uses
recording without export, so this serialization repair does not alter that data.

## Sources and implementation status

Code links use the repaired exporter's `791a179` snapshot. The first source
pilot above retains its original `97a3229` binary identity and measurements.
Offline aggregation is standard-library Python; the optional plot renderer uses
Matplotlib 3.11.2 in the documentation environment. No plot dependency is needed
on SLURM. The collection, analysis, summary and restricted-format audit live in
{callsource}`scripts/run_lra_call_distribution.py`,
{callsource}`scripts/analyze_lra_call_profile.py`,
{callsource}`scripts/summarize_lra_call_distribution.py` and
{callsource}`scripts/prepare_lra_call_replay.py`.


Dutertre–de Moura's simplex report supplies the row-equation, bound, pivot and
strict-arithmetic invariants {cite:p}`dutertre2006simplexreport`. The existing
[LRA chapter](../implementation/lra.md) explains those implemented mechanisms.
The timing/export procedure here is an original diagnostic design, not a new
paper-backed solving algorithm. It never optimizes or changes a theory clause.
Current tests check unchanged verdict/search counters with profiling enabled,
negative-rational/strict serialization, same-Primo round trips for feasible and
infeasible exports, and export-budget handling. Independent reference answers
and SAT-model validation for the first source pilot passed as recorded above.
The corrected export corpus and its independent checks are recorded above;
the full ablation matrix remains a later study.
