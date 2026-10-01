# Documentation replacement preparation

**Status: replacement draft; reader acceptance pending.**

The manual, references, contributor procedures, evidence summaries and research
register are being checked through [the preservation audit](../coverage/index.md).
The old book remains available at its existing paths. Local commits and a
successful build do not publish a website or change repository visibility.

## Before switching the book

1. Review the source-grounded chapters and mark reader acceptance explicitly.
   Review the content decisions in the section inventory, including corrected
   claims and retained history.
2. Use the compatibility mapping to preserve each old page and anchor through
   a retained archive location or a redirect. The mapping is preparation;
   redirects are not installed by this rewrite.
3. Move the accepted draft into the final book tree, updating relative includes
   and shared bibliography/example/asset locations together. Preserve both the
   centralized implementation revision and historical evidence snapshot.
4. Update README/navigation and deployment configuration only as part of the
   accepted replacement. Check the built site from its actual serving prefix,
  all local/Git/external links, search/navigation and old anchors.

No website framework change is needed: the static Jupyter Book HTML remains the
deployment artifact. MIT remains the repository license. Do not include local
reading PDFs in the published archive; cite stable bibliographic records.

## Remaining evidence work

[Unresolved artifact mappings and quantifier checking limits](evidence.md)
remain explicit. Those tasks do not become completed experiments merely because
their documentation is now preserved. The existing
{source}`public migration plan <docs/public-repository-migration.md>` retains
other publication decisions and their original scope.
