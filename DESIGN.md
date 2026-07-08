# Primo Design

Primo is a C++20, CMake-built SMT solver scaffold.

## Architecture

- `smtlib/` parses, prints, and type-checks SMT-LIB.
- `core/` owns sorts, symbols, terms, rewriting, substitutions, and models.
- `sat/` owns SAT literals, clauses, assumptions, and the IPASIR-UP backend adapter.
- `smt/` owns Boolean abstraction, clausification, assertion stacks, and lazy DPLL(T).
- `theory/` owns theory solvers and explanations.

## Core invariants

### Term invariant

- Terms are hash-consed.
- Term equality is by `TermId`.
- Every term has an explicit sort.
- Boolean formulas are terms of sort `Bool`.
- Applications are represented uniformly.
- All terms are created through the term manager.

### SAT invariant

- Primo does not implement its own SAT solver.
- SAT solving is delegated through IPASIR-UP, initially backed by CaDiCaL.
- The SAT backend sees only clauses over SAT literals.
- SMT atoms are mapped to SAT variables by the SMT layer.
- The SAT backend must not inspect theory terms.
- Assumptions are distinct from permanent clauses.
- Backend-specific details must stay behind the SAT adapter.

### Theory invariant

A theory solver receives asserted theory literals.

It may return:

- consistent
- conflict clause
- propagated literal with explanation
- model fragment

### DPLL(T) invariant

- The first implementation may use lazy theory checking.
- Theory propagation is optional until the EUF checker is correct.
- Every generated theory lemma must be independently checkable by tests.
