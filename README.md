# Primo

Primo is a paper-grounded SMT solver built from first principles, developed
with LLM assistance. Its SMT implementation is original; SAT solving is
delegated to CaDiCaL through IPASIR-UP. Primo-owned code and documentation use
the [MIT license](LICENSE).

## Build and first solve

With a C++20 compiler, CMake 3.20+, GNU Make, Git and Python 3 installed:

```bash
make build
./build/primo --quiet docs/examples/first-solve.smt2
```

Expected answer: `unsat`. The first build downloads required dependencies
under `contrib/local`. See the [getting-started guide](docs/guide/getting-started.md)
for prerequisites, stdin use and build details.

## Documentation

The manual has three entrances:

- [Done: current behavior and qualification](docs/current-status.md).
- [Active work](docs/active-work.md).
- [Open leads](docs/open-leads.md).

For everyday use, read [SMT-LIB input](docs/guide/input.md),
[models](docs/guide/models.md), [sessions](docs/guide/sessions.md), and
[command-line options](docs/reference/options.md). Implementation explanations
start at [architecture](docs/architecture.md); original algorithm sources and
implementation mappings are indexed in [literature](docs/literature.md).

Build and preview the book locally:

```bash
make docs-serve
```

Open `http://localhost:8000`. Documentation is being restructured before public
release; [the plan](docs/documentation-restructuring.md) records completed
batches and remaining work. A public website has not been deployed by this work.

## Contributing and verification

Start with the [contribution guide](docs/contributing.md). Source, tests and
affected documentation are reviewed together. Keep current behavior distinct
from experiments and proposals.

```bash
make test
python3 scripts/check_doc_examples.py --solver ./build/primo
make docs
```

Solver changes additionally need the [review and correctness gates](AGENTS.md),
including the appropriate fuzzing and model-validation checks. Benchmark
provenance belongs in the [run log](docs/run-log.md).

Primo follows a [clean-room policy](AGENTS.md): existing solvers and the
LLM2SMT predecessor may inform behavioral comparisons and retrospectives, but
must not supply implementation code, tests or documentation text.
