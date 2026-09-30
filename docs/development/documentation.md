# Maintain the documentation

The book is authored in MyST Markdown under `docs/` and built with Jupyter Book
1.x. Source, tests and affected explanations belong in the same change.
The public contribution process remains a [draft](../contributing.md); the
repository's existing documentation requirements already apply.

## Build and preview

The replacement book is now developed separately in `docs-next/`. Use
`make docs-next` to build it incrementally, or `make docs-next-serve` for its
preview at `http://localhost:8001`. Its `review.md` and `review.csv` track page review and
acceptance. The commands below continue to build the existing book.

Draft builds reuse Sphinx's cache and rebuild changed pages and their
dependants. Use `make docs-next-full` to re-read every draft page and check all
warnings before accepting changes. Warnings are errors in both modes, but
cached pages may hide warnings during an incremental build. The first build
and configuration changes may require a full rebuild. When calling the wrapper
directly, `PRIMO_DOCS_DIR=docs-next bash scripts/build_docs.sh --all` forces one.

```bash
make docs
make docs-serve
```

`make docs` uses `scripts/build_docs.sh`, installs `docs/requirements.txt` into
`.venv-docs`, re-reads every page and fails on Sphinx warnings. Generated HTML
is in `docs/_build/html`. `make docs-serve` builds and serves it on port 8000.
Use `PRIMO_DOCS_VENV` to select another documentation virtual environment.

Both books enable MyST's `dollarmath` extension: use `$...$` for inline
mathematics and `$$...$$` for displayed equations. Check the rendered page
when adding equations or mathematical tables.

Notebook execution is disabled. A successful book build checks references and
rendering, not the behavior of solver examples. Run:

```bash
make build
python3 scripts/check_doc_examples.py --solver ./build/primo
```

The inputs, expected outputs and invocation manifest live in `docs/examples/`.
Guide pages use `literalinclude` so displayed programs and outputs are the files
being checked. The checker exercises file/stdin use, models, scopes, quantified
UF, selected unsupported/error responses, the preprocessing-only `unknown`
response, both admitted assumptions routes, and the captured CLI help. It does
not fuzz the solver or independently prove model validity.

## Check hyperlinks

```bash
make docs-check-links       # fully rebuild both books; check local and pinned Git links
make docs-check-external    # also check external URLs with Sphinx
```

The local check uses `scripts/check_doc_links.py`, a standard-library Python
script. It checks rendered HTML links, assets referenced by `href`/`src`, and
HTML fragments, including percent-encoded paths and anchors. Theme templates
and downloaded examples are not scanned as book pages. Links to Primo files
and commits on GitHub are resolved in the local Git checkout: files must exist
at the named commit, and line ranges must fit that version. Moving branch
references and unsupported Git fragments fail explicitly. The check does not
establish that a cited function still supports the surrounding explanation.

The local JSON report is `docs/_build/linkcheck-local.json`. A missing book,
file, anchor, or Git object makes the command fail. A shallow checkout may need
the referenced history fetched first. To inspect an existing build without
rebuilding it:

```bash
python3 scripts/check_doc_links.py docs/_build/html docs-next/_build/html
```

