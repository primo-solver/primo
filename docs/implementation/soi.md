# SOI algorithm and implementation

This chapter is for implementers who already understand the
[tableau and four LRA operations](lra.md). It answers how the optional
sum-of-infeasibilities objective is represented, selected and explained, and
which invariants verification must check. Read the
[paper and thesis guide](../papers/soi-guide.md) next for the mathematical sources,
and the [experiments](../papers/soi-experiments.md) for qualification and costs. Sparse remains the solver default.

## Representation

{soiworksource}`LraTableau <src/theory/arithmetic/lra_tableau.hpp>` owns dense variable
state, sorted sparse `Row` vectors, reverse occurrences, bounds with `sat::Lit`
reasons, a set of violated basic IDs, and a trail. IDs survive storage changes;
row references and occurrence iterators do not. An SOI tableau additionally
owns `f_var_`, an always-basic artificial objective, and `soi_directions_`.
No SMT-LIB syntax enters this layer. `check()` returns either no conflict and a
feasible assignment, or a vector of antecedent literals whose conjunction is
inconsistent with the original equations.

Write $b_i=\sum_j a_{ij}x_j$ for each basic row, $\beta$ for the current
assignment, and $l_i,u_i$ for optional bounds. Define

$$
V_i=\max(l_i-\beta_i,0,\beta_i-u_i),\qquad
 d_i=\begin{cases}-1&\beta_i<l_i\\+1&\beta_i>u_i\\0&\text{otherwise.}\end{cases}
$$

An absent bound contributes no violation. Arithmetic is exact in the ordered
pair $q+k\delta$, with lexicographic comparison and symbolic $\delta>0$.
Every nonbasic variable stays inside its bounds. At public operation boundaries, the objective row maintains

$$ f=\sum_{i\in E}d_i b_i=\sum_j c_jx_j,
   \qquad c_j=\sum_{i\in E}d_i a_{ij}, $$

where $E$ is the set of violated basics. This is a **coefficient identity**.
The artificial objective's stored assignment is not maintained as its row
value; selection never uses that assignment. Ordinary basic assignments must
still satisfy their row equations. The unit tests check these two properties
separately, including after assertion, pivot and rollback.

