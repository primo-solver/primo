# Linear real arithmetic

Primo's linear real arithmetic solver checks whether the inequalities selected
by SAT have a common solution. It maintains exact values satisfying a fixed
system of linear equations, then repairs violations of the selected bounds.
Failure produces a clause explaining which Boolean assignments cannot hold
together. The [DPLL(T) chapter](dpllt.md) describes how SAT uses that clause.

The implementation follows Dutertre and de Moura's
[Integrating Simplex with DPLL(T)](https://yices.csl.sri.com/papers/sri-csl-06-01.pdf),
with substantial engineering described below: shared polynomial rows, direct
bounds, small-rational arithmetic, sparse pivoting, occurrence indices, deferred
checks and bounds-only rollback. Section numbers for this source refer to the
**technical report**, whose numbering differs from the CAV paper.

```{admonition} TODO: Write the integer-arithmetic companion chapter
:class: note

Extract the current algorithms from the
{source}`integer development record <docs/qf-lia-branch-and-bound.md>`:
branch-and-bound, GCD checks, Gomory cuts, patching, and conflict generalization.
Explain their order, guards, budgets, and concrete incompleteness examples,
with links to the pure and combined integer routes. Keep dated experiments
separate from the current algorithm and its limitations.
```

## Representation and ownership

The input to arithmetic preparation is typed core terms plus their Boolean
abstraction. The tableau itself receives variable IDs, exact bounds and reason
literals. It does not parse SMT-LIB or inspect the SAT solver's internals.

| Representation | Owner, identity and lifetime |
| --- | --- |
| {source}`LinearPolynomial <src/theory/arithmetic/linear_polynomial.hpp>` | Owns a constant and a map from `TermId` to nonzero rational coefficients. Repeated occurrences add; exact cancellation removes an entry. |
| {source}`LraAtom <src/theory/arithmetic/lra_atom.hpp>` | Owns a normalized polynomial, relation `Le`, `Lt` or `Eq`, and right-hand-side rational. It is a value, not a SAT literal. |
| {source}`PreparedLraTheory <src/smt/solver_engines.cpp#L3332-L3388>` | Borrows the term manager and owns the base tableau, term-to-variable map, polynomial-to-slack map, and prepared atom vector. Each atom retains its own threshold and orientation. |
| {source}`LraVarId <src/theory/arithmetic/lra_tableau.hpp#L17>` | A numeric index local to a tableau and its structurally matching copies. It denotes an input leaf or an auxiliary polynomial value, independently of core and SAT IDs. |
| {source}`LraTableau <src/theory/arithmetic/lra_tableau.hpp>` | Owns assignments, bounds with reasons, basic/nonbasic membership, sparse rows, reverse occurrences, violated-variable set and undo records. |
| {source}`OnlineLraPropagator <src/smt/solver_engines.cpp#L6972-L7063>` | Borrows prepared data and owns a working tableau copied from the base, decision-level checkpoints, pending consequences and retained reasons. It reads the shared SAT assignment view. |

The term manager must outlive preparation and any interpretation of its IDs.
Tableau IDs survive pivots and rollback; being basic is a mutable property of
the variable. Structural insertion allocates vectors and maps, and pivots can
replace entire rows. References into these containers must not survive such
mutations. Copying a tableau preserves its numbering but creates independent
mutable arithmetic state.

The usual preparation installs equations before search. `append_atom` can
extend the prepared base between SAT calls, for example for a new interface
equality. This is separate from asserting a bound on the live tableau;
`add_row` is not a trailed search operation.

## From atoms to bounds

{source}`LinearPolynomial::from_term <src/theory/arithmetic/linear_polynomial.cpp#L12-L50>`
collects additions and constant scaling. Core `Times` stores a rational
coefficient and one child, so it represents linear scaling. Arithmetic leaves
are identified by their `TermId`; any uninterpreted application needs the
surrounding combination machinery to enforce its function semantics.
Unlowered `ite`, `div`, `mod` and `abs` raise an exception at this boundary.

{source}`normalize_atom <src/theory/arithmetic/lra_atom.cpp#L10-L37>` subtracts
the right side and moves the constant to the threshold. Preparation then
orders variables by `TermId` and divides every coefficient and the threshold
by the first coefficient, making that coefficient one. A negative divisor
reverses the inequality. This canonicalization
identifies proportional polynomials; it does not merge their SAT variables.

For example, these predicates can share the variable `s = x + y`:

| Predicate | Bound when true |
| --- | --- |
| `x + y <= 5` | `s <= 5` |
| `2*x + 2*y <= 10` | `s <= 5` |
| `-x - y <= -5` | `s >= 5` |
| `x + y <= 7` | `s <= 7` |

The thresholds and SAT reasons stay separate, including when a predicate is
assigned false. A single-variable polynomial instead becomes a direct bound:
`2*x <= 10` needs `x <= 5`, with no auxiliary variable or equation.
{source}`PreparedLraTheory::append_atom <src/smt/solver_engines.cpp#L3411-L3514>`
implements both savings: bounds on a single variable use that variable
directly, while bounds on proportional compound polynomials share one
auxiliary variable and its defining row. This keeps the tableau smaller and
puts related bounds on the same variable, where they can be compared directly.

Monniaux's [CAV 2009 paper](https://arxiv.org/abs/0904.3525), §2.1, describes
both ideas, but chooses a different standard form for the coefficients.
The paper clears denominators and removes common integer factors, choosing
the sign so the first coefficient is positive. Primo divides by the first
coefficient, so it becomes exactly one; the remaining coefficients may be
fractions. For example, assuming `x` comes before `y` in the variable order:

| Input | Paper's integer form | Primo's form |
| --- | --- | --- |
| `4*x + 6*y <= 10` | `2*x + 3*y <= 5` | `x + (3/2)*y <= 5/2` |

The paper's coefficients `2` and `3` have greatest common divisor one: this
is what “coprime integer coefficients” means here. Both forms express the
same inequality and give proportional polynomials a common representation.
In both cases the threshold must be scaled by the same factor as the
coefficients.

Leading-coefficient normalization gives Primo a simple way to share rows.
{source}`canonical_polynomial <src/smt/solver_engines.cpp#L3296-L3313>` uses
one coefficient as the divisor for every term and the threshold, skipping
those divisions when it is already one. It needs no separate
common-denominator or greatest-common-divisor computation across the vector;
individual rational operations still reduce their results.

The tradeoff is that this can introduce fractions into an initially integer
row. Coprime-integer normalization avoids those initial fractions, but clearing
input denominators can enlarge coefficients, and later pivots can introduce
fractions again. There is no recorded direct performance comparison of the
two normal forms; the [normalization review](../review.md#why-leading-coefficient-normalization-was-chosen)
records the original row-sharing objective and available evidence. Choosing
an initial normal form and maintaining integer rows throughout pivoting are
separate design questions.

Primo also has a separate, optional
{source}`early atom canonicalizer <src/smt/preprocessing.cpp#L499-L599>` that
uses coprime integer coefficients in core terms, so equivalent predicates can
share a SAT atom. It is
{source}`off by default <src/smt/check_sat.hpp#L321-L324>`. Even when enabled,
tableau preparation still applies leading-coefficient division. The
{source}`early canonicalization note <docs/papers/lra-atom-canonicalization.md>`
describes that separate transformation and its evaluation.

Real equality is lowered to two inequalities before this path. Negating it
gives a Boolean choice between strict inequalities, handled by SAT. The
low-level `Eq` case's rejection of a false equality is therefore not a
restriction on surface Real disequality. See the
{source}`equality-lowering design <docs/arithmetic.md#L123-L166>` and
{source}`bound assertion boundary <src/smt/solver_engines.cpp#L3222-L3254>`.

## Exact numbers and strict bounds

{source}`util::Rational <src/util/rational.hpp#L13-L47>` uses an inline signed
64-bit numerator and denominator for values fitting its small representation.
Operations use checked wide intermediates and fall back to GMP rationals.
Results that fit return to the small representation: equal values must compare
and hash alike regardless of the arithmetic path that produced them.
This is the two-tier representation discussed by Monniaux §4 and Griggio
§2.5.2, with 64-bit rather than Monniaux's 32-bit small values. The paper's
floating-point solver and forced-pivoting algorithm remain an
[open lead: floating-point-guided basis selection](../leads/lra-floating-point.md).

The [compact rational storage lead](../leads/rational-storage.md) investigates
overlapping the small numerator and owned big-number pointer. It includes a
reproducible layout probe and the remaining ownership and evaluation questions.

A strict bound uses an exact symbolic infinitesimal.
{source}`DeltaRational <src/theory/arithmetic/lra_tableau.hpp#L24-L56>` stores
two rationals `(q, k)`, interpreted as `q + k*δ`, ordered lexicographically.
There is no floating-point tolerance. For an unflipped Real atom:

| Atom | Assigned true | Assigned false |
| --- | --- | --- |
| `t <= c` | upper bound `(c, 0)` | lower bound `(c, 1)` |
| `t < c` | upper bound `(c, -1)` | lower bound `(c, 0)` |

The flipped orientation reverses the bound direction. All cases are centralized
in {source}`bound_assertion <src/smt/solver_engines.cpp#L3119-L3187>`.
Integer tightening is a separate branch of this function; the infinitesimal
interpretation here is for Real arithmetic. The basis is Dutertre–de Moura
§3.3, including the argument that a sufficiently small positive rational
can replace `δ` in a satisfying model.

## Sparse rows and reverse occurrences

A basic variable is defined by a row `x_b = Σ a_bj*x_j` over nonbasic
variables. The row contains only nonzero coefficients, in increasing variable
order, with one entry per variable. The actual entry is
{source}`RowEntry {var, coeff, occ_slot} <src/theory/arithmetic/lra_tableau.hpp#L290-L295>`.
Its reverse link satisfies:

```text
For entry e in rows_[b]:  occurs_in_[e.var][e.occ_slot] == b
```

`occurs_in_[v]` lists the basic rows containing `v`. Its order is immaterial.
`occ_slot` locates the column occurrence, not the row entry. Removing an
occurrence swaps the last column element into its place and repairs that
element's row backlink using binary search. Thus
{source}`unlink_occurrence <src/theory/arithmetic/lra_tableau.cpp#L2000-L2020>`
avoids scanning the column, but repair is logarithmic in the moved row's
length. It is not the constant-time unlink of a doubly linked matrix.

```{figure} ../../docs/_static/lra-tableau-occurrence-index.svg
:alt: Sorted tableau rows linked by occ_slot to unordered column occurrence lists, with swap-pop removal and backlink repair.

Sorted rows and their reverse occurrence index. Each row entry's `occ_slot`
locates its entry in a column list; `occ` abbreviates `occurs_in_`.
The editable source is `docs/figures/lra-tableau-occurrence-index.tex`.
```

The distinction among the thesis representations matters. King §2.2.7 uses
unsorted, doubly linked rows and columns, with a dense scratch map for row
addition. Griggio §2.5.2 discusses sorted contiguous coefficient arrays.
Bromberger §7.1.4 uses sorted doubly linked rows, unordered columns and a
common denominator per row. Primo uses sorted vectors, individual rational
coefficients and the indexed occurrences above. These designs support the
same sparse operations with different allocation and removal costs.

{source}`pivot <src/theory/arithmetic/lra_tableau.cpp#L143-L315>` merges sorted
rows, drops exact cancellations and updates occurrences only when membership
changes. The coefficient merge is linear in the two row lengths; occurrence
repairs and arbitrary-precision arithmetic add their own costs. Updating a
nonbasic assignment visits its dependent rows through `occurs_in_`, but the
current {source}`update <src/theory/arithmetic/lra_tableau.cpp#L97-L141>` still
searches each affected row for the coefficient. The occurrence index is not
a direct pointer to that coefficient.

The 32-bit occurrence slot assumes fewer than `2^32` occurrences in a column.
The representation and its capacity assumption are distinct from SAT literal
encoding. Hash-consing core terms does not bound either quantity.

## Repairing an assignment

Dutertre–de Moura §3.2 separates three obligations:

1. Every assignment satisfies the tableau equations.
2. Every nonbasic variable satisfies its current bounds.
3. After a successful `check()`, basic variables satisfy their bounds too.

{source}`assert_upper and assert_lower <src/theory/arithmetic/lra_tableau.cpp#L47-L95>`
tighten a bound and save its predecessor on the undo trail. A weaker bound
does nothing. A new upper bound below the current lower bound, or conversely,
returns an immediate conflict without installing the contradictory bound.
If a nonbasic value falls outside a new bound, `update` moves it to that bound
and updates dependent basic values. Basic variables may then need repair.

{source}`check <src/theory/arithmetic/lra_tableau.cpp#L1711-L1890>` selects a
violated basic variable and a nonbasic variable that can move in the required
direction. It pivots the latter into the basis, then moves the now-nonbasic
leaving variable to its violated bound. For example, pivoting `y` into the
row `s = x + y` yields `y = s - x`; setting `s` to its target updates `y`
and every other row depending on `s`. Each pivot preserves the equations.
If no eligible entering variable exists, current bounds prevent repairing
that row, and the solver explains a conflict.

The solver's default is **Sparse**, although a directly constructed
`LraTableau` defaults to Bland. The
{source}`solver options <src/smt/check_sat.hpp#L282-L294>` select the policy:

| Policy | Implemented choice |
| --- | --- |
| Default sparse leaving | Shortest violated row; greatest violation breaks ties. |
| Sparse entering | Fewest column occurrences among eligible candidates considered; unit coefficient, then variable ID break ties. By default, compare at most 16 eligible candidates. The scan may visit more entries to find them. |
| Optional least-violated leaving | Griggio §2.5.3: smallest violation first, then shortest row. This overrides the default leaving policy under Sparse. |
| Optional Dantzig | Greatest violation first; largest absolute entering coefficient, with variable IDs breaking ties. |
| Bland | Smallest eligible variable IDs on both sides; also the termination fallback. |

Monniaux §3 also describes preferring shorter rows and fewer column
occurrences to reduce pivot cost, in the context of forced pivoting. Primo's
sparse policy applies that rationale to ordinary feasibility search;
it does not import the surrounding floating-point/forced-pivot procedure.
King §2.2.12 provides the direct comparison for minimum-column selection
and eventual Bland fallback in SMT.

Heuristic selection eventually switches to Bland within each `check()` call.
The default threshold is `4 * variable_count + 64`; this is a tuning choice,
not a bound on the total pivots needed. King §2.2.12 explains why eventual
Bland selection matters. The selection scan must still find an eligible
candidate beyond an unproductive row prefix; otherwise a cost cap could
become an incorrect conflict. See
{source}`select_entering_variable <src/theory/arithmetic/lra_tableau.cpp#L897-L982>`.

## Follow a strict conflict

Let SAT assign `p`, `q` and `r` true for:

```text
p: x + y <= 2       q: x >= 2       r: y > 0
```

Preparation supplies `s = x + y`, initially with all values zero. The
following is a trace of theory operations, not a prescribed SAT search order:

| Operation | Assignment and bounds |
| --- | --- |
| Assert `p` | Install `s <= 2`, carrying reason `¬p`. |
| Assert `q` | Install `x >= 2`, carrying `¬q`; update `x = 2`, hence `s = 2`. |
| Assert `r` | Install `y >= δ`, carrying `¬r`; update `y = δ`, hence `s = 2 + δ`. |
| Check the violated row | `s` is too high. Decreasing either `x` or `y` is blocked by its lower bound, so no repair pivot is possible. |

The row and the two lower bounds imply `s >= 2 + δ`, contradicting `s <= 2`.
The resulting clause is `¬p ∨ ¬q ∨ ¬r`.

Primo stores **the negation of the asserting SAT literal** with each bound,
at the {source}`assignment boundary <src/smt/solver_engines.cpp#L7302-L7310>`.
{source}`build_conflict <src/theory/arithmetic/lra_tableau.cpp#L433-L446>`
collects the violated bound's reason and the row bounds that block movement.
It returns clause literals directly; the caller must not negate them again.
This is the row-based explanation of Dutertre–de Moura §3.2.2. The returned
object is a SAT clause, not an exported proof certificate with coefficients.

## Propagation and online checking

Default propagation compares predicates bounding the same tableau variable.
For instance, `p: s <= 2` entails `t: s <= 5`, with reason clause `¬p ∨ t`.
{source}`scan_for_propagations <src/smt/solver_engines.cpp#L7554-L7600>` visits
the prepared `atoms_by_bound_var` index and checks both polarities of each
unassigned atom. It uses bounds, not accidental equalities in the current
candidate assignment. This is the cheap propagation of the technical report's
§3.2.5.

Row propagation is implemented but off by default. It can combine bounds
on several row terms, and the optional reverse direction can bound a child
from the row's basic-variable bound and its other terms. Every required bound
must exist; row-size and fanout caps may decline work. See
{source}`implied row bounds <src/smt/solver_engines.cpp#L6837-L6970>` and
{source}`propagation options <src/smt/check_sat.hpp#L245-L269>`.
Neither mode performs complete arithmetic consequence discovery.

```{admonition} TODO: Work through row propagation
:class: note

Derive a bound from a concrete row and signed coefficients, showing which
lower or upper bounds supply each contribution. Include the reverse direction
and the resulting SAT explanation clause. Show when a missing bound or a
configured cap prevents propagation; connect each step to the code above.
```

Bjørner and Nachmanson's [*Arithmetic Solving in Z3*](https://doi.org/10.1007/978-3-031-65627-9_2)
(CAV 2024), §3.3, makes the same
direct/indirect propagation distinction. Primo's row mode queries implied
bounds to explain existing Boolean atoms; this does not mean it implements
every bound-refinement policy described for Z3.

The online propagator applies each assigned bound immediately but defers
simplex repair until a consistency check is needed. This batches `check()`
calls, not the bound updates themselves; deferred assignment updates remain
a different proposal. An immediate bound conflict must survive the deferred
check because the rejected bound never entered the tableau. See
{source}`notification and deferred repair <src/smt/solver_engines.cpp#L7310-L7413>`.
Delivered propagation reasons remain available until their assignments are
removed, following the shared-assignment protocol in [DPLL(T)](dpllt.md).

## Rollback and model values

`push()` returns an undo-trail position. With the default sparse policy,
{source}`pop <src/theory/arithmetic/lra_tableau.cpp#L1892-L1962>` restores
bounds and rebuilds the set of violated basic variables. It retains the
current basis and assignment. Relaxing bounds preserves nonbasic feasibility,
and the assignment still satisfies the retained equations. Basic feasibility
is established by `check()`, not by the act of popping alone.

This is an implementation choice within the fixed-equation approach. The
technical report §3.2.4 describes saving a successful assignment;
Bromberger §7.1.3 compares that with keeping the current assignment and
rechecking. Primo neither restores an old assignment against a changed basis
nor unconditionally runs `check()` inside `pop()`. SAT decision rollback and
SMT-LIB assertion scopes remain different operations. The latter's reuse
design is described in the {source}`retained LRA session note <docs/assumption-incremental-lra.md>`.

The optional **sum-of-infeasibilities (SOI)** mode has a basis-dependent
objective representing the current violations. Its rollback also restores
assignments, pivoted rows and objective changes. This exception is explicit
in {source}`trails_assignments_and_pivots <src/theory/arithmetic/lra_tableau.hpp#L843>`.
King §§3.2–3.3 and King–Barrett–Dutertre's FMCAD 2013 paper provide the
algorithmic basis; Primo adds a finite fallback to ordinary Bland repair.
The default sparse policy does not optimize an SOI objective.

```{admonition} TODO: Explain the implemented SOI algorithm
:class: note

Add a dedicated subsection or algorithm page defining the violation objective,
its maintained row, breakpoint selection, multi-row conflict construction,
and conflict minimization. Work through a pivot and explain why objective
state needs additional rollback. Use the
{source}`retained SOI description <docs/arithmetic.md#L451-L541>` as an extraction
checklist, verifying each mechanism against the current code and primary
paper. Preserve its opt-in status and distinguish historical measurements
from current qualification.
```

For a satisfying Real model,
{source}`choose_concrete_delta <src/smt/solver_engines.cpp#L3516-L3561>`
finds a positive rational small enough to preserve the assigned bounds and
evaluates `q + k*δ` at that value. Where a crossing point limits `δ`, it
chooses half the smallest positive limit; otherwise it uses one. Mixed
theory model construction additionally preserves required shared-value
orders. Reconstruction of eliminated variables and Boolean values remains
part of the model builder; a feasible tableau alone is not the printed model.

### Equalities for combination

The default QF_UFLRA producer uses four rules based on fixed variables and
matching tableau rows. Their worked examples, bound explanations, and
incompleteness are described in
[the combination chapter](#lra-default-equality-rules). That chapter also gives
the [model-mutation schedule](combination.md#when-model-mutation-runs): the
dedicated Real route attempts scratch diversification before its final
equality-exchange passes and model reconciliation.

Bjørner and Nachmanson's *Arithmetic Solving in Z3*, §3.2, also informs an
optional equality producer for QF_UFLRA. Starting from a feasible tableau,
{source}`model_filtered_unit_equalities <src/theory/arithmetic/lra_tableau.cpp#L1261-L1456>`
groups shared variables by their exact current value. It then looks for
equalities justified by bounds and equations. Two variables fixed to the same
value are equal. For variables related by rows, it checks two shapes:

- `x = y + r`, where every variable in the remainder `r` is fixed by matching
  lower and upper bounds. If `x` and `y` have equal current values, `r` is
  zero and remains zero while those bounds hold.
- `x = s*z + r₁` and `y = s*z + r₂`, where the shared coefficient `s` is
  `1` or `-1` and both remainders contain only fixed variables. Equal current
  values of `x` and `y` establish that the fixed remainders are equal too.

Current values filter candidate pairs; the bounds fixing the remainders
supply the explanations. This avoids constructing the large rational offsets
used by the older producer. The method emits an equality only when it connects
previously separate groups; transitivity supplies the remaining connections.
The surrounding combination solver retains its exact fallback checks.

This is the `model-unit` option. The
{source}`default remains legacy <src/smt/check_sat.hpp#L396-L402>`; pure
QF_LRA calls neither producer. The full-corpus comparisons and the unpromoted
adaptive follow-up are preserved in the
{source}`2024 paper implementation note <docs/papers/arithmetic-solving-in-z3.md#L277-L461>`.
The paper's integer and nonlinear procedures are outside this chapter's scope.

## Further implemented work and its evidence

The following notes explain additional arithmetic mechanisms and preserve
their measurements and rejected alternatives. H1–H10 are identifiers in the
historical heuristics register.

```{admonition} TODO: Give retained arithmetic work permanent explanatory homes
:class: note

For each mechanism below, identify the current algorithm explanation and a
precise dated evidence record. Extract current design rationale from the long
development notes before archiving them. Record configuration, revision,
outcome, and qualification limits in evidence summaries, retaining rejected
alternatives and raw-record links. This inventory must not substitute for
the missing algorithm explanations.
```

| Work already done | Current explanation and retained record |
| --- | --- |
| Direct bounds, shared polynomial rows and small rationals (H1/H2) | Explained above; {source}`design and measurements <docs/qf-lra-heuristics.md#L217-L433>`. |
| Sparse pivot selection and tuning (H3) | Explained above; {source}`pivoting investigation <docs/qf-lra-heuristics.md#L434-L532>` and {source}`tuning controls <docs/tuning-parameters.md>`. |
| Sorted row merging, bounds-only rollback, incremental occurrence updates (H8/H9/H10) | Explained above; {source}`design progression <docs/qf-lra-heuristics.md#L731-L943>`, including the earlier rejected occurrence-maintenance attempt. |
| Fixed-basic-variable elimination (H7) | {source}`try_eliminate_fixed_basic <src/theory/arithmetic/lra_tableau.cpp#L393-L431>` pivots a fixed basic variable out and pins its value. Off by default for pure LRA, on by default for QF_UFLRA; {source}`route defaults <src/smt/check_sat.hpp#L295-L310>` and {source}`design and evidence <docs/qf-lra-heuristics.md#L533-L730>`. |
| Model-based phase hints (H5) | {source}`refresh_model_phase_atom <src/smt/solver_engines.cpp#L7415-L7473>` supplies preferred polarities while SAT keeps variable selection. Off by default; {source}`phase-selection investigation <docs/qf-lra-heuristics.md#L976-L1233>`. |
| SOI and alternative pivot policies | {source}`LraTableau::check <src/theory/arithmetic/lra_tableau.cpp#L1711-L1805>` and the {source}`base-paper deviation map <docs/papers/fast-linear-arithmetic-dpllt.md>` explain these opt-in policies. |
| Equality sharing and model reconciliation | [Theory combination](combination.md), the {source}`equality-sharing design <docs/qf-lra-equality-sharing.md>` and {source}`tableau equality/model interfaces <src/theory/arithmetic/lra_tableau.hpp#L331-L600>`. Model coincidence is not an entailed equality. |
| Preprocessing and experiment history | {source}`monotone elimination <docs/papers/monotone-variable-elimination.md>`, {source}`guarded equality experiments <docs/qf-lra-heuristics.md#L1234-L1347>` and {source}`arithmetic development record <docs/arithmetic.md>`. |

The [published QF_LRA results](../papers/primo-qflra-results.md) are another
part of this evidence trail. The tuned configurations in that paper have
their own settings and measurement scope. Exact table-to-artifact mapping
remains open.

Open investigations include alternative rational libraries, further fill-in
control, batched assignment updates and
[floating-point-guided basis selection](../leads/lra-floating-point.md). The
{source}`heuristics register <docs/qf-lra-heuristics.md>` retains their
hypotheses and the measurements that would support or reject them.

## Sources and implementation status

The implementation uses the following papers, theses, and sections. The
[review record](../review.md#primary-sources-read) identifies the reading copies
and sections checked.

| Source | Role and limits |
| --- | --- |
| Dutertre and de Moura, *Integrating Simplex with DPLL(T)*, SRI-CSL-06-01, 2006, §§3.1–3.3 | Base preparation, repair, conflicts and strict arithmetic. Sparse selection, occurrence layout and the exact rollback policy are Primo's adaptations described above. |
| [Tim King, *Effective Algorithms for the Satisfiability of Quantifier-Free Formulas Over Linear Real and Integer Arithmetic*](https://cs.nyu.edu/media/publications/king_tim.pdf), PhD thesis, NYU, 2014, §§2.2.7, 2.2.12, 3.2–3.3 | Sparse-operation comparison, heuristic termination and SOI. Primo does not adopt the thesis's full linked-matrix representation or every optimized mechanism. |
| [Alberto Griggio, *An Effective SMT Engine for Formal Verification*](https://iris.unitn.it/handle/11572/368265), PhD thesis, Trento, 2009, §2.5 | Exact-number and tableau engineering; the implemented opt-in least-violated rule. Its reported MathSAT heuristic is distinct from Primo's default. |
| [Martin Bromberger, *Decision Procedures for Linear Arithmetic*](https://publikationen.sulb.uni-saarland.de/handle/20.500.11880/28955), PhD thesis, Saarland, 2019, §§7.1.3–7.1.4 | Backtracking alternatives and sparse data structures. The complete equality-basis algorithms of Chapter 5 remain further work; current local equality rules do not implement that complete method. |
| David Monniaux, *On Using Floating-Point Computations to Help an Exact Linear Arithmetic Decision Procedure*, CAV 2009, §§2.1, 3 and 4 | Polynomial normalization, sparse pivot-cost rationale and two-tier rationals. The floating-point acceleration is not implemented. |
| Tim King, Clark Barrett and Bruno Dutertre, *Simplex with Sum of Infeasibilities for SMT*, FMCAD 2013 | Optional SOI mode; see also King's thesis. This does not characterize the default sparse repair loop. |
| Nikolaj Bjørner and Lev Nachmanson, *Arithmetic Solving in Z3*, CAV 2024, §§2, 3 and 6 | Modern comparison for exact sparse arithmetic and propagation; §3.2 is the basis for the optional model-unit equality producer. Integer/nonlinear techniques need their own mappings. |
| Martin Bromberger, Mathias Fleury, Simon Schwarz and Christoph Weidenbach, *SPASS-SATT: A CDCL(LA) Solver*, CADE 2019, §3 and footnote 3 | Theory-model phase recommendations underlying H5. Primo refreshes backend phase hints rather than exposing SPASS-SATT's `GetPhase` callback. |
| Erling D. Andersen and Knud D. Andersen, *Presolving in Linear Programming*, Mathematical Programming 71, 1995, §3.1(iv) | Fixed-variable elimination motivates H7. Primo keeps variables and reasons and pivots fixed basics out; it does not delete columns using the paper's full presolve/restoration pipeline. |

Robert Daniel Brummayer's *Efficient SMT Solving for Bit-Vectors and the
Extensional Theory of Arrays* (PhD thesis, JKU Linz, September 2009),
Chapter 4, informs the grammar-based fuzzing and failure reduction described
in the [contributor correctness guide](../development/correctness.md#fuzzing-and-reduction-sources).

## Invariants exercised by existing tests

| Obligation | Regression or mechanism test |
| --- | --- |
| Exact promotion/demotion preserves arithmetic, equality and hashing | {source}`rational representation and GMP comparison tests <tests/unit/rational_tests.cpp#L272-L383>` |
| A strict boundary cannot be rounded into a satisfying equality | {source}`strict-bound conflict test <tests/unit/lra_tableau_tests.cpp#L177-L197>` |
| Sparse pricing finds candidates and really uses occurrence counts | {source}`sparse selection tests <tests/unit/lra_tableau_tests.cpp#L533-L625>` |
| Heuristic repair eventually invokes the counted Bland fallback | {source}`Bland fallback test <tests/unit/lra_tableau_tests.cpp#L725-L753>` |
| Rows stay sorted; cancelled coefficients lose their occurrences | {source}`row/occurrence tests <tests/unit/lra_tableau_tests.cpp#L1208-L1397>` |
| Rollback preserves equation consistency; SOI restores its extra state | {source}`ordinary rollback tests <tests/unit/lra_tableau_tests.cpp#L238-L416>` and {source}`SOI rollback <tests/unit/lra_tableau_tests.cpp#L867-L904>` |
| Shared polynomials preserve scaled thresholds and flipped strictness | {source}`preparation tests <tests/unit/qf_lra_solver_tests.cpp#L4363-L4450>` |
| Propagation has a Boolean effect and respects configured caps | {source}`same-variable/row propagation tests <tests/unit/qf_lra_solver_tests.cpp#L3268-L3447>` |
| SOI exercises the paper's worked pivot example | {source}`SOI worked-example test <tests/unit/lra_tableau_tests.cpp#L798-L829>` |

Performance investigations additionally use pivot, row-update, term-operation,
Bland-fallback and propagation counters; their publication is visible in
{source}`run_deferred_check_if_needed <src/smt/solver_engines.cpp#L7379-L7413>`.
Correctness qualification still requires the fresh fuzzing and independent
model checks described in the [contributor guide](../development/correctness.md).