The external stage uses Sphinx's built-in
[linkcheck builder](https://www.sphinx-doc.org/en/master/usage/builders/index.html#sphinx.builders.linkcheck.CheckExternalLinksBuilder),
already included in the documentation environment. Each book writes
`_build/linkcheck/output.txt` and `output.json` (one JSON object per line),
identifying the source page, line, URL, and result. Both books are checked even
if one reports failures. Requests use four workers, a 15-second timeout, one
attempt, and a 30-second backoff limit; server `Retry-After` headers take
precedence. External HTML anchors are checked where supported by Sphinx;
PDF page fragments are not validated.

Primo's Git file/commit URLs are excluded from HTTP checking because the local
stage checks them without GitHub credentials. The repository landing page is
also excluded while the repository is private. These are the only configured
URL exclusions, documented in each book's `_config.yml`.

The linked CaDiCaL API header has a separate anchor-only exception: GitHub
implements its code-line fragments dynamically. Sphinx still checks that the
page is reachable, but does not validate its line range. Primo's own source
line ranges are checked locally as described above. GitHub README and wiki
heading links use their literal `user-content-` HTML anchors so they can be
checked without JavaScript.

Review external failures before changing a citation: a 403, rate limit, or
timeout can be an access problem rather than a missing paper. Sphinx records
some temporary outages, such as HTTP 503, as `ignored`; those remain unverified.
Do not treat an ignored or inaccessible URL as a successful check or add broad
domain exclusions to hide failures. Preserve the report and resolve or record
each outstanding link. No credentials are stored in the book configuration.

## Page roles

Apply the roles below to new draft pages as well. Do not copy the entire old
tree into the draft. Keep source links tied to the explained revision and
record checks and remaining review questions for each page. The draft must
pass page review and the release gates before replacing this book.

Use [](../current-status.md) for the canonical capability map,
`guide/` for user tasks, `reference/` for interface details, and implementation
chapters for representations and algorithms. The new `implementation/` layout
is still planned; use the nearest existing component chapter until it moves.
Keep detailed paper notes in `papers/` and dated investigations in `archive/`.

[](../active-work.md) and [](../open-leads.md) index work in different states.
Update them when work starts, is accepted, is rejected, or is deferred. An
accepted component can still have active extensions and open hypotheses.

Add pages to `docs/_toc.yml`. Preserve explicit labels and old heading targets
when moving a page, and update relative links in extracted content. The
{download}`page map <../documentation-page-map.csv>` tracks reviewed structural
dispositions and completed batches; structural review is not factual sign-off.
The [inventory audit](../work/documentation-audit.md) also links the supporting
file map, captured old targets, and proposed final contents. Check its
compatibility snapshot before moving or renaming a page.

For an implementation chapter, include concrete types and source files,
inputs/outputs, representation, ownership/lifetime, stable IDs versus borrowed
references, allocating operations, invariants, failure paths, a worked example,
and test/evidence links. Its paper mapping must state the relevant section,
implemented subset, deviations and limitations. Do not claim a full paper
algorithm when only a baseline or scaffold is present.

## Publication references

Both books use `docs/references.bib` and render a bibliography page. Add a
publication there once, then cite its key in MyST:

````text
The arithmetic procedure uses exact bounds {cite:p}`DutertreM06`.
{cite:ts}`king2014` describes the SOI algorithm in §3.3.
````

Use `doi` for the cited edition when available. Otherwise provide a stable
institutional or author `url`, with the report number or thesis institution.
Keep a conference paper, expanded report, thesis and slide deck as separate
entries when their sections differ. Record the section, theorem or printed
page in the surrounding explanation, including whether a page number refers
to the PDF or the publication's pagination.

Import only bibliographic metadata: authors, title, venue, date and publication
identifiers. Check imported author order, names, year, entry type and identifier
escaping. Keep DOI and URL values literal, without TeX escapes. Raw bibliography
imports under `external/bib/` and local reading PDFs are ignored. Do not add
abstracts, full text, local `file` fields or publisher figures to the book.
Write the explanation in our own words and cite the source. Link to the
publication; the project's MIT license does not license third-party papers.

Reading notes must still distinguish the sections actually read, the
implemented subset, deviations and open work. A bibliography entry does not
establish any of those. Source comments and JSON records cannot use MyST roles;
use a DOI or stable publication link there. If normalizing an old recorded
command, mark a replaced input filename as a bibliography-key placeholder
without presenting it as a literal command or a new run.

`make docs-check-links` rebuilds both books, checks citation resolution through
Sphinx, and verifies the generated local links. `make docs-check-external`
also checks publication URLs. Keep each book's single bibliography directive
on its `references.md` page; its default filter lists only works cited in that
book. Changes to the shared BibTeX file also invalidate the draft's cached
citation data.

## Source links and revisions

Use the Sphinx `source` role in new pages:

````text
{source}`command loop <src/main.cpp>`
{source}`src/smt/smt_session.cpp`
````

`docs/_config.yml` configures this role through `sphinx.ext.extlinks`, with a
commit-pinned repository URL, and defines the displayed `source_revision`
substitution. Keep those revisions consistent. Validate referenced files against
that commit; the ordinary Sphinx build does not fetch GitHub or check source
path existence. `make docs-check-links` validates paths and line ranges against
local Git. An archive-aware manifest remains planned in
[](../documentation-restructuring.md).

The replacement book uses its own `docs-next/_config.yml`, with a single
commit-pinned URL and no revision substitution in chapter prose. Write those
pages as explanations of the code they accompany. Its review ledger records
which commit was inspected; exact revisions also belong in dated experiments.
Changing the central URL does not update or revalidate those evidence records.

Advance the source revision only after reviewing changes in affected code and
examples. A documentation-only commit need not change the solver revision.
Historical experiments must use explicit measured-commit links, not the
`source` role's current-revision default. Links for editing documentation on a
branch are a different purpose from links to the solver source being explained.

For MyST explicit targets such as `(h8)=`, use the label directly
(`[H8](#h8)`) rather than a path-and-fragment link that may be build-order
sensitive. Use notebooks for executable experiments or plots, not as a
requirement for reading the manual.

## Distribution status

The proposed website uses GitHub Pages with build artifacts for private review;
it has not yet been configured. See the
[hosting plan](../documentation-restructuring.md#website-and-deployment-plan).

The current `make archive` produces a deterministic source/HTML snapshot with
`SOURCE-REVISION`, but excludes Markdown/MyST sources and the new guide fixtures
under `docs/`. It is not yet the public release format promised by the rewrite.
Updating packaging and testing the extracted-archive workflow remain M6 work.
Use a repository checkout to rebuild the book or run these guide commands.

## Logo assets

The lowercase `primo` wordmark uses original SVG paths, navy lettering and a
teal dot. `docs/_config.yml` selects light and dark variants. Editable assets
are `docs/_static/primo-logo.svg`, `docs/_static/primo-logo-dark.svg`, and
`docs/_static/primo-avatar.svg`; `docs/_static/primo-avatar.png` is the
512-pixel profile export. They need no external font and follow the MIT license.

Both books use the white `p` on navy from `primo-avatar.png` as their favicon,
selected by `html.favicon` in each book's configuration. Keep the copy in
`docs-next/_static/` synchronized with the original until the draft replaces
the existing book.
