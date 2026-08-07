# Primo documentation

Primo is a clean-room, paper-grounded SMT solver developed with LLM assistance.

This documentation is authored in MyST Markdown and built with Jupyter Book.

Build it from the repository root with:

```bash
scripts/build_docs.sh
```

The generated HTML is written under `docs/_build/html`.

## Documentation policy

- Use MyST Markdown for architecture, design, algorithm, paper, and developer notes.
- Use notebooks only for executable experiments, benchmark analysis, and plotted reports.
- Keep claims synchronized with implemented functionality.
- Cite the paper or design note behind each nontrivial algorithmic component.
- Keep dependency integration notes separate from algorithm descriptions.
- Link to an explicit target such as `(h8)=` by its label alone -- `[H8](#h8)`
  -- not by path and fragment. The path form (`qf-lra-heuristics.md#h8`) makes
  MyST look up an HTML id in a document that may not have been rendered yet, so
  it emits `xref_missing` warnings that depend on build order while still
  producing a working link. Labels resolve globally and do not.

## How the book is organised

Five parts, in the order you would read them to understand the solver:

- **Core architecture** -- invariants, the IPASIR-UP/CaDiCaL SAT backend, lazy
  DPLL(T), and preprocessing. Start with [](architecture.md).
- **Theories** -- one chapter per theory, each the entry point to its own
  detail. [](euf.md) covers congruence closure, with disequality propagation
  nested under it. [](arithmetic.md) is the QF_LRA reference -- what is
  implemented, every option and its default, and the statistics -- with the
  heuristics catalogue, the tuning-parameter review, the open-leads register,
  the tropical-matrix analysis and the fuzz-coverage note nested beneath it.
  [](difference_logic.md) is separate because it is not yet implemented.
- **Evidence and practice** -- how experiments are run and recorded
  ([](experiments.md), [](run-log.md)), how to debug the solver, and the
  TermId order-sensitivity note.
- **Paper notes** -- [](literature.md) states the reading policy and the three
  confidence tiers; the two index files for the lower tiers are nested under
  it, and each deeply-read paper has its own chapter.
- **Reference** -- retrospectives and material that does not describe Primo
  itself.

**Where to look for a given question.** What does the solver do today, and
with which flags? The theory chapter (for arithmetic, that is
[](arithmetic.md), including its options and statistics reference). Why does
it do it that way, and what was tried and rejected? The notes nested under
that chapter -- [](open-leads.md) in particular is an investigation log, not a
specification. What was measured, when, on what commit? [](run-log.md).
