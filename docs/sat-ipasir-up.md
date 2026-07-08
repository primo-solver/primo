# SAT backend: IPASIR-UP and CaDiCaL

## Role

Primo does not implement a SAT solver.

The SAT layer owns Primo's propositional variables, literals, clauses, assumptions, and backend adapter. Actual SAT solving is delegated through IPASIR-UP, initially using CaDiCaL as the implementation.

## Initial scope

- create and destroy a backend instance
- add permanent clauses
- add assumptions for a solve call
- call solve
- read Boolean assignments after SAT
- identify failed assumptions where supported
- return clear unsupported-feature errors for callbacks not wired yet

## Deferred scope

- online theory propagation through user-propagator callbacks
- theory-guided decisions
- adding clauses during search
- explaining user propagations
- proof-producing integration

## Boundaries

- The backend sees only SAT literals and clauses.
- SMT terms and theory literals do not cross into CaDiCaL-specific code.
- CaDiCaL-specific APIs stay isolated in the backend module.
- Missing IPASIR-UP/CaDiCaL dependencies must be reported explicitly.

## References

See `docs/papers/ipasir-up.md`.