{soiworksource}`set_soi_direction <src/theory/arithmetic/lra_tableau.cpp#L480>` trails the old objective and multiplier, then adds
$(d'_i-d_i)\operatorname{row}(i)$ to its coefficients. The historical update
concatenates and sorts terms and rebuilds objective occurrences. It costs
$O((|f|+|\operatorname{row}(i)|)\log(|f|+|\operatorname{row}(i)|))$ comparison
work plus exact arithmetic. Linear merging and preserving unchanged occurrence
slots are available through the separate experimental maintenance flag; they
are not properties of the historical baseline.

## Crossings and selection

{soiselectsource}`soi_select <src/theory/arithmetic/lra_tableau.cpp>` implements
the crossing walk. For a flexible nonbasic $x_j$, walk in the direction reducing $c_jx_j$.
A row bound $v_k$ is reached after
$h=(v_k-\beta_k)/a_{kj}$. The entering variable's own bound caps the historical walk; the paper arm
continues through all forward crossings.
Between crossings, $V=\sum_i V_i$ is linear, so integrate

$$ \Delta V(h_r)=\Delta V(h_{r-1})+m_{r-1}(h_r-h_{r-1}). $$

After integrating to a group of coincident events, change the slope by
$\sum_k(d'_k-d_k)a_{kj}$. At zero, only movement out of the interval changes
the direction; moving inward does not. A fixed variable has two bound events
at a nonzero crossing but only one outward direction change at zero.

For example, $b=2x-y$, $\beta_x=\beta_y=1$, $3\le b\le7$, $x\le3$,
$y\ge1$ has $V=2$. Increasing $x$ reaches $b=3$ at $h=1$ and $x=3$ at
$h=2$. Both give $\Delta V=-2$; the distance tie-break chooses $h=1$.
This is the FMCAD Figure 6 / thesis Figure 3.5 example.

The reference specification minimizes $(\Delta V,|h|,k)$ per entering
variable, then $(\operatorname{sgn}(\Delta V)|c_j|,j)$ across entering
variables. In particular, zero progress must select the smallest entering ID.
`--lra-soi-selection=historical` retains the old selector for replay and remains
the opt-in SOI default. The new experimental `paper` arm implements the stated
score, retains equal endpoint events, and checks error rows before selection
and after the selected column update (thesis Figure 3.3). Those checks use
ordinary row scans unless the separate experimental cached-activity flag is enabled.

The paper arm also retains crossings beyond the entering variable's own bound.
The variable becomes basic after a pivot and may then be violated. Its own
outward crossing adds one to the forward violation slope; an inward zero event
adds nothing. The historical arm keeps its old cap. For example, $b=2x$,
$x\le1$, $b\ge6$ at zero gives $\Delta V=-2$ at $x=1$ and $\Delta V=-4$ at
$x=3$, including $x$'s new violation. The paper arm selects the latter, pivots
$b$ out, and discovers the blocked row $x=b/2$. This additional conformance
obligation was found while mapping the thesis's §3.3.4 conflict preference;
[the design discussion below](#complete-forward-walk) records the invariant.

Both arms retain a finite prefix limit in
{soiselectsource}`check <src/theory/arithmetic/lra_tableau.cpp>`:
$4n+64$ selected steps by default, with one extra step from the inclusive loop
guard. Here $n$ counts every tableau variable, including slacks and the artificial
objective. The suffix uses ordinary Bland repair. An inward zero own-bound
event can select a no-op; the finite guard covers that boundary too. This is a
defensive implementation safeguard, and this profile is not presented as an
unrestricted execution of the paper's algorithm.

## Experimental short-column pricing

`--lra-soi-selection=short-column` retains the reference arm's complete crossing
walk and row-conflict checks, but considers columns in increasing occurrence
count, breaking ties by variable ID. With cached conflict preference disabled,
it stops at the first candidate with negative objective change. A zero-progress candidate cannot stop the search:
the selector examines the remaining columns before selecting a zero-progress
step or concluding that the objective is blocked.

{soishortsource}`The selector <src/theory/arithmetic/lra_tableau.cpp>` orders
columns with partial-sort chunks. `--lra-soi-pricing-candidates` is the chunk
size, initially 16; it controls sorting work rather than truncating the set of
eligible columns. A chunk can include ineligible columns, which are skipped
before crossing calculation. Candidate budgets 4, 16 and 64 are the frozen
pilot grid. `--lra-soi-stall-limit`, initially 16, counts consecutive steps with
zero objective change and switches to ordinary Bland repair once the limit
is reached. Progress resets that count. Stall limits 8, 16 and 32 are the
frozen grid. The overall finite guard remains active too.

These constants and the partial-sort organization are Primo engineering
choices, motivated by thesis §3.3.3 / FMCAD §V-D rather than specified by them.
The budget changes sorting cost; stopping at the first progressing column means
all three budgets visit the same ordered sequence, apart from time limits.
The stall limit can change the repair path. Pure tableau regressions exercise
continuing beyond an unproductive chunk and entering the Bland suffix at a
one-step stall limit. The [recorded pilot](../papers/soi-experiments.md#parameter-pilot) froze 64 and 8
for the evaluated candidate. Public option defaults remain 16 and 16; a full
profile must pass the separate acceptance rule before promotion.

## Experimental objective and crossing maintenance

`--lra-soi-linear-maintenance` enables three changes owned by
{soimaintsource}`set_soi_direction and soi_select <src/theory/arithmetic/lra_tableau.cpp>`.
It is off by default and is measured separately from short columns and caching.

First, the objective update merges its already sorted row with the signed
source row in one linear pass over their terms. Surviving terms keep their
occurrence back-slots. Only a newly supported variable adds an occurrence;
only exact cancellation removes one. Reverse-slot repair for a removal still
needs a binary search in the moved occurrence's row. The merge itself visits
$|f|+|\operatorname{row}(i)|$ terms; this is not a claim of constant-time
GMP arithmetic or occurrence repair. Every direction change still saves a full
old objective row, so its linear copy cost and trail memory remain.

Second, each crossing retains the original row coefficient. Slope integration
and the selected pivot target reuse that value instead of searching the same
row again. Coefficient discovery during initial event generation still searches
the row; that remaining cost is not eliminated by this profile.

Third, the forward derivative of the convex violation function is nondecreasing.
Once it is nonnegative after a complete crossing group, no farther point can
improve either objective value or the distance tie-break. The ordinary walk
can stop there. A cached conflict has higher priority than the objective, so
this profile first classifies conflict events and avoids pruning before an
unvisited one. Once the nearest conflict group is examined, later distances
cannot win. All coincident events in a retained group are considered before
stopping.

Focused tests compare exact selected triples with the full walk, check the
objective identity and occurrence-driven updates through rollback, and require
fewer objective occurrence changes on a cancellation/support-change example.
`:lra.soi-objective-terms` counts terms offered to objective maintenance;
`:lra.soi-objective-occurrence-changes` counts links/unlinks inside objective
direction maintenance (`set_soi_direction`). It excludes incidence changes made
by generic pivoting and rollback index rebuilds, as well as full old-row trail
copies. These are work counters, not timing estimates. Full trailing remains in
both maintenance settings.

## Conflict explanations

For a nonempty subset $S\subseteq E$, let $f_S=\sum_{i\in S}d_i b_i$ and
$B_S=\sum_{i\in S}d_i v_i$, using each row's violated bound $v_i$.
Then $f_S(\beta)>B_S$. If every positive coefficient is blocked at its lower
bound and every negative coefficient at its upper bound, those blocking bounds
imply $f_S(x)\ge f_S(\beta)$. The selected basic bounds imply
$f_S(x)\le B_S$, giving a contradiction. {soiworksource}`build_soi_conflict <src/theory/arithmetic/lra_tableau.cpp#L745>`
uses {soiworksource}`restricted_soi_row <src/theory/arithmetic/lra_tableau.cpp#L728>`
and returns precisely
the blocking and selected violated-bound reasons.

For $b=x-y\ge1$ and $c=y-x>0$, both rows are violated at zero; their signed
sum is zero, yet their selected bounds require a strictly negative sum. Neither
row is blocked individually when $x,y$ are unbounded. This is a multi-row
conflict and needs both bound reasons.

The predicate “$S$ is nonempty and $f_S$ is blocked” is not monotone: at
$x=0$, $x\ge0$, $b=-x\ge1$, $c=2x\ge1$, the singleton $\{b\}$ is blocked,
but $\{b,c\}$ is flexible. The {soiworksource}`historical recursive and deletion reducers <src/theory/arithmetic/lra_tableau.cpp#L810>` preserve a
certificate; neither is claimed inclusion-minimal. The separate `row-growing` arm implements Figure 3.6's sign-driven growth,
with the boundary resolutions below.

{soiselectsource}`Focused explanation fixtures <tests/unit/lra_soi_explanation_tests.cpp>`
save original equations before pivoting and export **only selected premises**.
{soiworksource}`The reference gate <scripts/check_soi_explanations.py>` requires a
successful reference process returning exactly `unsat`. Cases include strict
bounds, cancellation, the non-monotone example, and a propagated bound expanded
into its original antecedents. The propagation expansion is a fixture-level
check, not validation of every reason graph in the online SMT engine. Existing
reason-membership tests are named as membership tests rather than soundness
proofs.

## Experimental row-growing reduction

`--lra-soi-minimize=row-growing` selects
{soifinalsource}`row_growing_soi_conflict <src/theory/arithmetic/lra_tableau.cpp>`.
The historical `quickxplain`, `deletion` and `none` arms remain available. This
new arm reads tableau bounds, assignments, basis and rows without changing them;
diagnostic counters increase. Scratch ID vectors, an included-ID set and exact
sparse coefficient maps have invocation-local lifetime.

The private call takes disjoint sets of violated basics, a fixed background
$C$ and candidates $D$, whose nonempty union has a blocked signed sum. These are
caller preconditions, not a general input-validation API. Start $X=C$, or seed
$X$ with the smallest candidate ID when $C$ is empty. Construct $f_X$. While a
coefficient is flexible, select the smallest flexible column $j$ whose sign in
$f_X$ differs from its sign in the full blocked sum $f_{C\cup D}$. Add the
smallest unused row $i$ whose signed coefficient $d_i a_{ij}$ has the opposite
sign to $f_{X,j}$. A witness exists: the full sum must cancel or reverse the
flexible coefficient. Each growth iteration adds a distinct row, so at most
$|D|$ additions occur in one invocation.

For example, at $x=y=0$, the violated rows $b=x-y\ge1$ and
$c=y-x>0$ have signed rows $-x+y$ and $x-y$. Starting with $b$ leaves a
flexible coefficient; adding $c$ cancels both coefficients. The resulting zero
row is blocked and its violated bounds give a contradiction. This also shows
why the predicate is about the signed sum, not a new Simplex consistency search.

Once $X$ is blocked, retain $R=C\cup\{i_{last}\}$. Partition the ascending IDs
of $X\setminus R$ into floor-half $X_1$ and remainder $X_2$. First reduce $X_2$
with fixed $R\cup X_1$. Keep its returned members outside $X_1$ in $R$, then
reduce $X_1$ with fixed $R$. Every recursive return includes its fixed
background. Both recursive unions are certified and their candidate sets
strictly shrink. Induction preserves a sufficient certificate; this argument
does not establish inclusion-minimality for the non-monotone predicate.

Figure 3.6 line 15's undefined $C$ is interpreted as $S_1$, following the prose.
If the initial singleton is already blocked, its seed is the last-added row.
An already-conflicting nonempty background returns immediately. Empty union
and missing cancellation witnesses are invariant errors, never permission to
return an empty explanation. Smallest-ID choices and floor-half partitioning
are deterministic Primo choices.

`:lra.soi-predicate-calls` includes each growth-loop blocked test,
including the terminal one. `:lra.soi-growing-rows` counts additions by that loop
across recursion, excluding seeds and background/full-sum construction.
Restricted arithmetic terms include sum construction and addition, but exclude
witness-search comparisons and other bookkeeping. These are neither total
arithmetic-operation counts nor GMP bit-complexity estimates. Selected rows and
final reason literals measure the result independently. Focused fixtures cover
singleton seeds, cancellation, unrelated conflicts, strict bounds and the
non-monotone example; every emitted selected-premise problem is refuted by Z3.

## Bland fallback lifecycle

Reader: implementers reviewing heuristic shutdown and rollback. Prerequisites:
ordinary tableau equations, nonbasic feasibility and the SOI coefficient
identity above. This section answers which state can stop being maintained
when the active repair policy changes to Bland, and how later checks/pop remain
valid. Next: {ref}`the fallback experiment <soi-fallback-followup>`.

This is a Primo lifecycle design, motivated by the observed SOI tail costs;
it is not a mechanism claimed from FMCAD or the thesis. Bland selection still
uses the existing smallest eligible IDs. Sparse/Dantzig already bypass their
pricing when Bland is selected. The common leaving scan also skips
violation-magnitude arithmetic once ID selection is sufficient.

Implementation: {soifallbacksource}`suspend_soi_for_fallback <src/theory/arithmetic/lra_tableau.cpp#L666>` runs immediately before the
first actual Bland pivot in an SOI check. A suffix ending without a pivot needs
no snapshot. {soifallbacksource}`SoiFallbackTrailEntry <src/theory/arithmetic/lra_tableau.hpp>` owns one copy of all `VarState`, `Row` and
directional-multiplier vectors at that boundary. This costs O(n + nnz_stored)
storage, where n is the variable count and nnz_stored counts terms in every
copied row slot, including stale nonbasic rows,
plus copying exact values. Occurrences, violations and activity are derived and
are rebuilt rather than copied. Prefix rollback records remain untouched.
The record lives in the tableau trail until pop crosses it or the tableau is
destroyed; multiple completed checks can retain multiple snapshots.

After saving the snapshot, detach the artificial objective from all occurrence
lists and clear its live row. Set a private suspension flag. While suspended,
`set_soi_direction` and activity hooks do no work, and
`trails_assignments_and_pivots` returns false. Ordinary equation updates,
nonbasic feasibility, violated-basic membership and Bland conflict explanations
remain mandatory. Neither ordinary rows nor bounds depend on the artificial
objective: its variable is always basic, has no bounds, occurs in no ordinary
RHS and is excluded from model bindings. Thus detaching it cannot change an
ordinary Bland choice. Occurrence-list order may change, but Bland's ID choices
and exact ordinary row algebra do not depend on that order.

At either normal suffix return (feasible or explained conflict),
{soifallbacksource}`resume_soi_after_fallback <src/theory/arithmetic/lra_tableau.cpp#L679>` reconstructs all multipliers from current ordinary
bounds/assignments, sums the signed violated rows, sorts the surviving objective
support, reattaches its occurrences and rebuilds activity once. The local
`std::unordered_map<LraVarId, util::Rational>` accumulates coefficients. If $m_V$
counts terms in the violated rows and $k$ is the surviving objective width,
reconstruction takes expected amortized $O(n+m_V+k\log k)$ scalar operations; an enabled
activity cache adds $O(n+\mathrm{nnz}_{active})$ work. Occurrence-vector appends
have amortized constant insertion cost; a single reconstruction can reallocate
and copy longer columns. Detachment uses existing
occurrence removal, including binary-search back-slot repair in other rows.
These are scalar-operation bounds, separate from exact-value copying and GMP
bit costs. Snapshot restoration currently copies its owned vectors; destruction
and restoration costs remain in whole-process measurements. The artificial
assignment remains intentionally unused. The coefficient identity is restored
before the caller can assert bounds, request a checkpoint or start another
check. No re-entrant calls or new checkpoints occur inside `check`. Structural
extensions must precede the checkpoint being restored: `pop` must never cross
variable/row creation. Equality probes may extend a checked copy, but install
the temporary row before taking their checkpoint and never pop across it.

Rollback proof: every public checkpoint predates the suffix snapshot or follows
its completed check. Popping a later checkpoint first undoes any subsequent
operations using the usual prefix records. If pop crosses a suffix snapshot,
restoring its owned variables/rows/directions reinstates the exact pre-suffix
state; earlier prefix records can then be undone normally. Occurrences,
violations and activity rebuild once after the undo loop. No intermediate
suspended position can be a caller-created checkpoint. This is one coarse
rollback record for the suffix, not an unproved omission of rollback. Existing
methods do not promise a strong allocation-exception guarantee; normal
feasible/conflict exits must restore the active-state invariants.

{soifallbacksource}`Focused tests <tests/unit/lra_tableau_tests.cpp>` compare eager and suspended suffixes on multi-pivot SAT
systems, ordinary basis/assignments/rows, strict bounds, nested checkpoints,
repeated checks and cache recomputation. A direct private-state probe requires
zero incremental objective/activity work and zero per-pivot trail growth while
suspended. {soifallbacksource}`Multi-pivot strict UNSAT fixtures <tests/unit/lra_soi_explanation_tests.cpp>` export selected premises from eager and suspended suffixes
for independent refutation. Counters expose snapshots, copied stored
row terms, untrailed suffix pivots and objective rebuild terms; profiling clocks
include boundary rebuild work. `--lra-soi-suspend-on-fallback` enables the
lifecycle (default on within opt-in SOI); its `--no-` counterpart retains the
eager reference.
This does not select the experimental paper/short-column selector as default.
The {ref}`qualified lifecycle comparison <soi-fallback-full-results>` covers
this frozen conjunction corpus. Full prefix trailing remains; these measurements
do not establish performance on warm whole-SMT search.

## Rollback and measurement

SOI retains per-operation bounds, assignment, basis and objective records in
its prefix. A suspended suffix instead owns the full pre-suffix snapshot above;
`--no-lra-soi-suspend-on-fallback` retains eager per-operation suffix records.
{soifallbacksource}`pop <src/theory/arithmetic/lra_tableau.cpp#L2373>` restores those
records in reverse order and rebuilds occurrences and violations once. The
suffix snapshot preserves rollback without individual suffix records.

{soiworksource}`SoiWork <src/theory/arithmetic/lra_tableau.hpp>` counts selected steps, zero objective changes, eligible entering
candidates examined, crossings generated before truncation, objective direction
updates, prefix and fallback pivots, certificate predicate calls, restricted-row
terms processed, and selected explanation rows. These cumulative counters are
not trailed. A selected step may reach its own bound without a pivot, so selected
steps and pivots must not be equated. {soiworksource}`Engine reporting <src/smt/solver_engines.cpp#L7397>` includes objective work
since the previous report, including assertion-time updates. Existing
elimination-pivot counters still expose assertion-time fixed elimination.

Only `--lra-call-profile` enables the selection and objective-maintenance phase
clocks. Ordinary runs perform no phase clock reads. Objective timing includes
assertion-time updates; selection timing is nested within check timing. Do not
add overlapping phase timers or treat profiled process times as uninstrumented
performance measurements. The [statistics reference](../reference/statistics.md)
defines the printed names.

## Sources and implementation status

The basis is King, Barrett and Dutertre, *Simplex with Sum of Infeasibilities for
SMT*, FMCAD 2013 {cite}`King-fmcad2013`, §V-A–D, and King, *Effective Algorithms
for the Satisfiability of Quantifier-Free Formulas Over Linear Real and Integer
Arithmetic*, 2014 {cite}`king2014`, §§3.2–3.3.5. Primary texts were read for this
checklist; no other solver implementation is an implementation source.

| Source operation | Primo owner, invariant and obligation | Status at this milestone |
| --- | --- | --- |
| Thesis §3.2: equations, bound status, violation | `VarState`, `Row`, `update`; ordinary equations and nonbasic feasibility survive updates | Implemented; separate objective coefficient test added |
| §3.3.1 / FMCAD §V-A: integrate sorted crossings | `soi_select`; integrate before slope updates; exact rational/delta arithmetic; $O(p\log p)$ sorting for $p$ events | Historical baseline retained; paper arm retains ties and the complete forward walk; coefficient reuse and convex stopping are separate experimental maintenance features |
| Figure 3.4: entering and leaving score | `SoiSelection`; zero-score entering tie is by ID | Experimental `paper` arm implemented; historical arm retained |
| Figure 3.3: error-row conflicts before and after updates | `check`, `build_conflict`; sufficient selected bounds must independently refute | Experimental paper-arm early scans and focused external refutation gate implemented |
| §3.3.2 / §V-C: termination | `check`; finite prefix then Bland | Defensive fallback retained, with prefix/fallback pivot counters |
| §3.3.3 / §V-D: short columns, stalled progress | `Tuning`, `soi_select`; exhaust remaining candidates before declaring blocked | Experimental short-column arm implemented; pilot froze chunk 64 and stall 8 for evaluation; public defaults remain 16/16 |
| §3.3.4: constant-time breakpoint conflict preference | Tableau-owned RHS activity counts, status deltas, rewritten-row refresh and rollback rebuild | Experimental flag implemented; crossing predictions and counts compared to slow recomputation |
| §3.3.5 / §V-B and Figure 3.6 | `restricted_soi_row`, conflict builder and minimizer; nonempty blocked certificate | Builder and historical reducers implemented; row-growing arm implemented experimentally with explicit boundary resolutions |
| Thesis §3.4 / FMCAD §VI | Reproducible process measurements, qualified answers and separate SAT-model validation | Controlled pilot and separate full-corpus qualification; see the experiments chapter for the result and its limits |

## Complete forward walk

Reader: the implementer of the paper selector and cached conflict preference.
Prerequisites: the historical SOI walk and nonbasic feasibility invariant.
Question: can the paper selector consider crossings beyond the entering
variable's own bound? Next reading: thesis §3.3.4 and Figures 2.3, 3.3–3.4.

The historical cap in `LraTableau::soi_select` keeps the entering variable
inside its own bounds throughout its walk. Consequently a newly basic entering
variable cannot be in error; the specific new-entering-row conflict preference
in thesis §3.3.4 is vacuous under that cap. This is an implementation inference,
not a statement made explicitly in the paper.

The implemented experimental paper arm retains all forward row crossings, including those
beyond the entering bound, and includes the entering variable's own violation
in the objective walk. Figure 3.4 enumerates both bounds for k=j and every row
containing j, without a bound-cap operation. The text permits new basic variables
to be violated. Section 3.3.4 explicitly tests whether j becomes an error after
the candidate pivot. The old cap remains in the historical arm.

Write h=walk_sign*t for forward distance t>=0. At an OUTWARD entering-bound
crossing, dV/dt gains 1; in signed-step coordinates beta gains walk_sign.
An inward zero event contributes no slope. The reference arm includes both
forward own-bound events, including inward zero events, matching the literal
Figure 3.4 tuples. Such a zero event can select a no-op; the retained finite
fallback prevents an indefinite no-op prefix. We do not claim unrestricted
paper-algorithm termination for this engineering profile. Historical code's `k == j` slope skip
is therefore retained only for the capped historical arm. All other direction
changes continue to use their crossing row coefficient. Selection minimizes
(DeltaV,abs(step),leaving-id), then the Figure 3.4 entering score.
The execution owner is `check`: for k=j it only updates j to the selected
own bound. For k!=j it pivots leaving k with entering j, then sets
the newly nonbasic k to its selected bound. After the composed operation, every
nonbasic variable is within its bounds: variables other than k do not move,
and k is exactly at its bound. The newly basic j is allowed to violate its own
bound and is accounted for by `update`'s direction change. Ordinary row equations
continue to hold by exact pivot/update algebra. SOI prefix operations retain
full trailing; the Bland suffix uses the lifecycle described above.

Example: b=2x, x<=1, b>=6, starting x=b=0. The historical cap chooses x=1
without a pivot. The complete walk has DeltaV=-2 at x=1 and DeltaV=-4 at
x=3 (b=6 and x's violation=2). The paper arm chooses the latter, pivots b
out and x in, and detects x=b/2<=1 as a blocked row with b>=6 in its
post-update scan. This reaches the geometry needed for §3.3.4's proposed
candidate preference; the current reference arm detects it after execution.

### Experimental cached conflict preference

`--lra-soi-cached-conflicts` enables this separate experimental mechanism with
the paper and short-column selectors. It is off by default. The historical
selector does not use candidate conflict preference, even if the cache is enabled.
{soicachesource}`LraTableau <src/theory/arithmetic/lra_tableau.hpp>` owns the
per-variable `BoundStatus` vector and per-basic-row `RowActivity` vector. These
vectors are allocated only when caching is enabled. `set_tuning` rebuilds them
when enabling the option; tuning must be configured before search and is not
trailed.

A proven row-conflict candidate has priority over both ordinary tuples.
The paper selector examines every eligible column. Short-column pricing instead
stops at the first priced column offering either a proven conflict or ordinary
negative objective change. It does not inspect later columns for conflicts once
an earlier column supplies progress. Within a priced column, a proven conflict
has priority; it can have positive $\Delta V$ because execution then checks and
returns the conflict immediately. Non-conflict selections retain the ordinary
nonpositive-progress obligation.
Among examined candidates, ties use entering ID, then distance, then leaving
ID (an engineering choice). Coincident lower/upper events of a fixed variable
produce the same operation; the resulting row selects the required reason side.
`check` executes the selected operation, checks the resulting entering-row
conflict and immediately returns its explanation. A failed prediction raises an
invariant error rather than emitting an unchecked explanation.
Otherwise select by the ordinary tuples and require nonpositive DeltaV.

{soicachesource}`Activity maintenance <src/theory/arithmetic/lra_tableau.cpp>` maintains h_+,h_-,g_+,g_- per ordinary basic row, adapting
thesis §2.2.8 / Figures 2.11–2.12. The Primo cache counts only stored RHS nonbasic
terms, EXCLUDING the thesis full equation row's implicit basic diagonal -1: h_+ counts positive coefficients at a lower
bound, h_- negative coefficients at a lower bound; g_+ negative coefficients at
an upper bound, g_- positive coefficients at an upper bound. A fixed variable
contributes to both its relevant lower and upper counts. Track nonbasic bound
status on assertion/update, adjust column rows on status transitions, refresh
rewritten row counts through substitution, and rebuild counts after pop.
`sync_activity_status` applies old/new lower/upper status contributions to
column rows. After substitution, `rebuild_row_activity` scans the rewritten RHS
once; this is an additional linear pass, rather than the thesis's per-entry sign
callbacks fused into substitution. `pop` restores the tableau through prefix records or the suffix snapshot and
then rebuilds all cache state from live assignments, bounds and rows. Counts
exclude the artificial objective. The lifecycle above also rebuilds the cache
once when leaving a suspended suffix.

For a crossing on row k with a=a_kj, suppose the new value of j is violated
with direction lambda_j. Solve the row as j=k/a-sum_{i!=j}(a_ki/a)i. Put
sigma=-lambda_j*sign(a). All unchanged nonbasics except j must block sigma
row(k); the new nonbasic k must be at its lower bound if lambda_j*sign(a)>0,
and its upper bound otherwise. Write A_sigma=h_sigma+g_sigma. Then A_sigma minus the indicator that j's
relevant bound is currently active must equal support(row(k))-1, where
support is the number of stored nonzero RHS terms. {soicachesource}`soi_crossing_conflicts <src/theory/arithmetic/lra_tableau.cpp>`
implements this predicate in a constant number of arithmetic operations once
the coefficient and counts are available; this is not a bit-complexity claim.
A fixed leaving variable can supply either coincident event because both bounds
are active at the same value.

{soicachetests}`Focused tests <tests/unit/lra_tableau_tests.cpp>` independently
recompute all four counts and compare every available crossing prediction to
an explicitly solved-row/blocking-bound computation after bound changes,
assignment updates, pivoting and rollback. They cover both coefficient signs,
fixed and strict bounds. The
{soicachetests}`explanation fixtures <tests/unit/lra_soi_explanation_tests.cpp>`
exercise cached candidate preference past an entering bound and export only
selected premises for reference refutation. `:lra.soi-activity-updates` counts
status/row term contributions processed; `:lra.soi-conflict-preferences` counts
selected predictions that ended the check with the resulting row conflict.
These measure work and use, not a performance acceptance result.
