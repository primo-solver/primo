# TODO

## M0: Core architecture

- [ ] Create typed sort representation.
- [ ] Create symbol table.
- [ ] Create hash-consed term manager.
- [ ] Create SMT-LIB parser for QF_UF subset.
- [ ] Create pretty-printer.
- [ ] Add round-trip parser tests.
- [ ] Add sort-checking tests.

## M1: SAT and abstraction

- [ ] Define SAT literals, variables, and clauses.
- [ ] Define IPASIR-UP SAT backend interface.
- [ ] Add CaDiCaL-backed IPASIR-UP adapter.
- [ ] Add clear dependency detection and error messages for missing CaDiCaL/IPASIR-UP.
- [ ] Implement Boolean abstraction from Bool terms to SAT clauses.
- [ ] Maintain atom-to-SAT-literal mapping.
- [ ] Add tests for abstraction polarity.

## M2: Lazy DPLL(T)

- [ ] Define theory-solver interface.
- [ ] Implement lazy DPLL(T) loop.
- [ ] Extract true theory literals from SAT assignment.
- [ ] Add theory conflict lemmas back to SAT solver.
- [ ] Add smoke tests comparing with Z3.

## M3: QF_UF congruence closure

- [ ] Implement union-find over TermId.
- [ ] Register application terms.
- [ ] Merge equality classes.
- [ ] Maintain congruence closure.
- [ ] Support disequality constraints.
- [ ] Detect equality/disequality conflicts.
- [ ] Add QF_UF regression tests.

## M4: EUF explanations

- [ ] Track source literal for every asserted equality/disequality.
- [ ] Explain equality paths.
- [ ] Explain congruence-derived equalities.
- [ ] Generate conflict clauses.
- [ ] Validate conflict lemmas on regression tests.

## M5: QF_UF models

- [ ] Build equivalence classes as domain elements.
- [ ] Interpret constants.
- [ ] Interpret functions on observed tuples.
- [ ] Add default function values.
- [ ] Print SMT-LIB-style model.
- [ ] Validate simple models with Z3/cvc5 where possible.

## M6: Difference logic

- [ ] Parse constraints of form `x - y <= c`.
- [ ] Normalize difference constraints.
- [ ] Build weighted graph.
- [ ] Detect negative cycles.
- [ ] Explain cycle conflicts.
- [ ] Add QF_IDL/QF_RDL tests.

## M7: LRA scaffold

- [ ] Represent rational coefficients.
- [ ] Represent linear polynomials.
- [ ] Normalize arithmetic atoms.
- [ ] Define bounds and tableau rows.
- [ ] Add parser/type-checker tests.

## M8: LRA solver

- [ ] Implement feasibility checking.
- [ ] Implement pivoting.
- [ ] Implement conflict explanations.
- [ ] Add backtracking.
- [ ] Add theory propagation.
