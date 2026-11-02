# SOI paper and thesis guide

This guide is for readers implementing or reviewing an LRA theory solver. It
answers what each source section contributes, what it assumes, and where Primo
implements or departs from it. Prerequisites are the four
[LRA theory operations](../implementation/lra.md) and sparse tableau algebra.
Read the [SOI algorithm chapter](../implementation/soi.md) for concrete execution,
then the [audit](soi-implementation-audit.md) and
[experiments](soi-experiments.md) for evidence.

The primary sources are King, Barrett and Dutertre, *Simplex with Sum of
Infeasibilities for SMT*, FMCAD 2013 {cite}`King-fmcad2013`, §V-A–D and §VI,
and Tim King, *Effective Algorithms for the Satisfiability of Quantifier-Free
Formulas Over Linear Real and Integer Arithmetic*, NYU PhD thesis, 2014
{cite}`king2014`, §3.2, §§3.3.1–3.3.5 and §3.4. The
[project paper mirror](https://cvc4.github.io/publications/2013/KBD13.pdf) and
[NYU thesis](https://cs.nyu.edu/media/publications/king_tim.pdf) were read as
primary algorithm sources. Their recorded checksums are in `docs/run-log.json`.
No other solver's implementation supplies code or documentation text.

## Notation and the implementation boundary

Use $b_i=\sum_j a_{ij}x_j$ for a basic row, $\beta$ for the assignment,
$[l_i,u_i]$ for its possibly one-sided interval, and $E$ for violated basics.
Set $d_i=-1$ below a lower bound, $+1$ above an upper bound, and zero otherwise.
The thesis calls this multiplier $\lambda_i$. A coefficient $c_j>0$ is
**blocked** when $x_j=l_j$; a negative one is blocked at $u_j$. Otherwise that
coefficient is flexible for decreasing its linear function.

Primo's {soifinalsource}`LraTableau <src/theory/arithmetic/lra_tableau.hpp>` owns exact rational/infinitesimal assignments, sorted sparse
RHS rows, reverse occurrences, bound literals, violations and rollback state.
The thesis's tableau row $T_i$ includes the basic coefficient $-1$; its
effective row $\tau_i=T_i+e_i$ removes that coefficient for a basic row. Primo
stores the RHS, matching $\tau_i$. Counts stated in terms of $T_i$ therefore
need to account for the implicit basic diagonal. `smt::` owns the SAT interface and explanation expansion;
the tableau does not parse SMT-LIB. Sparse is the default repair policy. All
new SOI mechanisms discussed here are explicit options, not default Sparse work.

## Thesis §3.2: feasibility and violation

**Purpose and assumptions.** Turn a fixed equation system and asserted bounds
into a feasibility problem. Ordinary tableau equations hold at the assignment,
and nonbasic assignments satisfy their bounds before repair. Bounds can be
strict through symbolic infinitesimals. The set $E$ therefore contains only
basics after nonbasic bound repair.

**Operations and costs.** Define
$V_i=\max(l_i-\beta_i,0,\beta_i-u_i)$, omitting absent bounds, and
$V=\sum_i V_i$. A row has a conflict if its violated bound and the blocking
bounds of every signed RHS term disagree. Updating one nonbasic touches its
occurrences; checking one row scans its RHS unless cached counts apply. These
are sparse operation counts, not unit-cost claims for arbitrary-size rationals.

**Example and owner.** For $b=2x-y$, $\beta_x=\beta_y=1$, $b\ge3$, the
assignment has $b=1$, $V_b=2$, and $d_b=-1$.
{soifinalsource}`VarState <src/theory/arithmetic/lra_tableau.hpp>`, {soifinalsource}`update <src/theory/arithmetic/lra_tableau.cpp>`, bound assertion and {soifinalsource}`build_conflict <src/theory/arithmetic/lra_tableau.cpp>` implement this state
contract. Separate tests check ordinary row assignments and the SOI coefficient
identity. The artificial objective assignment is intentionally outside the
ordinary equation-assignment invariant.

## Figure 3.3: check and early conflicts

**Purpose and assumptions.** The check operation must return a feasible
assignment or an explained conflict. Start by repairing violated nonbasics,
then check error rows. A flexible objective permits a selected update; each
update is followed by conflict checks on affected error rows. A blocked
objective with remaining errors needs a multi-row explanation.

**Operations and costs.** Figure 3.3 connects selection, pivot/update and
explanation. Initial scans cost the total support of examined rows; subsequent
scans are restricted to affected error rows. Pivot work depends on occurrences
of the entering variable and fill-in, not merely the pivot count.

**Example and owner.** With $b=2x$, $x\le1$, $b\ge6$, choosing the breakpoint
$x=3$ makes $x$ basic; the new row $x=b/2$ immediately conflicts with the two
bounds. {soifinalsource}`LraTableau::check <src/theory/arithmetic/lra_tableau.cpp>` implements the before/after scans in the paper and
short-column arms. The historical arm is retained. All arms retain a finite
prefix guard followed by ordinary Bland repair, so this engineering profile
is not an unrestricted execution of Figure 3.3.

## Figure 3.4: exact entering and leaving rules

**Purpose and assumptions.** Choose an update using the global piecewise-linear
violation objective, allowing a newly basic variable to become violated. For
an eligible entering column $j$, enumerate forward crossings of row bounds
and its own bounds. The forward direction decreases its local coefficient.

**Operations and costs.** Minimize $(\Delta V,|h|,k)$ for leaving ID $k$ within
a column. Across columns minimize
$(\operatorname{sgn}(\Delta V)|c_j|,j)$. Negative progress therefore favors
large coefficients, while zero progress chooses the smallest entering ID.
Every event at a coincident distance participates before the leaving-ID tie.
Exhaustive pricing costs the sum of the column walks below.

**Example and owner.** Two zero-progress columns with coefficients of magnitudes
2 and 7 must tie on score zero; the smaller ID wins regardless of magnitude.
{soifinalsource}`SoiSelection <src/theory/arithmetic/lra_tableau.hpp>` and {soifinalsource}`soi_select <src/theory/arithmetic/lra_tableau.cpp>` implement the `paper` arm. The historical arm
keeps coefficient preference and its own-bound cap. Complete forward crossings
are necessary to expose the newly basic conflict in the preceding example;
this additional conformance gap was discovered during the cache design.

## FMCAD §V-A and thesis §3.3.1: computing objective change

**Purpose and assumptions.** Compute all relevant $\Delta V(j,h)$ without
re-evaluating every row at every breakpoint. Locally,
$f=\sum_{i\in E}d_i b_i=\sum_j c_jx_j$ is the linear part of $V$; the discarded
bound constants are valid only while violation directions stay unchanged.
The objective row must change whenever a direction changes.

**Operations and costs.** Sort $p$ crossing events, integrate the current slope
between successive distances, then update the slope for the whole coincident
group. This takes $O(p\log p)$ comparisons plus a linear integration traversal once crossing coefficients
are available. Historical Primo still searches row coefficients while walking
events, so this is not a bound on its entire selection cost. Exact arithmetic
cost also depends on operand size. Primo includes the entering variable's outward slope change beyond
its own bound. A direction change updates the objective by
$(d'_i-d_i)\operatorname{row}(i)$.

**Example and owner.** For $b=2x-y$, $3\le b\le7$, $x\le3$, $y\ge1$ at
$x=y=1$, increasing $x$ has crossings at steps 1, 2 and 3. Steps 1 and 2 both
remove the initial violation 2, so the distance rule selects 1 (paper Figure 6,
thesis Figure 3.5). {soifinalsource}`soi_select <src/theory/arithmetic/lra_tableau.cpp>` owns events and integration;
{soifinalsource}`set_soi_direction <src/theory/arithmetic/lra_tableau.cpp>` owns the coefficient identity.

The optional linear-maintenance arm merges sorted rows, changes occurrence
links only when support changes, and reuses event coefficients. Convexity
allows stopping once the forward slope is nonnegative, after a complete event
group. Conflict preference takes priority, so pruning cannot skip an unseen
preferred conflict. These optimizations preserve full objective trailing in
the SOI prefix. The later Primo-specific
[fallback lifecycle](../implementation/soi.md#bland-fallback-lifecycle) suspends
objective maintenance and individual undo records during Bland repair, using
one owned snapshot and boundary reconstruction. It has its own proof and
{ref}`evaluation <soi-fallback-full-results>`; it is not a thesis mechanism.
Primo does not claim the thesis's complete linked-matrix representation.

## FMCAD §V-B and thesis §3.3.5: multi-row explanations

**Purpose and assumptions.** Explain infeasibility when no single error row
conflicts but a nonempty signed sum is blocked. For selected errors $S$, write
$f_S=\sum_{i\in S}d_i b_i$ and $B_S=\sum_{i\in S}d_i v_i$ for violated bounds
$v_i$. At the current assignment $f_S(\beta)>B_S$. Blocking nonbasic bounds
imply $f_S(x)\ge f_S(\beta)$, while selected violated bounds require
$f_S(x)\le B_S$, which is impossible. This is the sufficient certificate used
by the builder and thesis Lemma 3.2.

**Operations and costs.** Construct the restricted signed row, test each
coefficient for flexibility, and collect selected-basic and blocking-nonbasic
bound reasons. Construction visits selected row terms; exact cancellations
matter. Reducing selected rows can shorten reasons, but reason literals can
merge or expand through antecedents, so row count is not clause size.

**Example and owner.** At zero, $b=x-y\ge1$ and $c=y-x>0$ are violated and
their signed sum is zero. Neither row alone blocks when $x,y$ are unbounded.
{soifinalsource}`restricted_soi_row <src/theory/arithmetic/lra_tableau.cpp>`, {soifinalsource}`soi_subset_still_conflicts <src/theory/arithmetic/lra_tableau.cpp>` and {soifinalsource}`build_soi_conflict <src/theory/arithmetic/lra_tableau.cpp>` implement the
certificate. Original equations plus only the selected premises are independently
refuted by Z3 in fixtures, including strict bounds and expanded propagation
antecedents. This does not audit every online reason graph.

The blocked-subset predicate is not monotone: at $x=0$ with $x\ge0$,
$b=-x\ge1$ is blocked, but adding $c=2x\ge1$ gives a flexible sum.
Historical recursive and deletion arms retain sufficient certificates; their
names do not justify generic monotone QuickXplain guarantees.

## Figure 3.6: sign-driven row growth and recursive reduction

**Purpose and assumptions.** Reduce error rows without additional Simplex
search. A call has disjoint fixed background $C$ and candidate set $D$ of
violated basics, with nonempty blocked union. These are private caller
preconditions. The returned set includes $C$.

**Operations and costs.** Start with $C$, or one candidate if it is empty. Grow
by choosing a flexible coefficient whose sign differs from the full sum and an
unused row with the opposite sign at that coefficient. The full blocked sum
guarantees a cancellation witness. Once the partial sum blocks, retain the
last-added row with $C$, split the remaining rows in half, and recursively
reduce one half while fixing the other, then reduce the other against the
retained result. A single invocation adds at most $|D|$ rows, and recursive
candidate sets shrink. This gives termination and certificate sufficiency,
not an inclusion-minimality proof or the usual monotone-oracle complexity bound.

**Example, owner and resolved boundaries.** In the two-row cancellation example,
the first row's flexible coefficient directs addition of the second. The
`row-growing` option calls {soifinalsource}`row_growing_soi_conflict <src/theory/arithmetic/lra_tableau.cpp>`; other arms remain separate.
Primo interprets Figure 3.6's undefined $C$ at line 15 as $S_1$, following the
prose. A blocked singleton retains its seed as last-added; an already-blocked
background returns immediately. ID choices and floor-half splits are
deterministic engineering choices. Missing witnesses are invariant failures.
The [implementation chapter](../implementation/soi.md#experimental-row-growing-reduction)
gives recursion details and counter scope. Tests include non-monotone subsets;
no minimality claim is made.

## FMCAD §V-C and thesis §3.3.2: termination

**Purpose and assumptions.** Relate eventual zero-progress selection to Bland's
anti-cycling argument. Under the source rules, a nonterminating tail with
unchanged objective must eventually use zero-distance updates. Distance and ID
ties then matter to the proof, not just to reproducibility.

**Operations, costs and example.** Positive objective improvement is not a
polynomial pivot bound. A zero-progress tie between IDs must discard coefficient
magnitude, as above. The proof motivates exact tuple comparisons in
{soifinalsource}`soi_select <src/theory/arithmetic/lra_tableau.cpp>`; it is not a performance estimate.

**Status and owner.** {soifinalsource}`check <src/theory/arithmetic/lra_tableau.cpp>` retains the existing $4n+64$ prefix limit, where $n$ includes all tableau variables, slacks and the
artificial objective (with one additional selection allowed by its inclusive guard), then Bland repair.
Short columns also switch after the configured zero-progress streak. Own-bound
zero events can select a no-op; the guard remains a defensive safeguard.
Counters report both prefix and suffix work. No unrestricted-paper termination
claim is attached to the combined engineering profile.

## FMCAD §V-D and thesis §3.3.3: short-column heuristics

**Purpose and assumptions.** Avoid exhaustive pricing when one progressing
candidate suffices. Columns are ordered by length, then ID. A zero-progress
candidate cannot justify concluding that no later column progresses.

**Operations and costs.** {soifinalsource}`soi_select <src/theory/arithmetic/lra_tableau.cpp>` partially sorts chunks and stops at the
first progressing candidate, or proven conflict when caching is enabled. If a
chunk has none, it continues through remaining columns. Worst-case crossing
work remains exhaustive; chunks change ordering cost, not the visited order.
After a consecutive stall limit, {soifinalsource}`check <src/theory/arithmetic/lra_tableau.cpp>` switches to ordinary Bland repair.

**Example and status.** If the first chunk contains only blocked or zero-progress
columns and column 17 improves, a budget of 16 still reaches column 17. Tests
exercise this boundary. Budgets 4/16/64 and stalls 8/16/32 were a predeclared
pilot grid. Its frozen 64/8 choice is a Primo parameter choice, not a paper
constant. Public option defaults remain 16/16 and the selector stays historical.

## Thesis §3.3.4 and FMCAD §V-D: cached conflict preference

**Purpose and assumptions.** Prefer a breakpoint whose pivot exposes an
immediate conflict on the entering variable's new row. Activity/flexibility
counts permit a constant number of count tests for that prediction; their
maintenance is essential to the claimed fast test.

**Operations and costs.** {soifinalsource}`RowActivity <src/theory/arithmetic/lra_tableau.hpp>` stores positive/negative coefficient
counts at each required active bound, and {soifinalsource}`BoundStatus <src/theory/arithmetic/lra_tableau.hpp>` stores variable bound
activity. Status changes update reverse occurrences; rewritten rows refresh
counts; rollback rebuilds them. Primo's counts exclude the implicit basic
coefficient. Substitution refresh uses an extra row pass rather than the
thesis's fully fused callbacks. The candidate test is constant in row length,
not constant GMP cost or zero maintenance cost.

**Example, owner and limits.** In $b=2x$, $b\ge6$, $x\le1$, predicting the
$x=3$ pivot finds the new row $x=b/2$ blocked. {soifinalsource}`soi_crossing_conflicts <src/theory/arithmetic/lra_tableau.cpp>` uses
cached counts and the crossing coefficient; focused tests compare predictions
and every maintained count with slow recomputation after bounds, updates,
pivots and pop. The optional cache flag allocates tableau-owned state only when
enabled. Paper pricing compares eligible columns; short pricing stops at the
first priced column with conflict or progress, and may skip later conflicts.
A proven conflict can outrank a negative objective change. Preference for
shrinking the entire error set, also mentioned by the sources, is deferred.

## FMCAD §VI and thesis §3.4: experiments and interpretation

**Purpose and assumptions.** Separate algorithm cost from the rest of the SMT
solver and recognize that many theory checks are easy. The sources compare
controlled CVC4 configurations, then broader solver configurations. Their
historical SMT-LIB and biological-modeling inputs, 1,000-second limits and
hardware are not Primo's experimental conditions.

**Operations and complexity expectations.** Measure completed process times,
solved counts, and check-level pivot distributions. Long latendresse checks
illustrate the intended opportunity for fewer pivots; easy checks reveal the
cost of a more expensive selection procedure. Neither source supplies a
universal speedup or complexity guarantee from a pivot reduction alone.

**Example, owner and status.** Moving from 30 cheap pivots to 20 expensive
pricing steps can lose process time. Primo therefore keeps selection counters,
assertion-time elimination counters, uninstrumented timing and separate
profiles. {soievalsource}`run_soi_improvements.py <scripts/run_soi_improvements.py>` and its summary/model stages evaluate the
frozen 1,000-conjunction corpus with immutable binaries. This is a cold
conjunction comparison, not a reproduction of whole-SMT historical results.
The [experiments chapter](soi-experiments.md) owns its acceptance decision.
