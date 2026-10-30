# QF_LRA call distribution and conjunction extraction

**Status: Experimental observer and exporter; qualification and the first pilot
are pending.** Work is isolated on `experiment/qf-lra-call-ablation`. The first
question is how completed numerical feasibility calls distribute across pivot
counts and elapsed time. Choosing a pivot cutoff and a full ablation matrix
comes after this observation, not before it.

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
{source}`the options <src/smt/check_sat.hpp>`,
{source}`the CLI <src/main.cpp>` and
{source}`the online callback <src/smt/solver_engines.cpp>`.
The new diagnostic helper is `LraCallProfiler` in `src/smt/lra_call_profiler.*`;
its revision-pinned source link will be added after the preparation commit.
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
with its disequalities. A feasible exported conjunction can come from an
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
crashes or untriaged reference disagreements. No pivot cutoff, algorithmic
winner, default change or performance improvement is claimed by the first probe.

## Sources and implementation status

Dutertre–de Moura's simplex report supplies the row-equation, bound, pivot and
strict-arithmetic invariants {cite:p}`dutertre2006simplexreport`. The existing
[LRA chapter](../implementation/lra.md) explains those implemented mechanisms.
The timing/export procedure here is an original diagnostic design, not a new
paper-backed solving algorithm. It never optimizes or changes a theory clause.
Current tests check unchanged verdict/search counters with profiling enabled,
negative-rational/strict serialization, same-Primo round trips for feasible and
infeasible exports, and export-budget handling. Independent reference answers
and SAT-model validation belong to the pending SLURM qualification/replay stages. Qualifying observations and
fresh replay difficulty remain empirical questions.
