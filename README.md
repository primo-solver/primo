# Primo

Primo is a clean-room, paper-grounded SMT solver developed with LLM assistance.

Its SMT layers are built from first principles, papers, and tests. The project starts from scratch and does not reuse code from existing SMT solvers or from its predecessor, LLM2SMT. SAT solving is delegated through IPASIR-UP, initially using CaDiCaL as the backend.

## Goals

- implement a small but systematic SMT solver
- keep the architecture modular and inspectable
- document every nontrivial algorithmic component with its paper basis
- prioritize correctness infrastructure before performance
- support reproducible testing, fuzzing, benchmarking, and debugging

## Initial target

The first target is QF_UF:

- SMT-LIB subset
- typed terms
- Boolean abstraction
- IPASIR-UP/CaDiCaL SAT backend integration
- lazy DPLL(T)
- congruence closure
- equality and disequality reasoning
- eventual model generation

## Later targets

- difference logic
- LRA via Simplex
- incrementality
- theory propagation
- LIA extensions

## Clean-room policy

Primo is a new solver. LLM2SMT may be used as a reference artifact only, not as a source of code or text.

## Build

```bash
make build
```

This configures CMake in `build/` and builds the `primo` executable. The current executable is a scaffold and reports unsupported solving explicitly.

## Run

```bash
./build/primo benchmarks/smoke/qf_uf/direct_equality_contradiction.smt2
```

## Test

```bash
make test
```

## Fuzz

```bash
make fuzz
```

External fuzzers are optional and are skipped clearly when missing.

## Benchmark

```bash
make bench
```

## Docs

Documentation is written in MyST Markdown and built with Jupyter Book 2 / MyST.

```bash
make docs
```

Use notebooks only for executable experiments and benchmark reports.

## Literature

See `docs/papers/` and `docs/literature.md`.
