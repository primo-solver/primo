# Primo

Primo is a paper-grounded SMT solver built from first principles, developed
with LLM assistance. Its SMT implementation is original; SAT solving is
delegated to CaDiCaL through IPASIR-UP. Primo-owned code and documentation use
the [MIT license](LICENSE).

Our goal is to understand all the code we develop with LLM assistance. This
requires clear documentation throughout the project: how the algorithms work,
why design choices were made, and what invariants and limitations the
implementation has.

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

- [Done: current behavior and qualification](docs/capabilities.md).
- [Active work](docs/work/publication.md).
- [Open leads](docs/leads/index.md).

For everyday use, read [SMT-LIB input](docs/guide/input.md),
[models](docs/guide/models.md), [sessions](docs/guide/sessions.md), and
[command-line options](docs/reference/options.md). Implementation explanations
start at [architecture](docs/implementation/architecture.md); original algorithm sources and
implementation mappings are indexed in [literature](docs/literature.md).

Build and preview the book locally:

```bash
make docs-serve
```

Open `http://localhost:8000` to preview the current manual. `make docs` builds
only that manual; `make docs-history` builds the historical book separately.
`make docs-site` assembles both books and preserves original page/fragment links.
`make docs-full` strictly rebuilds the combined site; `make docs-check-links`
also validates its links. The
[closing review](docs/review.md) and [publication checklist](docs/work/publication.md)
record verification and outstanding acceptance. The published manual is
[Primo documentation](https://primo-solver.github.io/primo/).

## Citing Primo

If you use Primo in research, please cite:

Mikoláš Janota and Jan Jakubův.
[Vibe-Coded and Tuned: A State-of-the-Art SMT Solver for QF-LRA](https://doi.org/10.29007/rn52).
*Kalpa Publications in Computing*, volume 23, pages 44–56, EasyChair, 2026.
[Publisher copy](https://easychair.org/publications/paper/rPZ9).

```bibtex
@inproceedings{janota2026primo,
  author    = {Mikoláš Janota and Jan Jakubův},
  title     = {Vibe-Coded and Tuned: A State-of-the-Art {SMT} Solver for {QF-LRA}},
  booktitle = {Proceedings of 16th International Workshop on the Implementation
               on Logics. Proceedings of the 26th Conference on Logic for
               Programming, Artificial Intelligence, and Reasoning (short papers)},
  editor    = {Stephan Schulz and Konstantin Korovin and Michael Rawson},
  series    = {Kalpa Publications in Computing},
  volume    = {23},
  pages     = {44--56},
  publisher = {EasyChair},
  year      = {2026},
  doi       = {10.29007/rn52},
  url       = {https://easychair.org/publications/paper/rPZ9}
}
```

Also report the commit and configuration used in your experiments.
[CITATION.cff](CITATION.cff) supplies the paper metadata for GitHub's
**Cite this repository** button.

## Contributing and verification

Start with [CONTRIBUTING.md](CONTRIBUTING.md) for MIT/DCO and the contribution guide. Source, tests and
affected documentation are reviewed together. Correctness comes before
performance: solver behavior changes require fresh
[fuzzing evidence](docs/development/correctness.md#choose-a-fuzz-gate), and
wrong answers, invalid models, or crashes block acceptance. Keep current
behavior distinct from experiments and proposals.

```bash
make check
python3 scripts/check_doc_examples.py --solver ./build/primo
make docs
```

`make check` runs lint and tests, but does not run fuzzing. Solver changes
additionally need the [review and correctness gates](AGENTS.md),
including the appropriate fuzzing and model-validation checks. Benchmark
provenance belongs in the [run log](docs/development/experiments.md).

Primo follows a [clean-room policy](AGENTS.md): existing solvers and the
LLM2SMT predecessor may inform behavioral comparisons and retrospectives, but
must not supply implementation code, tests or documentation text.
