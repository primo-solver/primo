# Primo documentation

Primo is a clean-room, paper-grounded SMT solver developed with LLM assistance.

This documentation is authored in MyST Markdown and built with Jupyter Book.
Build it from the repository root with `scripts/build_docs.sh`; generated HTML
is written under `docs/_build/html`.

## Documentation policy

- Use MyST Markdown for architecture, design, algorithm, paper, and developer notes.
- Use notebooks only for executable experiments, benchmark analysis, and plotted reports.
- Keep claims synchronized with implemented functionality.
- Cite the paper or design note behind each nontrivial algorithmic component.
- Keep dependency integration notes separate from algorithm descriptions.
- Link to an explicit target such as `(h8)=` by its label alone -- `[H8](#h8)`
  -- rather than by path and fragment, which produces build-order-dependent
  `xref_missing` warnings.

## How the book is organised

Five parts, in the order they are normally needed:

- **Core architecture** covers invariants, IPASIR-UP, lazy DPLL(T), and preprocessing.
- **Theories** gives each implemented theory an entry point. [](euf.md) covers
  congruence closure and Boolean arguments. [](arithmetic.md) is the QF_LRA
  reference, with its heuristics, tuning, open investigations, and fuzz evidence
  nested below it. [](nelson-oppen.md) is the QF_UFLRA combination reference;
  [](modular-theory-combination.md) defines the generic component boundary and
  the non-convex QF_UFLIA direction;
  its nested notes contain performance history, the convex merge plan, pure-LRA
  isolation regression, and arithmetic equality-sharing details.
- **Evidence and practice** explains experiments, the canonical run log, debugging,
  and TermId order sensitivity.
- **Paper notes** starts with [](literature.md), which defines the reading and
  confidence policy, followed by the deeply read algorithm papers.
- **Reference** contains retrospectives that do not describe Primo itself.

For current behavior and flags, start with the relevant theory chapter. For
design rationale and rejected experiments, read its nested notes. For measured
results tied to exact commits and commands, use [](run-log.md).
