# EUF And Congruence Closure

Primo's first theory target is QF_UF. The current implementation is a small
lazy EUF solver over `core::TermId` values, not yet the incremental
explanation-producing solver we ultimately want.

## Current Solver Path

SMT-LIB assertions are parsed into hash-consed terms. Boolean structure is
clausified into SAT clauses. Equality atoms and Boolean predicate applications
are tracked as theory atoms.

The lazy QF_UF loop in `src/smt/check_sat.cpp` is:

```text
clausify assertions
loop:
  call CaDiCaL
  if SAT says UNSAT, return UNSAT
  read assigned theory atoms
  rebuild congruence closure for the assignment
  if all assigned disequalities are consistent, return SAT
  add a blocking theory-conflict clause and repeat
```

The current conflict clause is justified by the explained congruence closure.
Reasons are sound but not guaranteed minimum; the optimized proof-forest
structure from the fast congruence-closure literature is still deferred.

`--euf-prop=off|model|online`,
`--euf-prop-budget=auto|unlimited|N`, and
`--euf-prop-delivery=auto|unlimited|N` are the public option shape for
propagation work. The default is `off`.

For compatibility with the predecessor benchmark command, Primo also accepts
`--prop-delivery-budget N`; `0` means unlimited delivery. `--prop-interval N`
and `--prop-assign-threshold X` are accepted for online propagation experiments.
The tuned interesting-set profile sets them to `32` and `0.25` respectively.

`model` is implemented as a conservative post-model lemma learner. After each
SAT model, Primo rebuilds explained EUF closures that omit one candidate theory
atom at a time. If the remaining assigned equalities and Boolean predicate facts
already imply the candidate's current value, Primo adds a SAT clause from the
explained premise literals to the candidate literal. Reasons come from the
explicit explanation graph and are sound, but not guaranteed minimum.

`online` connects a CaDiCaL external propagator through the SAT adapter. It
observes only EUF-relevant SAT variables: equality atoms and Boolean predicate
applications. The propagator tracks assignments and backtracking notifications,
then asks the explained closure for entailed unassigned theory literals and
their reason clauses. This is an initial online subset; it still reconstructs
closures during callbacks and is experimental/unsafe for headline benchmark
numbers until exact `interesting.html` and fuzz runs show zero crashes and zero
wrong answers. It does not choose decisions and it does not inject arbitrary
external clauses.

## Term Interface

The equality solver receives only `core::TermId` values. It inspects structure
through `core::TermManager::get`; it does not own term memory and it does not
refer to SMT-LIB syntax.

Syntactic equality and EUF equality are distinct:

- equal `TermId` values mean the terms are structurally identical after
  hash-consing and local simplification;
- EUF equality is the congruence relation computed from assigned equality
  literals.

## Congruence Closure

`src/theory/equality/congruence_closure.*` stores:

- a `UnionFind` over term ids;
- `terms_seen_`, the registered terms and their children;
- `seen_`, a term-id-indexed membership bitmap;
- a dirty flag used by saturation.

`add_term(t)` recursively registers `t` and its children. `assert_equal(a, b)`
registers both sides and merges their classes. `are_equal(a, b)` saturates the
closure and then compares representatives.

The saturation algorithm is intentionally simple:

```text
while a merge happened:
  for every pair of seen terms:
    if they are not already equal and are congruent:
      merge them
```

Two terms are considered congruent when they are both constants or applications
with the same symbol and their corresponding children are already equal. This is
quadratic over seen terms and rebuilds from each SAT assignment; it is adequate
for the scaffold but not the final QF_UF engine.

`src/theory/equality/explained_congruence_closure.*` is the first
explanation-capable EUF state. It keeps a union-find plus an explicit equality
edge graph. Explicit assigned equalities add edges labelled by the corresponding
SAT blocking literal. Congruence merges add edges labelled by the union of child
equality explanations. This is not the optimized proof-forest structure from
Nieuwenhuis and Oliveras, but it provides independently checkable reason
literals for the current lazy conflict clauses and model-time propagation
lemmas. The closure borrows term storage from `core::TermManager`; all terms it
will inspect must already exist when the closure is constructed.

## EufSolver

`src/theory/equality/euf_solver.*` is a batch checker:

- `assert_equal(lhs, rhs)` records an equality;
- `assert_distinct(lhs, rhs)` records a disequality;
- `is_satisfiable()` rebuilds a `CongruenceClosure`, applies equalities, and
  rejects the assignment if any recorded disequality becomes equal.

The lazy SMT loop uses `ExplainedCongruenceClosure` so conflict and model
propagation clauses carry reason literals rather than all assigned theory
premises.

## Preprocessing Interaction

`--qf-uf-preprocess=strong` runs before QF_UF solving. It appends bridge
equalities, finite-domain at-most-one clauses, and value-precedence assertions
when their recognizers can prove the required local conditions. These generated
assertions are then handled by the same Boolean clausifier and
congruence-closure loop as ordinary user assertions. `--uf-symmetry-breaking`
and `--eq-bridge` remain available as narrower experimental switches.

See [](preprocessing.md) for the finite-domain symmetry-breaking design.

## Implemented And Deferred

Implemented:

- union-find over `TermId`;
- recursive term registration;
- congruence closure for constants and applications;
- equality and disequality checking;
- explanation graph for explicit and congruence equality reasons;
- lazy Boolean/EUF integration with CaDiCaL;
- Boolean predicate applications participating in EUF by equality to
  `true`/`false`;
- model-time EUF propagation lemmas with deterministic inspection and delivery
  budgets;
- experimental online EUF propagation through CaDiCaL observed-variable
  callbacks;
- basic QF_UF preprocessing by default;
- strong preset with finite-domain AMO clauses and default-off UF symmetry
  breaking.

Deferred:

- incremental backtracking congruence closure;
- use lists and lookup tables from the fast congruence-closure literature;
- optimized explanation-producing proof forests;
- richer online conflict-clause injection;
- minimal theory-propagation reasons;
- proof output for preprocessing-generated lemmas;
- model construction.

## Paper Traceability

Nieuwenhuis and Oliveras, *Fast Congruence Closure and Extensions*, motivates
the EUF direction and the eventual data structures: use lists, congruence
lookup, pending merges, and explanations. Primo currently implements only the
basic ground congruence behavior and a simple explanation graph, not the
optimized incremental algorithm.

Déharbe et al., *Exploiting symmetry in SMT problems*, and Reger and Suda,
*Using SAT and SMT Solvers for Finite Model Finding with Sorts*, motivate the
default-off finite-domain symmetry-breaking preprocessing pass described in
[](preprocessing.md).
