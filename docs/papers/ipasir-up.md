# IPASIR-UP: User Propagators for CDCL

## Citation

Katalin Fazekas, Aina Niemetz, Mathias Preiner, Markus Kirchweger, Stefan Szeider, Armin Biere:  
IPASIR-UP: User Propagators for CDCL. SAT 2023.

## Role in Primo

This paper is the basis for Primo's SAT backend interface.

Primo uses IPASIR-UP to avoid implementing a custom SAT solver while preserving a clean integration point for lazy DPLL(T), and later tighter CDCL(T)-style interaction.

## What we use initially

- incremental SAT backend interface
- adding clauses
- assumptions
- solving under assumptions
- assignment/model-value queries
- failed-assumption information where available
- CaDiCaL as the initial backend implementation

## Deferred

- user propagator callbacks
- inspecting trail changes during search
- adding theory propagations during search
- adding clauses during search
- theory-guided decisions
- explaining user propagations

## Boundaries

- Do not implement custom DPLL or CDCL in Primo.
- Do not copy CaDiCaL code.
- Keep CaDiCaL-specific integration behind the SAT backend adapter.
- Keep SMT terms and theory state out of backend-specific code.

## Tests derived from the interface

- add simple clauses and solve
- solve under assumptions
- distinguish permanent clauses from assumptions
- query assignment only after SAT
- report missing backend dependency clearly
