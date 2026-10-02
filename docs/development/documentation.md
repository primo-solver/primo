# Writing and checking documentation

Write current explanations under the appropriate implementation, guide, reference,
or contributor page. Keep active work, research leads and dated evidence separate.
Every implementation chapter needs concrete owners, operations, invariants,
limitations, an example, source links, and paper/test mappings.

## Writing conventions

Explain what an operation does before describing its interface or limitations.
Use standard mathematical and solver terminology, with these conventions:

- Define a technical term or acronym at its first use when the reader needs
  it to understand the next step. For example, explain a strategy leaf as one
  tactic application, and expand CITE before discussing its simplification.
- Keep code identifiers exact, but describe their behavior in ordinary prose.
  Prefer “clauses generated before simplification” to an unexplained label
  such as “raw clauses.”
- Use concrete verbs and objects. Write “the transformation must preserve
  satisfiability,” rather than “the transformation owes an equivalence.”
  Avoid strings of compressed modifiers where a sentence would be clearer.
- Keep warnings where they prevent a specific misunderstanding. State the
  scope of tests and experiments once in the evidence section, rather than
  repeatedly interrupting the algorithm with validation disclaimers.
- Keep architecture sections about ownership, operations and invariants.
  Put migration chronology, commit hashes, SLURM jobs, run IDs and measured
  counts on evidence pages. Link to that evidence from a clearly named section;
  readers should not need the development history to understand the design.
- Keep syntax-specific rules, such as `let` scope and definition expansion,
  in the frontend chapter. The architecture overview identifies the responsible
  layer and links to that explanation.
- Keep implementation-status summaries to one short sentence per feature,
  with links to the detailed chapters. Put limitations and open work separately;
  do not expand a summary bullet into an algorithm or experiment report.
- Distinguish terms with different meanings. In particular, a logical guard
  controls when a constraint applies; an acceptance check decides whether a
  proposed rewrite is worth performing. Do not use “guard” for both without
  explaining which is meant.


## Sources and review

Use the shared bibliography in `docs/references.bib`, with DOI or stable publisher/
author links. Ignored local PDF names are not bibliographic identifiers. Read
relevant primary sections before claiming a mechanism is implemented, and state
the subset/deviations. Keep source links in the central `source` role configuration;
ordinary paragraphs should not carry repeated revision hashes.

Track coverage in [the preservation audit](../coverage/index.md) and verification
separately from reader acceptance in [the review ledger](../review.md). Retain
old paths/anchors through the compatibility manifest.

## Build and check

```bash
make docs
make docs-check-links
make docs-check-external
```

The first command is an incremental preview. The local/Git-link target rebuilds
both books strictly, including the frozen historical book. The external checker produces
separate Sphinx linkcheck reports; distinguish genuinely broken targets from
transient timeout, rate-limit or authorization failures. Do not silently suppress
a new failure. Inspect rendered equations, tables, navigation and source links.

Book builds do not execute notebooks or solver examples. Changed executable
examples need separate verification in the chosen test environment. Keep source
fixtures and expected outputs synchronized, and review a changed CLI snapshot
before replacing it. The full original {record}`authoring record <docs/development/documentation.md>`
retains logo asset, distribution and infrastructure history during migration.

## Publication and source archives

`make docs` builds the current and historical books, then assembles
`docs/_build/site/`. `make docs-full` disables incremental reuse for both books.
The site assembler verifies every archived compatibility target before replacing
the previous site. Its generated redirects have ordinary-link fallbacks.
Documentation dependency installation disables pip's periodic version check,
so builds do not print pip upgrade notices.

`make archive` packages tracked source, editable books and the assembled HTML,
with a source revision and SHA-256 checksum. It requires a clean checkout;
`--allow-dirty` is only for diagnostic snapshots. Reading PDFs and cached tools
are excluded. The archive rebuilds without Git history; source-link validation
still requires a checkout containing the pinned commits. See the root
`THIRD_PARTY.md` before distributing dependencies or benchmark material.

GitHub Actions builds PR previews with read-only repository access. Pages
publication is restricted to public `main` and the `github-pages` environment;
configure required approval there before enabling deployment. The site is served
under `/primo/`, including the historical book at `/primo/archive/original/`.
External-link checks are a separate scheduled/manual workflow so publisher
rate limits do not obscure deterministic documentation failures.
