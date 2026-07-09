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

The current conflict clause blocks the whole set of equality premises used for
the rebuilt check. This is correct but coarse. Explanation-minimal conflict
clauses are deferred until the congruence closure module records proof forests.

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

## EufSolver

`src/theory/equality/euf_solver.*` is a batch checker:

- `assert_equal(lhs, rhs)` records an equality;
- `assert_distinct(lhs, rhs)` records a disequality;
- `is_satisfiable()` rebuilds a `CongruenceClosure`, applies equalities, and
  rejects the assignment if any recorded disequality becomes equal.

The lazy SMT loop currently uses `CongruenceClosure` directly so it can build
SAT blocking clauses over assigned theory atoms.

## Preprocessing Interaction

`--uf-symmetry-breaking` runs before QF_UF solving. It appends value-precedence
assertions over equality terms when finite-domain values are provably symmetric
by a syntactic constant-permutation check. These generated assertions are then
handled by the same Boolean clausifier and congruence-closure loop as ordinary
user assertions.

See [](preprocessing.md) for the finite-domain symmetry-breaking design.

## Implemented And Deferred

Implemented:

- union-find over `TermId`;
- recursive term registration;
- congruence closure for constants and applications;
- equality and disequality checking;
- lazy Boolean/EUF integration with CaDiCaL;
- Boolean predicate applications participating in EUF by equality to
  `true`/`false`;
- default-off finite-domain UF symmetry breaking.

Deferred:

- incremental backtracking congruence closure;
- use lists and lookup tables from the fast congruence-closure literature;
- explanation-producing proof forests;
- theory propagation;
- proof output for preprocessing-generated lemmas;
- model construction.

## Paper Traceability

Nieuwenhuis and Oliveras, *Fast Congruence Closure and Extensions*, motivates
the EUF direction and the eventual data structures: use lists, congruence
lookup, pending merges, and explanations. Primo currently implements only the
basic ground congruence behavior, not the optimized algorithm.

Déharbe et al., *Exploiting symmetry in SMT problems*, and Reger and Suda,
*Using SAT and SMT Solvers for Finite Model Finding with Sorts*, motivate the
default-off finite-domain symmetry-breaking preprocessing pass described in
[](preprocessing.md).
