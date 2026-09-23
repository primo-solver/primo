# Primo documentation

Primo is a clean-room, paper-grounded SMT solver developed with LLM assistance.

This documentation is authored in MyST Markdown and built with Jupyter Book.
Build it from the repository root with `scripts/build_docs.sh`; generated HTML
is written under `docs/_build/html`.

## Work status

The documentation separates three questions:

| Status | Entry point | What belongs here |
| --- | --- | --- |
| What has been done | [](current-status.md) | Implemented behavior, accepted results, supporting revisions and verification, and known limitations. |
| What is being worked on | [](active-work.md) | Work actually underway, its owner, branch or review, remaining acceptance criteria, and blockers. |
| Open leads | [](open-leads.md) | Ideas and proposed work, their evidence, prerequisites, and the next investigation that could accept or reject them. |

Status indexes link to the relevant component, design, and evidence pages;
they do not duplicate those pages. A proposal is not active work until someone
starts it, and an implementation awaiting required qualification is not done.
Dated experiments retain their original revisions and outcomes, including
rejected results. A completed experiment need not produce an accepted feature.

## Documentation policy

- Substantial restructuring before public release is planned in
  [](documentation-restructuring.md). The current three status entry points
  establish the intended distinction; the underlying book still needs the
  content review, extraction, and rewrites specified there. A private repository
  transfer may precede that work.
- Documentation as code is an existing engineering requirement: review source,
  tests, and affected documentation together, and update status as work changes.
- The proposed public contribution workflow is in [](contributing.md), with
  the decision to retain MIT and pending publication-rights, sign-off, and
  governance decisions in [](public-repository-migration.md). The public
  workflow remains a draft; existing engineering requirements continue to apply.
- Use MyST Markdown for architecture, design, algorithm, paper, and developer notes.
- Use notebooks only for executable experiments, benchmark analysis, and plotted reports.
- Keep claims synchronized with implemented functionality.
- Cite the paper or design note behind each nontrivial algorithmic component.
- Keep dependency integration notes separate from algorithm descriptions.
- Link to an explicit target such as `(h8)=` by its label alone -- `[H8](#h8)`
  -- rather than by path and fragment, which produces build-order-dependent
  `xref_missing` warnings.

## How the book is organised

Six parts, in the order they are normally needed:

- **Work status** separates completed work, active work, and open leads through
  the three entry points above.
- **Core architecture** covers ownership invariants, typed models,
  tactics, IPASIR-UP, DPLL(T), and preprocessing.
- **Theories** gives each implemented theory an entry point. [](euf.md) covers
  congruence closure and Boolean arguments. [](arithmetic.md) is the QF_LRA
  reference and entry point to LIA, with heuristics, tuning, investigations, and fuzz evidence
  nested below it. [](nelson-oppen.md) is the QF_UFLRA combination reference;
  [](modular-theory-combination.md) defines the generic component boundary and
  the implemented non-convex QF_UFLIA route; [](n-way-combination-migration.md)
  specifies the production collection driver and its completed qualification;
  its nested notes contain performance history, the convex merge plan, pure-LRA
  isolation regression, and arithmetic equality-sharing details.
- **Evidence and practice** contains the draft contribution and public-migration
  guides, experiments, the canonical run log, debugging, and TermId order
  sensitivity.
- **Paper notes** starts with [](literature.md), which defines the reading and
  confidence policy, followed by the deeply read algorithm papers.
- **Reference** contains retrospectives that do not describe Primo itself.

For current behavior and flags, use the status guide and relevant theory chapter. For
design rationale and rejected experiments, read its nested notes. For measured
results tied to exact commits and commands, use [](run-log.md).

## Logo assets

The lowercase `primo` wordmark is drawn with original SVG paths, with navy
lettering and a teal dot. The book uses light and dark variants through
`docs/_config.yml`. Editable assets are `docs/_static/primo-logo.svg`,
`docs/_static/primo-logo-dark.svg`, and the square `docs/_static/primo-avatar.svg`;
`docs/_static/primo-avatar.png` is the 512-pixel export for profile uploads.
The artwork needs no external font and follows the repository's MIT license.
