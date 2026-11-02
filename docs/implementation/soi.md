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

{soiworksource}`soi_select <src/theory/arithmetic/lra_tableau.cpp#L542>` implements
the crossing walk. For a flexible nonbasic $x_j$, walk in the direction reducing $c_jx_j$.
A row bound $v_k$ is reached after
$h=(v_k-\beta_k)/a_{kj}$. The entering variable's own bound caps the walk.
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
The historical implementation instead prefers the largest coefficient even
on zero progress and may truncate equal endpoint events before the leaving-ID
tie-break. These remain open selector obligations at this verification
milestone. {soiworksource}`check <src/theory/arithmetic/lra_tableau.cpp#L1746>`
owns the finite prefix limit, $4n+64$ selected steps by default with one
extra step from the inclusive loop guard, then switches to ordinary Bland repair;
here $n$ counts all tableau variables, including leaves, slacks and the
artificial objective. This safeguard supplies termination independently of paper conformance.

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

{soiworksource}`Focused explanation fixtures <tests/unit/lra_soi_explanation_tests.cpp>`
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
| §3.3.1 / FMCAD §V-A: integrate sorted crossings | `soi_select`; integrate before slope updates; exact rational/delta arithmetic; $O(p\log p)$ sorting for $p$ events | Implemented baseline; tie retention and coefficient reuse still open |
| Figure 3.4: entering and leaving score | `SoiSelection`; zero-score entering tie is by ID | Reference-selector correction planned |
| Figure 3.3: error-row conflicts before and after updates | `check`, `build_conflict`; sufficient selected bounds must independently refute | Early scans planned; focused external refutation gate added |
| §3.3.2 / §V-C: termination | `check`; finite prefix then Bland | Defensive fallback retained, with prefix/fallback pivot counters |
| §3.3.3 / §V-D: short columns, stalled progress | `Tuning`, `soi_select`; exhaust remaining candidates before declaring blocked | Separate profile planned; shortlist/stall constants require pilot qualification |
| §3.3.4: constant-time breakpoint conflict preference | Tableau-owned activity counts, updated by status/sign changes | Planned; cache must match slow recomputation after rollback |
| §3.3.5 / §V-B and Figure 3.6 | `restricted_soi_row`, conflict builder and minimizer; nonempty blocked certificate | Builder and historical reducers implemented; row-growing arm planned |
| Thesis §3.4 / FMCAD §VI | Reproducible process measurements, qualified answers and separate SAT-model validation | Historical audit only; no new performance acceptance yet |
