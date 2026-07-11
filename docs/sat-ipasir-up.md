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

## CaDiCaL option overrides

The CLI accepts repeated `--cadical-option name=value` arguments. Each pair is
parsed as an integer CaDiCaL option and applied through `CaDiCaL::Solver::set()`
while the backend is still in its configuring state. The options are stored in
`smt::CheckSatOptions` and consumed only by `sat::CadicalBackend`; no SMT or
theory layer depends on CaDiCaL-specific option names.

This hook is intended for benchmark campaigns and dependency checks, not as a
paper-backed solver algorithm. It exists because incremental SAT workloads can
be sensitive to CaDiCaL version defaults. In particular, repeated `solve()`
calls with small deltas should be checked with an incremental profile such as
`ilb=2` plus selected inprocessing options disabled before attributing a
slowdown to Primo's EUF code. Invalid option names or values are rejected by the
backend and reported as unsupported input for that run.

## References

See `docs/papers/ipasir-up.md`.

## Local installation

CaDiCaL can be installed into `contrib/local` with:

```bash
make contrib-cadical
```

`make build` passes `contrib/local` through `CMAKE_PREFIX_PATH` and reports whether CaDiCaL/IPASIR headers and libraries were found.
