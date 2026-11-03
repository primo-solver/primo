# Audit of Primo's SOI simplex

Reader: solver implementers investigating the historical SOI cost. This page
answers what the dated binary implemented and which evidence justified repair.
Prerequisites: [LRA operations](../implementation/lra.md) and the
[SOI algorithm](../implementation/soi.md). Read the
[follow-up experiments](soi-experiments.md) next; the findings below describe
the preserved baseline, with resolutions recorded at the end.

Primo implements the sum-of-infeasibilities objective, breakpoint integration
and row-combination conflicts from King, Barrett and Dutertre, but its SOI
profile differs substantially from the implementation evaluated in their
paper. Exhaustive candidate selection is the largest check cost in the five
complete SOI/g1/f0 diagnostic profiles. Some inputs also take thousands of pivots
before reaching the finite Bland fallback. The present evidence supports
auditing and improving this implementation before drawing conclusions about
the competitiveness of the published method.

This audit examines native source revision `d4944eb` (unchanged under `src/`
at the audit baseline `ba6fd9f`). It changes documentation and diagnostic
workflows, with no solver or default changes. The
{ref}`algorithm chapter <lra-soi>` explains ordinary use;
the {ref}`1,000-input ablation <lra-simplex-factorial-results>`
provides the larger performance comparison. This page answers three audit
questions: what agrees with the sources, why the implemented profile is
expensive, and what evidence is still missing.

## Primary sources and implementation status

