# Maintain the documentation

The book is authored in MyST Markdown under `docs/` and built with Jupyter Book
1.x. Source, tests and affected explanations belong in the same change.
The public contribution process remains a [draft](../contributing.md); the
repository's existing documentation requirements already apply.

## Build and preview

The replacement book is now developed separately in `docs-next/`. Use
`make docs-next` to build it, or `make docs-next-serve` for its preview at
`http://localhost:8001`. Its `review.md` and `review.csv` track page review and
acceptance. The commands below continue to build the existing book.

```bash
make docs
make docs-serve
```

`make docs` uses `scripts/build_docs.sh`, installs `docs/requirements.txt` into
`.venv-docs`, re-reads every page and fails on Sphinx warnings. Generated HTML
is in `docs/_build/html`. `make docs-serve` builds and serves it on port 8000.
Use `PRIMO_DOCS_VENV` to select another documentation virtual environment.

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
path existence. Source-path validation and an archive-aware manifest remain
planned automation in [](../documentation-restructuring.md).

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
