# SOI algorithm and implementation

This chapter is for implementers who already understand the
[tableau and four LRA operations](lra.md). It answers how the optional
sum-of-infeasibilities objective is represented, selected and explained, and
which invariants verification must check. Read the
[dated audit](../papers/soi-implementation-audit.md) next for the gaps in the
historical selector and the cost evidence. Sparse remains the solver default.

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
Every nonbasic variable stays inside its bounds. The objective row maintains

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
slots are planned improvements, not properties of this baseline.

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
ordinary row scans; cached activity is still a separate planned mechanism.

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
count, breaking ties by variable ID. It stops at the first candidate with
negative objective change. A zero-progress candidate cannot stop the search:
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
one-step stall limit. No tuning result or performance acceptance is claimed yet.

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
certificate; neither is claimed inclusion-minimal. Figure 3.6's sign-driven
row-growing minimizer remains a separate planned arm.

{soiselectsource}`Focused explanation fixtures <tests/unit/lra_soi_explanation_tests.cpp>`
save original equations before pivoting and export **only selected premises**.
{soiworksource}`The reference gate <scripts/check_soi_explanations.py>` requires a
successful reference process returning exactly `unsat`. Cases include strict
bounds, cancellation, the non-monotone example, and a propagated bound expanded
into its original antecedents. The propagation expansion is a fixture-level
check, not validation of every reason graph in the online SMT engine. Existing
reason-membership tests are named as membership tests rather than soundness
proofs.

## Rollback and measurement

SOI continues to trail bounds, assignments, basis changes, directions and full
objective rows, including during its Bland suffix.
{soiworksource}`pop <src/theory/arithmetic/lra_tableau.cpp#L1931>` restores those
records in reverse order and rebuilds occurrences and violations once. No
rollback optimization is included here.

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
| §3.3.1 / FMCAD §V-A: integrate sorted crossings | `soi_select`; integrate before slope updates; exact rational/delta arithmetic; $O(p\log p)$ sorting for $p$ events | Historical baseline retained; paper arm retains ties and the complete forward walk; coefficient reuse still planned |
| Figure 3.4: entering and leaving score | `SoiSelection`; zero-score entering tie is by ID | Experimental `paper` arm implemented; historical arm retained |
| Figure 3.3: error-row conflicts before and after updates | `check`, `build_conflict`; sufficient selected bounds must independently refute | Experimental paper-arm early scans and focused external refutation gate implemented |
| §3.3.2 / §V-C: termination | `check`; finite prefix then Bland | Defensive fallback retained, with prefix/fallback pivot counters |
| §3.3.3 / §V-D: short columns, stalled progress | `Tuning`, `soi_select`; exhaust remaining candidates before declaring blocked | Experimental short-column arm implemented; chunk/stall constants require pilot qualification |
| §3.3.4: constant-time breakpoint conflict preference | Tableau-owned activity counts, updated by status/sign changes | Planned; cache must match slow recomputation after rollback |
| §3.3.5 / §V-B and Figure 3.6 | `restricted_soi_row`, conflict builder and minimizer; nonempty blocked certificate | Builder and historical reducers implemented; row-growing arm planned |
| Thesis §3.4 / FMCAD §VI | Reproducible process measurements, qualified answers and separate SAT-model validation | Historical audit only; no new performance acceptance yet |

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
continue to hold by exact pivot/update algebra. SOI full trailing is unchanged.

Example: b=2x, x<=1, b>=6, starting x=b=0. The historical cap chooses x=1
without a pivot. The complete walk has DeltaV=-2 at x=1 and DeltaV=-4 at
x=3 (b=6 and x's violation=2). The paper arm chooses the latter, pivots b
out and x in, and detects x=b/2<=1 as a blocked row with b>=6 in its
post-update scan. This reaches the geometry needed for §3.3.4's proposed
candidate preference; the current reference arm detects it after execution.

### Planned cached conflict-preference design

The following mechanism is a proposed next stage and is **not implemented in
the reference-selector snapshot**.

A proven row-conflict candidate has priority over both ordinary tuples.
Among examined candidates use (entering ID, distance, leaving ID, bound kind)
for deterministic conflict ties (an engineering choice). Execute that operation,
check the resulting row conflict, and immediately return its explanation.
Otherwise select by the ordinary tuples and require nonpositive DeltaV.

For activity counts, maintain h_+,h_-,g_+,g_- per ordinary basic row, adapting
thesis §2.2.8 / Figures 2.11–2.12. The proposed Primo cache will count only stored RHS nonbasic
terms, EXCLUDING the thesis full equation row's implicit basic diagonal -1: h_+ counts positive coefficients at a lower
bound, h_- negative coefficients at a lower bound; g_+ negative coefficients at
an upper bound, g_- positive coefficients at an upper bound. A fixed variable
contributes to both its relevant lower and upper counts. Track nonbasic bound
status on assertion/update, adjust column rows on status transitions, refresh
rewritten row counts through substitution, and rebuild counts after pop.

For a crossing on row k with a=a_kj, suppose the new value of j is violated
with direction lambda_j. Solve the row as j=k/a-sum_{i!=j}(a_ki/a)i. Put
sigma=-lambda_j*sign(a). All unchanged nonbasics except j must block sigma
row(k); the new nonbasic k must be at its lower bound if lambda_j*sign(a)>0,
and its upper bound otherwise. Write A_sigma=h_sigma+g_sigma. Then A_sigma minus the indicator that j's
relevant bound is currently active must equal support(row(k))-1, where
support is the number of stored nonzero RHS terms. This is an O(1)
candidate predicate once crossing coefficient and counts are available.

Proof obligations/tests: independently recompute all four cached counts after
every bound/assignment/substitution/rollback operation; compare each predicted
candidate conflict against an explicit solved-row/blocking-bound recomputation;
exercise both coefficient signs, movement directions, fixed bounds and strict
bounds; independently refute the emitted selected-premise explanation. Record
cache updates and successful preference selections separately from runtime.
