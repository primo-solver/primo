# Architecture

Primo is organized as layered components with explicit data ownership.

## Layers

- `smtlib/` parses, prints, and type-checks SMT-LIB.
- `core/` owns sorts, symbols, terms, rewriting, substitutions, and models.
- `sat/` owns propositional variables, literals, clauses, assumptions, and the IPASIR-UP backend adapter.
- `smt/` owns Boolean abstraction, clausification, assertion stacks, and DPLL(T).
- `theory/` owns theory solvers and theory-specific explanations.

## Boundaries

- SMT-LIB AST nodes must not be used by the SAT or theory solvers.
- SAT literals must not directly contain terms.
- Theory solvers communicate via theory literals and explanations.
- All terms are created through the term manager.

## Initial status

This repository is an implementation scaffold. The architectural boundaries are present, but solver functionality is not implemented yet.

## Optional tools

Local optional tools live under `contrib/local` and are installed with:

```bash
make contrib-z3
make contrib-cvc5
make contrib-cadical
```

Downloaded archives and build trees under `contrib/downloads`, `contrib/src`, and `contrib/local` are not committed.
