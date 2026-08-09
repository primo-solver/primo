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
- default-off finite-domain UF symmetry breaking
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

This bootstraps required local dependencies under `contrib/local`, configures
CMake in `build/`, and builds the `primo` executable. CMake emits
`build/compile_commands.json` by default for tools such as `clang-tidy` and
editor language servers.

## Dependencies

Required build dependencies are installed automatically by `make build`:

```bash
make contrib-required # ANTLR4 runtime, CLI11, CaDiCaL/IPASIR
```

Optional reference solver dependencies also install under `contrib/local`:

```bash
make contrib-z3      # install z3-solver into the active Python environment and expose z3
make contrib-cvc5    # download a cvc5 release binary
make contrib         # all of the above
```

`make compare` and `make fuzz` prepend `contrib/local/bin` to `PATH`.

## Run

```bash
./build/primo benchmarks/smoke/qf_uf/direct_equality_contradiction.smt2
```

Experimental QF_UF finite-domain symmetry breaking is available with:

```bash
./build/primo --uf-symmetry-breaking input.smt2
```

Equality-diamond preprocessing is available with:

```bash
./build/primo --eq-bridge --preprocessing-passes 3 input.smt2
```

Solver statistics can be printed to stderr while keeping stdout as the SMT
result:

```bash
./build/primo --stats input.smt2
```

## Test

```bash
make test
```

AddressSanitizer uses a separate Debug build directory:

```bash
make asan-test
```

## Coding checks

```bash
make lint
make format
make check
```

`make lint` always checks Python syntax and **requires `clang-format`** -- if it is not installed, lint fails rather than skipping the C++ format check. It also runs `ruff check` when ruff is installed. `make check` runs lint and tests.

Run `make format` before committing; it applies the repository `.clang-format` in place.

## Fuzz

```bash
make fuzz
```

External fuzzers are optional locally and are skipped clearly when missing.
Real campaigns should run on SLURM with `scripts/yinyang_fuzz.sh`, for example
from a synced cluster checkout:

```bash
scripts/yinyang_fuzz.sh \
  --seeds benchmarks/seeds/qf_uf \
  --solver 'build/primo --quiet' \
  --ref 'z3 model_validate=true' \
  --seconds 3600 \
  --iterations 200
```

## Benchmark

```bash
make bench
```

## Docs

Documentation is written in MyST Markdown and built with Jupyter Book.

```bash
make docs
```

This uses `scripts/build_docs.sh`, which creates `.venv-docs` and installs
`docs/requirements.txt` when needed. The generated HTML is written to
`docs/_build/html`.

Use notebooks only for executable experiments and benchmark reports.

## Literature

See `docs/papers/` and `docs/literature.md`.