The algorithmic sources are {cite}`King-fmcad2013`, §V-A–D, Figures 5–6,
and {cite}`king2014`, §§3.2–3.3.5, Figures 3.3–3.6. Their experimental
comparisons are in §VI and §3.4 respectively. The audit read the
[primary project mirror of the FMCAD paper](https://cvc4.github.io/publications/2013/KBD13.pdf)
and the [NYU thesis](https://cs.nyu.edu/media/publications/king_tim.pdf).
Paper checksums are retained in the {soiaudit}`run log <docs/run-log.json>`, entry
`2026-11-02-soi-paper-audit`. No implementation from another solver was used.

The abstract selector in Figure 5 examines all flexible candidates. Section
V-D and thesis §3.3.3 permit examining only heuristically many candidates and
stopping once a progressing candidate is found. They explicitly report that
**CVC4 prefers shorter columns and switches to Bland selection after progress
stalls**. Section V-D and thesis §3.3.4 also report CVC4's preference for
candidates exposing a row conflict. These distinctions matter when comparing
published performance with Primo's coefficient-based, exhaustive selector.

| Paper operation or obligation | Primo owner and implemented subset | Difference or remaining obligation |
| --- | --- | --- |
| Represent equations, feasible nonbasic values and violated basics | {callsource}`LraTableau <src/theory/arithmetic/lra_tableau.hpp>` owns sorted sparse rows, assignments, bounds, reverse occurrences and `violated_vars_`. | The artificial objective is a coefficient accumulator; its assignment needs the exception explained below. |
| Maintain $f=\sum_i d_i\tau_i$, with $d_i=-1,0,+1$ for lower violation, feasibility and upper violation | {callsource}`soi_direction and set_soi_direction <src/theory/arithmetic/lra_tableau.cpp#L440-L503>` update cached directions and the objective row. | Each changed direction saves a whole objective row, then concatenates and sorts its terms with a signed row. |
| Keep nonbasic values inside their bounds | {callsource}`assert_lower, assert_upper and update <src/theory/arithmetic/lra_tableau.cpp#L49-L141>` immediately repair a newly tightened nonbasic bound. | This supplies SOICHECK's initial nonbasic repair without a separate scan. |
| Integrate changes in violation between breakpoints | {callsource}`soi_select <src/theory/arithmetic/lra_tableau.cpp#L517-L691>` gathers forward bounds, sorts crossings and integrates rational slopes against delta-rational steps. | All flexible objective variables are examined; row coefficients are searched again at crossings. |
| Prefer the closest best step, then the smallest leaving ID | The selector compares $(\Delta V,\lvert h\rvert,k)$ inside each retained crossing group. | Truncating the sorted list at the entering variable's own bound can discard other events at the same endpoint. |
| Reduce zero-progress selection to Bland | {callsource}`check <src/theory/arithmetic/lra_tableau.cpp#L1718-L1813>` limits the SOI prefix, then uses ordinary Bland repair. | The entering selector keeps its coefficient preference on zero progress, unlike §V-C. Termination relies on the finite fallback, not that paper tie-break argument. |
| Check for existing single-row conflicts before and during SOI | Direct contradictory bounds are checked during assertion; ordinary row conflicts are checked after fallback. | Figure 5's preliminary `CheckAllConflicts`, thesis Figure 3.3's error-row checks and §V-D's constant-time conflict preference are absent from the SOI prefix. |
| Extract a conflict when a nonempty error set has no flexible objective variable | {callsource}`restricted_soi_row and build_soi_conflict <src/theory/arithmetic/lra_tableau.cpp#L702-L741>` combine signed rows and collect blocking and violated bound reasons. | Implemented; see the argument below. |
| Reduce the conflicting row set | {callsource}`quickxplain and deletion <src/theory/arithmetic/lra_tableau.cpp#L744-L886>` use the restricted-row conflict predicate. | The thesis Figure 3.6 coefficient-sign-driven growing loop is absent. Generic recursive reduction is sound under its invariant, but minimum size is not established. |
| Restore search state | {callsource}`pop and undo <src/theory/arithmetic/lra_tableau.cpp#L1896-L1965>` restore bounds, assignments, rows and directions, then rebuild occurrences and violations once. | SOI retains full trailing during the Bland suffix. Cheaper rollback would require a separate invariant and evaluation. |

## Correctness arguments checked

These are source-level arguments and a review of existing tests, supplemented
by unchanged-binary replays. They are not a machine-checked proof or a new
fuzz campaign.

### Objective, endpoints and delta rationals

Write $\tau_i=\sum_j a_{ij}x_j$ for basic variable $b_i$'s row and
$\beta_i$ for its current assignment. Its violation is

$$
V_i=\max(l_i-\beta_i,0)+\max(\beta_i-u_i,0),
$$

omitting terms whose bound does not exist. The nonbasic variables satisfy
their bounds. The local objective coefficients are
$c_j=\sum_i d_i a_{ij}$; the offsets involving bounds are constant within
one segment and are unnecessary for selecting a direction. A direction
change adds $(d_i'-d_i)\tau_i$ to the objective. Pivots substitute the same
equations into all affected rows, including the objective.

Writing $h_r$ for the step at the $r$th bound crossing and $m_r$ for the
slope until the next crossing, the selector integrates
$\Delta V_{r+1}=\Delta V_r+(h_{r+1}-h_r)m_r$ before changing the slope at
a crossing. All slopes are rational. Assignments, bound values, steps and
violation changes are delta rationals, ordered lexicographically; division is
only by nonzero rational row coefficients. This does not require numerical
epsilon selection or optimization to a numerical tolerance.

The endpoint correction already present in this revision is essential:
at a zero-length crossing, moving inward contributes no direction change,
moving outward contributes one, and a fixed variable's two zero events
contribute once. At a nonzero fixed crossing both events contribute.
Existing {callsource}`endpoint tests <tests/unit/lra_tableau_tests.cpp#L831-L954>`
exercise both signs, fixed bounds and purely infinitesimal moves. The old
zero-breakpoint defect is already repaired; it is not a new audit finding.

The paper's Lemma 1 supplies a forward breakpoint with non-increasing
violation for every flexible objective variable. Primo uses that fact when
treating an absent selection as an empty flexible set: it needs exact
coefficients, correct endpoint slopes and a complete forward event list.
Discarding events strictly beyond the entering variable's own bound preserves
feasibility. Discarding *equal* endpoint events preserves the candidate value
but can change the smallest leaving ID; this is a selection-conformance gap,
not evidence of a wrong arithmetic answer.

`check()` pivots first and updates the now-nonbasic leaving variable, whereas
the paper describes updating the entering variable before the basis change.
For a leaving coefficient $a_{kj}\ne0$, Primo uses target
$\beta_k+h a_{kj}$. The solved entering row then changes $x_j$ by $h$ and
each other basic by its old entering coefficient times $h$. This gives the
same assignment and new basis in exact arithmetic. The intermediate state is
private to that pair of operations.

### Conflict extraction and minimization

For a nonempty set $S$ of currently violated basics, put
$f_S=\sum_{i\in S}d_i b_i=\sum_j c_jx_j$ and
$B_S=\sum_{i\in S}d_i v_i$, where $v_i$ is the bound violated by $b_i$.
At the current assignment,
$f_S(\beta)-B_S=\sum_{i\in S}V_i>0$.

If $f_S$ has no flexible nonbasic variable, every $c_j>0$ is blocked at
$x_j=l_j$ and every $c_j<0$ at $x_j=u_j$. These blocking bounds imply
$f_S(x)\ge f_S(\beta)>B_S$, while the selected basic bounds imply
$f_S(x)\le B_S$. This contradiction justifies collecting precisely those
blocking reasons and selected violated reasons. It works in delta rationals
as well as ordinary rationals. Single-row conflicts remain sufficient when
available; a general theory conflict does not require this global objective.

Let $P(S)$ mean that $S$ is nonempty and its restricted objective has no
flexible variable. This is a **certificate test**, not a satisfiability test.
It is not monotone. For example, take $x=0$, $x\ge0$, basic rows $b=-x$
and $c=2x$, and lower bounds $b\ge1$, $c\ge1$. Both basics are violated.
For $S=\{b\}$ the signed objective is $x$, blocked at its lower bound, so
$P(S)$ holds. Adding $c$ gives $-x$, which can decrease by increasing $x$,
so $P(\{b,c\})$ fails. The larger constraints are still inconsistent.

Nevertheless, Primo's recursive `quickxplain(B,C)` is sound with precondition
$P(B\cup C)$. It returns $D\subseteq C$ satisfying $P(B\cup D)$:

1. If the background already passes, returning the empty set preserves the
   precondition. With one candidate, returning that candidate preserves it.
2. Split $C=C_1\cup C_2$. The first recursive call returns $D_2$ with
   $P(B\cup C_1\cup D_2)$.
3. That is the second call's precondition; it returns $D_1$ with
   $P(B\cup D_2\cup D_1)$, exactly the caller's returned union.

The induction requires no monotonicity. It establishes soundness, not
inclusion-minimality or a minimum number of literals. Deletion likewise keeps
only subsets that independently pass $P$; one pass does not establish
minimality for this non-monotone predicate. The thesis's Figure 3.6 adds a
different row-growing mechanism. Primo should not claim that mechanism or
generic monotone-QuickXplain complexity guarantees for its present oracle.

### Lifetime, rollback and the artificial objective

The tableau owns its vectors and rows. IDs remain stable; references into
row and occurrence storage can be invalidated by mutations. The audit checked
the SOI update/pivot/direction-change/undo chain for these boundaries:

- `update()` snapshots the affected row IDs before direction changes mutate
  objective occurrences. The historical use-after-free at this site is fixed.
- `set_soi_direction()` consumes the borrowed source row into scratch storage
  before unlinking and rebuilding objective occurrences; it does not use that
  borrow afterwards.
- `pivot()` snapshots the old basic row before resetting its direction,
  restores the basis algebraically, and trails affected rows. `pop()` reverses
  that order before one deferred occurrence-index rebuild.
- The {callsource}`model builder <src/smt/solver_engines.cpp#L3636-L3654>` reads
  term-mapped variables, while concrete epsilon selection reads prepared atom
  variables in {callsource}`choose_concrete_delta <src/smt/solver_engines.cpp#L3555-L3588>`.
  The artificial objective has neither mapping.

There is an important representation exception: the stored assignment of the
artificial objective is **not maintained as the value of its current row**.
Direction changes replace its coefficients without recomputing that value.
For example, after setting $x=1$ in $s=x$, asserting $s\ge2$ adds $-x$ to
the objective row while its stored assignment can remain zero. Selection and
conflict extraction use coefficients and ordinary variable assignments,
not that artificial assignment. This does not demonstrate an unsound SAT or
UNSAT result, but global statements that every stored basic row always agrees
with its assignment need this exception. Future invariant checks should test
the objective coefficient identity separately from the original equations.

## Measured costs

The diagnostic job used the **same Release binary** as the completed
ablation: source `d4944eb`, SHA-256
`710929c58c18970cf4fccf7eb479fb95bb1410e6b46559433e4af80b55a9eee8`.
SLURM array **323130**, six tasks on `node-09`, allocated one physical CPU
and 8 GiB per task, with an eight-minute task limit. There were six selected
inputs and three configurations per input: sparse/g1/f0, SOI/g1/f0 and
SOI/g1/f1. Gaussian elimination was on; f1 enables fixed-variable elimination.

Each pair had one native replay (30-second limit) and one Callgrind 3.22.0
instruction profile (90 seconds plus a five-second termination grace).
All **18 native runs agreed with the frozen labels**. Fifteen profiles
completed and agreed too. Three profiles for case 0 reached their profiler
time limit before producing an answer and are retained as partial evidence;
they are excluded from selector-cost conclusions. A profiler timeout is not
a solver correctness failure or a native benchmark timeout.

| Case and family | Answer | Sparse check pivots | SOI check pivots | SOI fallback | Selector instructions / check instructions |
| --- | --- | ---: | ---: | --- | ---: |
| 1: LassoRanker, LeeJonesBen-Amram | SAT | 17 | 12,436 | yes | 80.1% |
| 2: UltimateInvariantSynthesis, functions1 | UNSAT | 70 | 73 | no | 54.2% |
| 3: tta_startup, 12 nodes | UNSAT | 31 | 1,773 | yes | 71.0% |
| 4: tropical-matrix, constraint-223827 | SAT | 26 | 4,713 | yes | 52.1% |
| 5: sc-34.induction | SAT | 206 | 203 | no | 77.0% |

This table compares g1/f0 profiles. Counts are inclusive Callgrind `Ir`,
not CPU-time samples. `soi_select()` is a child of `check()`; parent and child
counts must not be added. Library inlining, recursion attribution and missing
line-level debug information limit finer attribution. Whole-process counts
also include preprocessing, parsing and teardown. The five complete SOI/g1/f0
profiles establish that selection is expensive on those cases; they do not measure a
corpus-wide percentage or isolate the contribution of each loop inside it.

Two distinct problems appear. In case 5 the pivot counts are almost equal,
yet selection consumes 77% of SOI's check instructions. In cases 1, 3 and 4
the SOI path also performs far more pivots. The existing fallback counter
does not reveal how many prefix steps made zero progress, so these numbers
alone do not prove cycling or identify a particular degeneracy sequence.
Case 1's SOI tableau has 3,085 variables: the default threshold permits
12,405 selected SOI steps before fallback. Many variables therefore permit a
long expensive prefix even when Sparse requires only 17 check pivots.

Fixed elimination changes the path considerably: cases 1, 3 and 4 have SOI
check pivot counts 8, 0 and 11 under f1. That is not free work removal:
elimination pivots occur during bound assertion, outside the reported check
pivot count. Its effects on the full 1,000-input set are documented in the
[factorial results](lra-call-ablation.md).

The minimizer is not the dominant explanation for these five profiles.
SOI/g1/f0 calls it once in case 2 and zero times in the other four. This says
nothing about explanation quality or its cost on other workloads.

The [machine-readable audit](../coverage/soi-audit-20261102.json) retains
counts, counters, original driver and artifact hashes. The
{soiaudit}`plan <scripts/qualification/soi_audit/plan.json>`,
{soiaudit}`profiling driver <scripts/qualification/soi_audit/profile.py>` and
{soiaudit}`analysis driver <scripts/qualification/soi_audit/summarize.py>`
identify every input, selection criterion and validation check. Raw profiles, stdout/stderr and
metadata are retained at `results/soi-audit-20261102` in the qualified SLURM
workspace and the local audit results directory. The run-log entry records
commands, resource limits, test jobs and collection details.

(soi-audit-findings)=
## Findings and next experiments

| ID | Priority and finding | Evidence and next action |
| --- | --- | --- |
| SOI-1 | High performance: exhaustive expensive pricing | Selection takes 52–80% of check instructions in five selected complete profiles. Test paper §V-D's bounded, short-column pricing and stop-after-progress, with explicit stalled-progress behavior. |
| SOI-2 | High performance: expensive long prefixes | Three selected cases take 1,773–12,436 pivots versus Sparse's 17–31. Record selected steps, zero-progress steps, entering candidates, crossings and prefix/suffix costs before selecting a new fallback policy. |
| SOI-3 | Medium performance: full objective maintenance and trailing | Whole-row copies, concatenation/sorting and occurrence rebuilds occur at direction changes; full assignment/pivot trailing continues after fallback. This is source-proven overhead, not a separately measured fraction. Evaluate linear merges and cheaper rollback only with explicit invariants. |
| SOI-4 | Medium algorithm conformance: early conflicts missing | Add and ablate error-row conflict checks and paper §V-D's cached conflict preference. No evidence yet isolates the benefit from other changes. |
| SOI-5 | Medium termination conformance: entering zero-progress tie | Coefficient preference remains when the paper chooses the smallest entering ID. Current finite fallback supplies termination; paper-faithful ties should be tested separately from pricing. |
| SOI-6 | Low selection conformance: tied endpoint truncation | Sorting compares only step values, then truncates at the entering variable's event. Other coincident events can be lost. Retain the full equal-value group before applying the smallest-ID leaving rule; add a focused regression. |
| SOI-7 | High verification gap: reasons checked for membership, not inconsistency | The {callsource}`multi-row test <tests/unit/lra_tableau_tests.cpp#L1031-L1064>` and {callsource}`minimization-arms test <tests/unit/lra_tableau_tests.cpp#L2656-L2679>` accept reason subsets without independently refuting their conjunction. Reconstruct selected bounds with base equations and check the certificate with exact arithmetic or a reference solver, including strict bounds and non-monotone subsets. |
| SOI-8 | Medium representation/documentation gap: objective assignment exception | Coefficients, not the artificial assignment, drive SOI. State and test that distinction. The header's rollback rationale also calls SOI an extension beyond the FMCAD paper, although that paper is the SOI source; clarify that the difference is from the earlier feasibility-simplex rollback argument. |
| SOI-9 | Medium guarantee gap: minimization attribution | Generic recursive reduction preserves a conflict by induction, but does not implement thesis Figure 3.6 or establish inclusion-minimality. Strengthen tests for this exact non-monotone oracle before making minimality or generic QuickXplain complexity claims. |

The next controlled changes should address pricing and progress observability
first, then early conflicts, ties and objective maintenance in separate
ablations. Any behavior change needs fresh focused tests, independent conflict
checks, fuzzing and model validation on SLURM before another full comparison.
This audit accepts no new optimization and promotes no defaults.

## Verification scope

The earlier ablation independently validated all 31,248 successful SAT
model-producing replays, using native checking and Dolmen, and compared known
UNSAT answers with the frozen reference labels. That is historical evidence
for this exact native binary, not a fresh audit fuzz campaign and not an
independent check of every emitted theory lemma.

Fresh audit verification reruns the existing tableau and QF_LRA solver unit
executables on SLURM, including the endpoint, rollback, occurrence-index and
minimizer tests. Those executables explicitly enable assertions even in a
Release build. Their exact hashes, job status and outputs are retained in the
run log and diagnostic artifacts. No native implementation changed, so a new
full solver gate, ASAN rebuild or whole-tree clang-tidy/include-cleaner run was
not performed. The review covers the SOI source/caller/test chain above;
it does not certify unrelated theories or every client of the tableau.

## Follow-up resolution ledger

The dated findings and their original evidence above remain unchanged. The
following work addresses them in opt-in profiles at source `9a1efcf`; it does not
retroactively qualify the audited binary. The [experiments](soi-experiments.md)
record separate measurements and the acceptance decision.

| Finding | Implemented response | Evidence and remaining limit |
| --- | --- | --- |
| SOI-1 | Short columns, chunked pricing and stop-after-progress | Pilot froze 64/8; full ablations isolate pricing from the selector. |
| SOI-2 | Selected/zero steps, candidates, crossings, prefix/fallback pivots; stall fallback | Work counters and separate profile clocks expose the prefix. A finite overall guard remains. |
| SOI-3 | Linear objective merge, support-only incidence changes, coefficient reuse and convex pruning | Exact-selection and occurrence regressions. Full objective/assignment/pivot trailing is retained. |
| SOI-4 | Error-row scans before/after updates and cached breakpoint conflict preference | Slow recomputation checks cache updates/rollback; selected-premise fixtures independently refute conflicts. Row substitution refreshes counts in a separate pass. |
| SOI-5 | Paper entering score, smallest-ID zero-progress ties | Focused exact-selection regression; historical arm retained for comparison. |
| SOI-6 | Complete coincident groups and complete forward crossings | Endpoint/fixed-bound tests; own-bound outward slope included in paper arms. |
| SOI-7 | Reconstruct original equations and only selected premises for Z3 | Strict, propagated-premise, multi-row and non-monotone fixtures; not a universal online lemma checker. |
| SOI-8 | Separate coefficient identity from ordinary assignment invariants; corrected rollback comment | Objective identity tests through update/pivot/pop. Artificial assignment remains intentionally unused. |
| SOI-9 | Independent Figure 3.6 row-growing arm and explicit boundary decisions | All four minimizer arms independently refuted in fixtures. No inclusion-minimality or generic QuickXplain complexity claim. |


### Subsequent Bland lifecycle resolution

Source `8faa775` separately addresses the post-fallback portion of SOI-3.
{soifallbacksource}`The lifecycle helpers <src/theory/arithmetic/lra_tableau.cpp>`
save one owned pre-suffix state, detach the artificial objective and suspend
incremental objective/cache maintenance and individual suffix records. They
reconstruct derived SOI state before returning. Full prefix records and exact
rollback remain. This is a separate Primo design with an explicit
[rollback proof](../implementation/soi.md#bland-fallback-lifecycle), rather than
an extension of the ordinary bounds-only rollback argument to SOI.

Strict/nested/copy regressions, independently refuted explanations, fresh
generated cases and retained real-seed mutations qualify the mechanism's
behavior. Its {ref}`separate evaluation <soi-fallback-full-results>` records
archived, eager and suspended arms, boundary costs and completed model gates.
The dated audit and `9a1efcf` resolution table above retain their original
scope and evidence.

### Stall-response clarification (2026-11-03)

The SOI-1/2/5 responses above do not establish that every fallback choice
matches the source algorithm. Thesis §3.3.3, p. 95, and FMCAD §V-D explicitly
describe changing from short-column heuristics to Bland selection after stalls.
The numerical threshold is unspecified. Their surrounding SOI algorithm and
termination proof support selection within SOI; they do not explicitly describe
Primo's early handoff to ordinary Bland row repair. The
{ref}`source guide <soi-stalls-and-bland>` records the distinction, including
ordinary SMT simplex's own safeguards from thesis §2.2.12.

The current combined profile's threshold of eight and the ordinary-repair
handoff remain engineering choices. Its qualified results measure that hybrid;
they do not settle the performance of retaining SOI's objective and crossings
while using Bland selection after stalls. This is an open conformance and
evaluation question, not a newly observed wrong answer or a change to the
archived benchmark results.

### Observed self-update stalls (2026-11-03)

The {ref}`stall drill-down <soi-stall-drilldown>` observes eight repetitions of
the same zero-distance self-update in two selected fixed-off corpus examples.
These leave the assignment and basis unchanged; a third example instead
performs eight zero-distance pivots through distinct bases. Source `8faa775`
counts both patterns as zero progress and hands off after the configured streak.
The known self-event boundary in the literal Figure 3.4 enumeration therefore
has a demonstrated operational effect. The finite guard prevents indefinite
repetition but does not resolve that selection issue. No claim of corpus-wide
prevalence, a repaired selector or a new performance result follows from these
three diagnostic traces.

### Self-update boundary resolved (2026-11-03)

Source `7cb3743` excludes an own-bound event whose exact distance is zero
before breakpoint sorting. It retains basic-row events at zero distance, so selection
can exchange a basis rather than repeating an unchanged state. Nonzero
own-bound assignment steps also remain eligible. The
{ref}`implementation checklist and argument <soi-self-step-resolution>`
state why deleting this event preserves the crossing objective and a genuine
zero-distance candidate. This resolves the demonstrated self-update defect;
it does not resolve the separate ordinary-repair handoff question above.

The new counter `:lra.soi-self-zero-events-skipped` exposes the filter.
Mirrored rational and infinitesimal regressions check genuine degenerate
pivots, positive assignment-only steps, objective coefficients and rollback.
SLURM qualification passed 45 solver tests, 275 Python tests, 45 ASAN tests
and 62 independent Z3 explanation refutations. Fresh reference checking
classified 3,915 of 4,000 generated cases as passes and 85 as timeouts, with
no hard failures. The {ref}`paired pilot <soi-selfstep-pilot>` records its
separate model qualification and mixed performance findings. The historical
SOI selector, Sparse default and combined profile's 64/8 settings are unchanged.

The {ref}`qualified full comparison <soi-selfstep-full>` preserves both
combined stable solve sets and reduces handoffs on the matched profiling
population. Most pivots still occur in ordinary repair, and neither
parent-bootstrap interval establishes a process-time improvement. The filter
is accepted as a defect repair in the experimental selectors; the ordinary
Bland handoff remains an explicit deviation awaiting a separate experiment.
