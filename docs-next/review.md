# Review this draft

This book is the replacement documentation under construction. The existing
book remains in `docs/`; it is still built by `make docs`. Only drafted pages
appear in this book's navigation. The complete proposed structure remains in
`docs/documentation-target-toc.yml`.

## Preview either version

```bash
make docs-next             # writes docs-next/_build/html/index.html
make docs-next-serve       # serves the draft on http://localhost:8001
```

The existing preview uses `make docs-serve` on port 8000. Stop a server with
Ctrl-C. Set `DOCS_NEXT_PORT` to use another draft port, for example
`make docs-next-serve DOCS_NEXT_PORT=8011`.

Both builds use `scripts/build_docs.sh`, the shared `.venv-docs` environment,
and `docs/requirements.txt`. Each has its own configuration, navigation, and
generated output. Notebook execution is disabled. The draft carries copies
of Primo's two original wordmark SVGs; keep those assets synchronized until
the draft replaces the existing book.

The build wrapper defaults to `docs`; setting `PRIMO_DOCS_DIR=docs-next`
selects the draft when invoking the wrapper directly. The Make targets select
their book explicitly, so a shell environment setting cannot make `make docs`
overwrite the draft or make `make docs-next` overwrite the existing book.

## Page review ledger

Documentation review state is separate from solver work state. An implemented
feature can have a draft explanation; an accepted explanation can describe an
unimplemented proposal accurately.

- **Draft:** the page exists but factual verification is incomplete.
- **Verified:** the recorded source/command/example checks passed; editorial
  and user review are still pending. This does not certify solver correctness.
- **Reviewed:** page review has occurred and its findings are resolved.
- **Accepted:** the user or designated reviewer explicitly accepts the page
  for the replacement book. A successful build never assigns this state.

```{csv-table}
:file: review.csv
:header-rows: 1
:widths: 18, 12, 14, 28, 28
```

The {download}`CSV ledger <review.csv>` is the record behind this table. Its
Source column records the commit inspected during verification. Those entries
are evidence of a review, so they change only when a new review is recorded.
The preview/review workflow is newer than the inspected solver baseline and
belongs to the current documentation branch. No page is accepted yet.

## Review order and questions

1. [Home page](index.md): does it explain Primo, offer a useful first action,
   and distinguish implemented behavior, ongoing work, and open leads?
2. [Correctness guide](development/correctness.md): can a contributor identify
   the required tests, fuzzing and independent model checks, recognize a failed
   or inconclusive gate, and preserve reproducible evidence?
3. [Architecture](implementation/architecture.md): can a developer follow a
   query and find the owning code, understand object lifetimes, and avoid
   mixing session, SAT, and theory responsibilities?
4. [Core representation](implementation/core.md): are identity, borrowing,
   constructor simplification, and storage retention clear enough to use safely?
5. [Library API](leads/library-api.md) and
   [incremental memory](leads/incremental-memory.md): do the work items capture
   the intended direction, available evidence, and unresolved design questions?
   Reviewing these leads does not complete their investigations.

Record findings by page and section, distinguishing factual corrections,
missing explanations, and editorial preferences. Fix them in that page's
next commit, update the ledger, and rebuild. Acceptance concerns the page's
declared scope; unfinished component chapters remain separate work.

## Code links are part of the review

Every implementation explanation should link to the owning type or function,
and to relevant regression tests. Use the centrally configured `source` role:

````text
{source}`SmtSession <src/smt/smt_session.hpp>`
{source}`execute_stream <src/main.cpp#L87-L119>`
````

These expand to GitHub URLs using the single commit-pinned URL under
`sphinx.config.extlinks.source` in `_config.yml`. Authors write paths and
optional line ranges; they do not repeat a commit hash in chapter prose.
Line anchors are appropriate for short functions and declarations; use a file
link when a mechanism spans many operations.

Write the manual as a description of the code it accompanies. Update an
explanation when behavior, ownership, or an interface changes. Advancing the
source-link revision is one configuration edit, and a documentation-only edit
does not require it. Keep exact revisions in this ledger and in dated experiment
records; changing the link configuration does not revalidate either record.

Before marking a page verified, check each path and line range against
`git show <revision>:<path>` and read the referenced code. Confirm that the
link points to the claimed operation, not merely a nearby symbol. When the
source revision advances, review changed code and update affected explanations
and anchors together. Ordinary Sphinx builds do not perform this Git/source
validation automatically. GitHub viewing requires repository access while
Primo is private.

## First draft batch: checks and limits

The three content pages were checked against the linked local source interfaces,
workflow definitions, and existing design notes. Code paths and anchor bounds
are checked against the pinned commit. The first-solve input is executed
separately from the book build. The test pointers in the architecture chapter
identify existing coverage; they are not a claim of new full solver qualification.

The infrastructure checks cover both strict book builds, shell syntax, lint,
separate preview outputs, and a rejected invalid book selection. The rendered
draft is inspected. Paper algorithms, historical measurements, and outside-reader
reproduction are not requalified by this work. Solver source and behavior are
unchanged; fresh solver fuzz, ASan, and clang-tidy campaigns are not part of
this documentation/build-wrapper change.

## Core chapter checks

The core representation draft was checked against the owning headers,
constructors, symbol-scope operations, session teardown paths, and model
ownership. Source links are checked against their configured commit. Its C++
example is extracted from the page, compiled against `primo_terms`, `primo_util`,
and GMP, and executed with assertions enabled. The existing regression suites
are rebuilt and run with:

```bash
cmake --build build --target primo_core_tests primo_binder_tests primo_model_tests primo_smt_session_tests --parallel 2
ctest --test-dir build -R '^primo_(core|binder|model|smt_session)_tests$' --output-on-failure
```

Both strict book builds and rendered-page checks complete this documentation
batch. This checks the example and its cited regression coverage; it does not
measure incremental memory or qualify a library API. No solver implementation
changes or fresh fuzz/ASan/static-analysis campaigns are included. The page
still needs user review and acceptance.

## Replacing the old book

The preserved baseline is tagged `docs-before-restructure-2026-09-24`; the tag
remains local until pushed. Use the original page map and compatibility
snapshot in `docs/` to account for material and old targets. Write a new page
from its code, original design notes, and useful prior explanations; extract
dated evidence separately. Do not bulk-copy the old book and call it rewritten.

Before switching, every required page needs acceptance, its source and examples
must be reconciled with the release revision, and historical material and old
links must have destinations. Test the clean-checkout and editable-archive
workflows. Then replace `docs/` with the accepted tree and remove the temporary
draft targets and duplicate assets. Until then, the two versions have separate
previews and there is no publication or automatic cutover.
