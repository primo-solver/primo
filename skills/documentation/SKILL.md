---
name: smt-documentation
description: Write architecture, algorithm, invariant, and user documentation for the SMT solver.
---

# SMT solver documentation skill

## Goal

Document the solver so that future code changes preserve the architecture and invariants.

Use Jupyter Book 2 / MyST as the documentation workhorse:

- author normal docs in MyST Markdown
- maintain `docs/myst.yml` and `docs/index.md`
- use notebooks only for executable experiments and benchmark reports
- keep the documentation buildable with `make docs`

## Documentation types

### Architecture docs

Explain:

- module boundaries
- ownership of data structures
- major invariants
- extension points

### Algorithm docs

Explain:

- Boolean abstraction
- clausification
- DPLL(T)
- IPASIR-UP/CaDiCaL SAT backend integration
- congruence closure
- model generation
- push/pop
- explanation generation

### User docs

Explain:

- supported SMT-LIB fragment
- build instructions
- run instructions
- options
- examples
- known limitations

### Developer docs

Explain:

- how to add a theory
- how to add an SMT-LIB construct
- how to add a regression test
- how to run fuzzing
- how to run benchmarks
- how to build the MyST/Jupyter Book documentation

## Style

Use precise technical prose.
Prefer small examples.

Every nontrivial algorithm doc should include:

- input
- output
- invariants
- failure modes
- one worked example

## Output format

When documenting a feature:

1. identify what changed
2. update the nearest relevant doc
3. add examples if useful
4. check that docs do not claim unsupported features
