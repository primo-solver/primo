# Derived arithmetic bounds, explanations and new atoms

**Status: Open lead. Owner: unassigned.**

Investigate retaining arithmetic consequences that have no SAT naming literal,
using them in later deductions, and explaining their use at the SAT boundary.
Separately investigate creating useful bound atoms during search. These are
different extensions; neither is implemented as a general mechanism in Primo.
The question is whether either improves propagation or learned explanations
enough to justify its proof storage, arithmetic and integration costs.

## Current behavior and the missing operation

A SAT literal names an inequality, not merely its arithmetic variable. Suppose
`p` names $x\le2$, `q` names $y\le5$, and a registered atom `t` names
$x+y\le8$. With row propagation enabled, the theory can compute $x+y\le7$
and explain `t` directly by $\neg p\lor\neg q\lor t$. The value 7 needs no
Boolean name for this deduction. Once SAT assigns `t`, its bound 8 can enter
the tableau. The calculation does not install a separate bound 7 that can feed
another propagation. If no relevant registered atom exists, this calculation
does not by itself create an external propagation.

The ordinary [LRA implementation](../implementation/lra.md#propagate-and-explain-consequences)
already has the following pieces:

| Current owner | Implemented state and boundary |
| --- | --- |
| {source}`LraTableau <src/theory/arithmetic/lra_tableau.hpp>` | Owns current numerical bounds, their literal reasons and rollback records. It has no general arena of internally derived constraints with proof-parent handles. |
| {source}`PreparedLraTheory and OnlineLraPropagator <src/smt/solver_engines.cpp>` | Preparation maps registered atoms to bounds. Row helpers return an implied value together with the selected premise reasons; the online propagator proves existing atoms and retains delivered reasons for SAT. |
| {source}`ExternalPropagator <src/sat/ipasir_up.hpp>` | Supplies clauses for external propagations and conflicts. This Boolean interface does not give the theory a trail of concrete arithmetic decisions. |

Calling these calculations "unnamed propagations" obscures the distinction.
Use **computed intermediate bound** for the current calculation and
**internally installed derived bound** for the proposed persistent state.
Auxiliary tableau variables and the
[temporary assumptions in equality probes](../papers/conflict-explanation-minimization.md#private-bounds-and-future-provenance)
are separate cases.

## Primary readings and their implemented scope

### King: derived constraints inside a conventional SMT solver

Tim King, *Effective Algorithms for the Satisfiability of Quantifier-Free Formulas
Over Linear Real and Integer Arithmetic*, PhD thesis, NYU, 2014
{cite:p}`king2014`. The relevant chapter is **Chapter 2, "Simplex for
Satisfiability Modulo Theories"**. Read **§2.2.4, "Constraints", printed
pp. 61–62**, with §§2.2.3 and 2.2.5 for variable and assertion handling.
[Primary chapter location](https://cs.nyu.edu/media/publications/king_tim.pdf#page=70).
This reading scope does not claim a review of the entire thesis.

Section 2.2.4 distinguishes assertions received from the theory engine from
constraints derived inside arithmetic. Its constraint representation contains
a tableau variable ID, a relation and a delta-rational threshold. Each proved
constraint has an explanation: antecedent constraints, or an assertion origin.
An assertion index records when a constraint is available as an asserted literal.
Every antecedent must already have an explanation when a derived explanation
is attached. Explanations and assertion indices are SAT-context dependent.

`Regress2Assertions`, rule (2.15), replaces a constraint lacking an assertion
index by its explained antecedents. Repeating the rule reaches assertions that
can appear in an external conflict. A propagated constraint can re-enter as an
assertion, so regression may stop there while preserving its earlier proof for
other explanation requests. This is an explanation graph over arithmetic
constraints; it does not require a SAT name for every intermediate consequence.
The chapter describes depth-first traversal and a constraint manager that
backtracks explanations and collects unused constraints. It does not establish
minimum-cardinality explanations or a constant-cost regression operation.

**Primo status: open design.** The current direct reasons are not this general
constraint manager. A proposed theory-owned proof arena would borrow this
separation of constraint identity, proof and assertion availability. The
[existing survey](../papers/conflict-explanation-minimization.md#king-ordinary-simplex-conflict-strengthening-and-provenance)
also covers **§2.2.9, printed pp. 74–76**: choosing weaker sufficient bound
premises while preserving a numerical conflict. That is a separate operation
from regression, SAT clause minimization and Chapter 3's SOI row reduction.

### Feydy–Stuckey: create bound literals when needed

Thibaut Feydy and Peter J. Stuckey, *Lazy Clause Generation Reengineered*,
CP 2009, pp. 352–366 {cite:p}`feydy2009lazy`.
Read **§§3.1–3.2**, especially §3.2's **list encoding**.
[Primary paper](https://people.eng.unimelb.edu.au/pstuckey/papers/cp09-lc.pdf#page=6).

This is a finite-domain integer solver. Propagators explain domain reductions
to an integrated SAT solver. The list encoding creates literals such as
$[x\le d]$ when propagation or explanation needs them. It locates the nearest
existing thresholds on either side and adds the monotonic implications linking
the new literal to those neighbors. The representation therefore needs current
domains, ordered threshold-to-literal maps, propagation scheduling and clausal
reasons. The paper states that encoding size is linear in generated literals;
bound-update work follows the generated literals fixed by the change.

For the running example, a solver following this naming policy could create
`u` for $x+y\le7$ when it becomes useful, then expose
$\neg p\lor\neg q\lor u$ to SAT. Further deductions could use `u` as a compact
premise. This is an analogy for Primo, not a claim that the finite-domain paper
implements a complete real-arithmetic procedure for arbitrary linear sums.
Adapting it to unbounded Real thresholds needs its own generation policy and
progress argument. Ordered implications alone do not guarantee termination.

**Primo status: open design.** On-demand observation support in the SAT adapter
is not an implemented arithmetic atom-generation policy. Such a policy would
need SMT-owned term creation, abstraction, preparation and observation; the
arithmetic solver must not create a SAT variable containing a term.

### De Moura–Jovanović: generate explanations during model construction

Leonardo de Moura and Dejan Jovanović, *A Model-Constructing Satisfiability
Calculus*, VMCAI 2013, pp. 1–12 {cite:p}`demoura2013mcsat`.
Read **§§2.2–2.3**, including the theory search/conflict rules, linear-arithmetic
projection, Example 3, and **Theorem 1** with its finite-basis premise.
[Primary paper](https://leodemoura.github.io/files/mcsat.pdf#page=5).

The trail includes concrete theory assignments $x\mapsto\alpha$ as well as
decided and propagated literals. An explanation must be a valid theory clause
that is false in the conflicting partial model. It may contain new literals
from a permitted finite basis. For linear arithmetic, §2.3 constructs
explanations by eliminating unassigned variables with Fourier–Motzkin
elimination. Thus conflict analysis can discover an inequality absent from the
input rather than merely select a subset of existing bound literals.

For example, let the constraints be $z\ge0$ and $x+z\le7$. A tentative model
assignment $x\mapsto8$ leaves no possible value for $z$: the second constraint
requires $z\le-1$, contrary to $z\ge0$. Adding the inequalities
$-z\le0$ and $x+z\le7$ eliminates $z$ and gives $x\le7$. The explanation

$$
\neg(z\ge0)\lor\neg(x+z\le7)\lor(x\le7)
$$

is theory-valid by the elimination step above. On the mixed MCSAT trail, the
first two inequalities are already asserted true and $x\mapsto8$ makes the new
conclusion false, so all three disjuncts of the explanation are false. This is
falsity under the mixed trail, not a total arithmetic model satisfying the
premises. The inequality $x\le7$ excludes the whole invalid region $x>7$,
rather than only the attempted value 8. This example instantiates the paper's
projection idea; it is not a Primo execution trace.

The required state includes a mixed Boolean/theory trail, partial evaluation,
feasibility checks, projection explanations and a backjump policy. Theorem 1's
termination result assumes a finite-basis explanation function; arbitrary new
Real thresholds do not inherit that result. The paper is a calculus and
correctness argument, not a guarantee that projection, explanation storage or
search is cheaper than the current simplex path.

**Primo status: research alternative.** Primo currently delegates Boolean search
through IPASIR-UP; it has no integrated MCSAT trail or projection-based conflict
analysis. Treat MCSAT as a separate architectural study. Its suitability for new
inequalities is motivation to investigate, not evidence of an easier or faster
implementation in Primo.

## Representation and proof obligations before implementation

The following is a proposed checklist, not a description of existing C++ types:

| Concern | Proposed owner and required invariant |
| --- | --- |
| Internal constraint and proof identity | `theory/arithmetic/` owns context-aware constraint records and antecedent handles. Handles must stay valid across storage growth; derivations retain the historical premises that justified their value. |
| Soundness and acyclicity | Every derived bound follows from its recorded antecedents using exact arithmetic, including strict delta-rational thresholds. Parents must precede the new proof. Replacing a live bound must not make its proof depend on itself. |
| SAT-visible leaves | `smt/` maps available asserted or propagated theory literals to the Boolean abstraction. Regression returns a valid clause over those leaves; internal handles and temporary sentinel reasons cannot escape. |
| Lifetime and rollback | Theory decision contexts and assertion scopes restore active bounds and proof availability. Retain every reason SAT may still request; clause forgetting and proof-node reclamation have separate lifetime conditions. |
| New atoms | `smt/` creates core terms through the term manager, registers/prepares atoms and coordinates SAT observation. `sat/` continues to handle Boolean variables and clauses, with backend details behind its adapter. |
| MCSAT scope | Specify ownership of theory-value decisions and a sound projection/progress argument before changing the Boolean/theory search boundary. A larger atom vocabulary alone is not MCSAT. |

For an acyclic proof graph with visited-node marking, a traversal of the
reachable proof costs $O(V+E)$ graph operations, plus exact-arithmetic checks
and clause construction. This is an engineering bound for that traversal,
not a bound on generating constraints, finding the best explanation or MCSAT
search. Expanding to a tree can repeat shared subproofs, and enumerating Real
thresholds has no finite-domain bound without additional assumptions.

## Investigation, tests and rejection criteria

First specify whether the candidate retains internal consequences, creates new
atoms, or changes the search calculus. Keep those experiments separate. For
already SAT-visible reasons, compare with the backend's final learned clauses:
[SAT minimization and all-UIP shrinking](../papers/conflict-explanation-minimization.md)
can already traverse supplied reasons. A theory-only graph reduction does not
establish that the backend missed it.

Required examples are the computed-7/registered-8 case above; a two-step internal
bound chain that reaches a conflict; shared antecedents; strict and negative
coefficient bounds; a propagated bound re-entering as an assertion; and
backtrack/reassignment after a stronger bound replaced an older one. Check
every generated theory lemma independently. A new-atom experiment also needs
equivalent thresholds, opposite polarities, fresh observation and scope-removal
tests. An MCSAT prototype needs the projection example and the finite-basis
progress argument, not only successful runs on finitely many examples.

Use declared QF_LRA development inputs and the existing stratified sample,
followed by the supported real corpus. Before execution, freeze the baseline,
candidate policy, input hashes and limits. Run solver qualification, fuzzing,
timings and independent SAT-model replay on SLURM; model validation remains
separate from timed runs. Measure installed internal bounds versus computed
candidates, reused derivations, new atoms, live proof storage, explanation work,
theory-clause and final learned-clause sizes, LBD, solved counts and PAR2. Here
LBD counts the distinct decision levels represented in a learned clause.

Reject incorrect lemmas, stale/cyclic reasons, missing rollback, invalid models
or reference disagreements. Reject an implementation whose extra arithmetic,
proof storage or atoms cost more than their search benefit, or whose claimed
reduction is already obtained by the backend. No such implementation,
performance improvement or default change is claimed by this lead.
