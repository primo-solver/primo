# Writing and checking documentation

Write current explanations under the appropriate implementation, guide, reference,
or contributor page. Keep active work, research leads and dated evidence separate.
Every implementation chapter needs concrete owners, operations, invariants,
limitations, an example, source links, and paper/test mappings.

## Writing conventions

```{include} ../../docs/development/documentation.md
:start-after: "## Writing conventions"
:end-before: "## Publication references"
```

## Sources and review

Use the shared bibliography in `docs/references.bib`, with DOI or stable publisher/
author links. Ignored local PDF names are not bibliographic identifiers. Read
relevant primary sections before claiming a mechanism is implemented, and state
the subset/deviations. Keep source links in the central `source` role configuration;
ordinary paragraphs should not carry repeated revision hashes.

Track coverage in [the preservation audit](../coverage/index.md) and verification
separately from reader acceptance in [the review ledger](../review.md). Retain
old paths/anchors until the replacement and compatibility mapping are accepted.

## Build and check

```bash
make docs-next
make docs-check-links
make docs-check-external
```

The first command is an incremental preview. The local/Git-link target rebuilds
both books strictly, including a full draft build. The external checker produces
separate Sphinx linkcheck reports; distinguish genuinely broken targets from
transient timeout, rate-limit or authorization failures. Do not silently suppress
a new failure. Inspect rendered equations, tables, navigation and source links.

Book builds do not execute notebooks or solver examples. Changed executable
examples need separate verification in the chosen test environment. Keep source
fixtures and expected outputs synchronized, and review a changed CLI snapshot
before replacing it. The full original {source}`authoring record <docs/development/documentation.md>`
retains logo asset, distribution and infrastructure history during migration.
