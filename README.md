# Primo

Primo is a clean-room, paper-grounded SMT solver developed with LLM assistance.

Its SMT layers are built from first principles, papers, and tests. The project starts from scratch and does not reuse code from existing SMT solvers or from its predecessor, LLM2SMT. SAT solving is delegated through IPASIR-UP, initially using CaDiCaL as the backend.

## Goals

- implement a small but systematic SMT solver
- keep the architecture modular and inspectable
- document every nontrivial algorithmic component with its paper basis
- prioritize correctness infrastructure before performance
- support reproducible testing, fuzzing, benchmarking, and debugging

## Current implementation

Primo supports quantifier-free UF, linear Real arithmetic, linear Int arithmetic,
and their combinations: `QF_UF`, `QF_LRA`, `QF_LIA`, `QF_UFLRA`, and `QF_UFLIA`,
within the documented frontend and resource limits. A direct SAT path handles
Boolean-only formulas outside explicit integer routing. All five routes support
typed, owning models and checking
against original assertions; model printing is separate from construction.

The default EUF engine is incremental congruence closure. Arithmetic uses exact
Simplex with integer completion for LIA. IPASIR-UP callbacks provide incremental
theory checks during Boolean search. QF_UFLIA uses the extensible component
driver; QF_UFLRA retains its dedicated convex combination engine. Pure theories
keep direct paths without combination overhead.

Start with [current status and qualification](docs/current-status.md),
[architecture](docs/architecture.md), and [model interpretations](docs/model-interpretation.md).
The feature branch adds [scoped incremental sessions](docs/incremental-smt.md):
interactive and file-based `push`/`pop`, repeated checks, and scoped declarations.
Each query gets a fresh solver; persistent incremental solving remains deferred.
SLURM qualification of this session layer is pending.
Quantifiers, arrays, bit-vectors, nonlinear arithmetic, proof production, and
dedicated difference-logic frontends are not implemented. Integer resource
limits can return `unknown`; support is not a promise to solve every input.

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

QF_UF finite-domain symmetry breaking is enabled by default where applicable.
For an ablation, disable it with:

```bash
./build/primo --no-uf-symmetry-breaking input.smt2
```

Equality-diamond preprocessing is also enabled by default. Its pass budget can
be selected explicitly:

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

External fuzzers are optional build dependencies; a local skipped invocation
does not satisfy a solver-change correctness gate. Merge/default qualification
requires the matching generated and real-seed gates, including model checks
under the same solver options. Operational instructions live in
`skills/testing/` and `skills/fuzzing/`. Real campaigns should run on SLURM with
`scripts/yinyang_fuzz.sh`, for example
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

Keep timing and correctness separate: replay every successful SAT row with the
same binary and options, then validate the printed model independently with
Dolmen. Reuse recorded reference answers when their input hashes and provenance
match; do not automatically rerun reference solvers for a Primo-only comparison.
See [experiments](docs/experiments.md), `skills/benchmarking/`, and
`skills/primo-model-validation/`. Exact revisions, coverage, resource limits and
artifact locations are recorded in [the run log](docs/run-log.md).

## Docs

Documentation is written in MyST Markdown and built with Jupyter Book.

```bash
make docs
```

This uses `scripts/build_docs.sh`, which creates `.venv-docs` and installs
`docs/requirements.txt` when needed. The generated HTML is written to
`docs/_build/html`.

The build always re-reads every page and **fails on any Sphinx warning**, so a
broken cross-reference or an unknown code-fence language is an error rather
than a line of scrollback. This matters because Sphinx only warns about the
documents it re-reads: an incremental build reports a clean tree whenever
nothing changed, and says nothing about a reference that an edit to some
*other* file just broke. Set
`PRIMO_DOCS_STRICT=0` to downgrade the failure while iterating; do not commit
against a build that needed it.

Use notebooks only for executable experiments and benchmark reports.

## Source archive

Create a publication-ready source snapshot with rendered HTML documentation:

```bash
make archive
```

The deterministic `dist/primo-<revision>.tar.gz` contains the solver source,
build and test support, SMT-LIB samples, and `html/`. It excludes Git metadata,
build artifacts, experiment results, and all Markdown/MyST sources, including
Sphinx's generated `_sources` copies. A matching SHA-256 file is written beside
the archive. The archive includes `SOURCE-REVISION`, which supplies the version
banner when the source is built without a Git checkout. Source links are hidden
in the rendered site because those source files are intentionally absent.

## Literature

See `docs/papers/` and `docs/literature.md`.
