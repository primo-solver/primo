# Review this draft

This book is the replacement documentation under construction. The existing
book remains in `docs/`; it is still built by `make docs`. Only drafted pages
appear in this book's navigation. The complete proposed structure remains in
`docs/documentation-target-toc.yml`.

## Preview either version

```bash
make docs-next             # incremental build in docs-next/_build/html/
make docs-next-serve       # serves the draft on http://localhost:8001
make docs-next-full        # rebuild every draft page for validation
```

The existing preview uses `make docs-serve` on port 8000. Stop a server with
Ctrl-C. Set `DOCS_NEXT_PORT` to use another draft port, for example
`make docs-next-serve DOCS_NEXT_PORT=8011`.

Both builds use `scripts/build_docs.sh`, the shared `.venv-docs` environment,
and `docs/requirements.txt`. Each has its own configuration, navigation, and
generated output. Notebook execution is disabled. The draft carries copies
of Primo's two original wordmark SVGs and the `p` avatar PNG used as the favicon;
keep those assets synchronized until
the draft replaces the existing book.

The build wrapper defaults to `docs`; setting `PRIMO_DOCS_DIR=docs-next`
selects the draft when invoking the wrapper directly. The Make targets select
their book explicitly, so a shell environment setting cannot make `make docs`
overwrite the draft or make `make docs-next` overwrite the existing book.

Draft previews reuse Sphinx's cached documents and rebuild changed pages and
their dependants. The first build, or a configuration change, can rebuild
everything. Warnings remain errors. Use `make docs-next-full` for a complete
warning check before accepting edits; `make docs-check-links` rebuilds both
books fully before checking links. Direct wrapper calls can pass `--all` to
force a full draft build.

## Page review ledger

Documentation review state is separate from solver work state. An implemented
feature can have a draft explanation; an accepted explanation can describe an
unimplemented proposal accurately.

- **Draft:** the page exists but factual verification is incomplete.
- **Verified:** the recorded source/command/example checks passed; remaining
  editorial and reader checks are listed in the ledger. This does not certify
  solver correctness or complete content coverage.
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

```{admonition} TODO: Audit content coverage before accepting the replacement
:class: note

Extend the file-level migration map to account for every substantive old
section: preserved with an exact destination, intentionally superseded with
a reason, archived as dated history, or still missing with an inline TODO.
Resolve the coverage gaps before accepting a replacement chapter. Check that
current algorithms, correctness arguments, design rationale, examples, option
contracts, and evidence boundaries all have an explanatory home. A source
link or an old-page inventory is not by itself evidence of complete coverage.
```

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
5. [Boolean abstraction and SAT integration](implementation/sat.md): can a
   reader derive the worked clauses, distinguish definitions from assertions
   and assumptions, and find the lifetime and explanation obligations?
6. [DPLL(T)](implementation/dpllt.md): are assignment ownership, callback
   ordering, reason signs and lifetimes, and final consistency checks clear?
7. [EUF](implementation/euf.md) and [LRA](implementation/lra.md): can the reader
   follow each worked example through the owning code and understand the
   representations, invariants, defaults, limitations, and paper basis?
8. [Theory combination](implementation/combination.md): is the difference
   between entailment and a model choice clear, and can the reader identify
   the two production routes, their acceptance conditions, and their fallback
   and model-construction obligations?
9. [Quantifiers](implementation/quantifiers.md): can the reader follow binding,
   both polarities, term-pool growth, and queue scheduling, and explain why
   exhausting candidates does not establish `sat`?
10. [Model construction](implementation/models.md): can the reader follow
   authoritative values, evaluated function tuples, and reverse replay, and
   explain why an evaluator cannot survive a change of interpretation?
11. [Library API](leads/library-api.md) and
   [incremental memory](leads/incremental-memory.md), together with
   [compact term storage](leads/term-storage.md): do the work items capture
   the intended direction, available evidence, and unresolved design questions?
   Apply the same questions to the remaining lead pages. Reviewing a lead
   does not complete its investigation.
12. Literature notes: are the cited sections, implemented subsets, deviations,
   and open questions clear? Keep published results attached to their original
   configurations and evidence.

Record findings by page and section, distinguishing factual corrections,
missing explanations, and editorial preferences. Fix them in that page's
next commit, update the ledger, and rebuild. Acceptance concerns the page's
declared scope; unfinished component chapters remain separate work.

## Work states and writing conventions

Use three work states consistently in the home page, navigation, and lead pages:

- **Implemented:** behavior present in the linked code. State whether it is
  enabled by default or opt-in, and identify its supported scope.
- **Active work:** an investigation, implementation, or documentation task
  currently being carried out, with its next step recorded.
- **Open leads:** candidates for investigation. Each lead names its owner and
  separates completed inspection or probes from pending design and evaluation.

These describe project work; the page-review states above describe documentation.
An initial probe does not by itself make a lead an active implementation.

Implementation chapters explain current behavior in the present tense. Lead
with the problem and a concrete example, connect operations to their owning
types, and explain invariants, lifetimes, limitations, and sources. Link type
and function names directly to code; give regression links descriptive labels.
Define acronyms and internal terms at first use. Put historical identifiers
such as H1 or D2 after the mechanism's descriptive name.

Keep design tradeoffs in the chapter. Put repair chronology, commands already
run, and verification outcomes in dated review or experiment records, linked
from the explanation. State each limitation where it helps the reader; avoid
repeating general disclaimers about what writing a chapter does not establish.
Paper-specific limitations and incomplete validation remain explicit.

Visible TODO callouts record unfinished documentation from the coverage review;
they do not mark solver features as unimplemented or create new research leads.
Keep each TODO beside the explanation it qualifies, with a concrete completion
criterion. Before removing it, check that a reader can reconstruct the argument
or worked step without inferring missing conventions from source code. Preserve
the full reasoning in its owning chapter or a linked algorithm note, while
keeping dated experiments and abandoned designs in the evidence archive.

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

## Automated link checks

```{include} ../docs/development/documentation.md
:start-after: "## Check hyperlinks"
:end-before: "## Page roles"
```

## First draft batch: checks and limits

The records below describe successive reviews at their recorded revisions.
Statements about source pins, page counts, and pending work belong to those
reviews; later records may supersede them. The configuration holds the current
source-link pin, and the page ledger records outstanding acceptance checks.

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

## SAT chapter checks

The Boolean abstraction/SAT draft was checked against the clausifier, atom
registry, backend adapter, scoped engines, and relevant regression tests.
Tseitin §1 (English reprint p. 467) and IPASIR-UP §§2.1–2.4 were read for its
paper-to-code checklist. The chapter distinguishes current root rules from
the rejected root-polarity prototype and records interface omissions and
compatibility differences.

The executable example uses the current internals without frontend or
preprocessing passes. It checks four variables/four clauses before search,
all eight primary assignments, both guard settings over those assignments,
and permanent selector retirement. With the normal dependencies installed:

```bash
cmake --build build --target primo_boolean_solver_tests primo_sat_backend_tests primo_smt_session_tests primo_quantifier_solver_tests primo_component_combination_tests --parallel 2
c++ -std=c++20 -Wall -Wextra -Werror -UNDEBUG \
  -Isrc -isystem contrib/local/include \
  docs-next/examples/boolean-abstraction.cpp \
  build/libprimo_smt.a build/libprimo_sat.a contrib/local/lib/libcadical.a \
  build/libprimo_terms.a build/libprimo_util.a \
  contrib/local/lib/libgmpxx.a contrib/local/lib/libgmp.a \
  -o /tmp/primo-boolean-abstraction
/tmp/primo-boolean-abstraction
ctest --test-dir build -R '^primo_(boolean_solver|sat_backend|smt_session|quantifier_solver|component_combination)_tests$' --output-on-failure
```

This command assumes the existing `build/` configuration and default static
dependency installation. The example reports 24 checked assignments and a
rejected attempt to reactivate a retired selector. It fails explicitly if
CaDiCaL is unavailable; it must run with assertions enabled (`-UNDEBUG`).
All five regression suites passed. Both strict book builds, source-link
validation, and rendered-page inspection passed. This documentation/example
change does not alter solver behavior; no new performance, fuzz, ASan, or
whole-tree static-analysis qualification is claimed. The chapter and SAT
implementation-template pilot still require user review and acceptance.

(dpllt-chapter-checks)=
## DPLL(T) chapter checks

The DPLL(T) draft was checked against the dedicated EUF/LRA drivers,
`ComponentDispatcher`, `TheoryComponent`, the component solver/coordinator,
the SAT adapter, and session model finalization. The reading covers
Nieuwenhuis–Oliveras–Tinelli (2006), §§4.1, 4.4, 5.1–5.2, and IPASIR-UP
(2023), §§2.1–2.4, from the primary PDFs already used by this book. The
chapter is the local representation/invariant/operation checklist for these
integration mechanisms, with explicit limits on completeness and retention.
No other solver implementation was used as an implementation source.

The unused `src/smt/dpllt.hpp` empty class and its include-only `.cpp` were
removed after a whole-tree reference search found no users. CMake discovers
SMT sources with `CONFIGURE_DEPENDS`; a successful rebuild verifies removal.
No active driver, callback, solver option, or runtime behavior changed. Existing
source links remain pinned to the unchanged implementations at the configured
revision; none of them points to the deleted scaffold. Harmonizing the actual
drivers remains a separate design decision.

From the repository root, reproduce the example matrix with:

```bash
make check
python3 - <<'PY'
import subprocess
profiles = [
    [],
    ['--preprocessing-mode=off', '--euf-prop=online'],
    ['--preprocessing-mode=off', '--euf-prop=conflict'],
    ['--preprocessing-mode=off', '--euf-engine=lazy', '--euf-prop=off'],
    ['--preprocessing-mode=off', '--incremental-mode=assumptions', '--euf-prop=online'],
]
for flags in profiles:
    result = subprocess.run(
        ['./build/primo', '--quiet', '--check-model', *flags,
         'docs-next/examples/dpllt-equality.smt2'],
        check=True, text=True, capture_output=True, timeout=30)
    assert result.stdout.split() == ['sat', 'unsat', 'sat'], result.stdout
print('5 profiles: 15 expected answers; requested SAT models checked internally')
PY
make docs
make docs-next
```

All five profiles produced the expected answers. This checks the input's
semantics and selected routes, not the illustrative SAT decision order or
an exact learned-clause trace. The chapter links existing callback, explanation,
and session regressions; `make check` passed lint, all 41 CTest suites, and
188 Python tests after the scaffold removal. No new solver fuzz campaign,
ASan run, or external corpus/model-validation claim is made for this
documentation and unreachable-scaffold cleanup.

Verification also covers source paths/line bounds, both strict book builds,
local HTML links, navigation/ledger coverage, and preservation of the original
book targets. The draft still needs user acceptance. The review distinguishes
partial callback checks from final reconciliation, live reasons from candidate
queues, and the component dispatcher's forgettable clauses from protected
dedicated EUF/LRA conflicts. No common retention policy is claimed.

The code review identified a separate EUF marker/closure rollback mismatch:
with `r` root literals, the marker boundaries became `{0, r}` at the first
decision, so `apply_backtrack(0)` cleared to zero rather than `r`. The subsequent
focused correctness investigation below reproduces lost propagation and
repairs the indexing. The scaffold-removal verification above is historical;
it does not itself qualify the later behavior change.

`make tidy` completed over all 123 tracked source/test `.cpp` files remaining
after removal. It reports 18 enum-cast diagnostics in the unchanged generic
option-name tests and one potential leak in the external CLI11 header. These
are outside the removed stub; they were not fixed or presented as a clean
static-analysis baseline. `make include-cleaner` completed with 390 advisory
diagnostics. Of its 22 unused-header suggestions, 21 are paired self-includes;
the remaining `<variant>` include supplies `std::get` in
`instance_simplifier_tests.cpp`. Those includes were retained. The other
advisories concern direct includes and remain a separate hygiene task.

Code-review verdict for the scaffold removal: no blocking issues found.
The automated scans cover the whole tracked C++ tree; manual review focuses
on the removed API's users/build wiring and the chapter's integration paths.
The identified EUF synchronization issue remains explicitly open, so this
verdict is not a claim that all solver invariants have been verified.

## Architecture figures

The architecture page has a data-flow map of the default rebuild route and a
simplified lazy SMT loop adapted from the user-provided `sandbox/pic.tex`.
The map follows the linked command/session/frontend and solver-dispatch code.
The loop is explanatory: the accompanying text identifies online callbacks,
omitted outcomes, and the separate quantified-UF completion limits.

Editable sources are retained with the book:
{download}`query-flow.tex <figures/query-flow.tex>` and
{download}`lazy-smt-loop.tex <figures/lazy-smt-loop.tex>`. Their comments record
the meaning and routing corridor of each edge. SVG exports are checked in under
`_static/`; the ordinary book build needs no TeX installation. Regeneration
requires `pdflatex` with TikZ and the `standalone` class, and Poppler's
`pdftocairo`. From the repository root:

```bash
figure_build=$(mktemp -d)
for figure in query-flow lazy-smt-loop; do
  pdflatex -halt-on-error -interaction=nonstopmode \
    -output-directory="$figure_build" "docs-next/figures/$figure.tex" || break
  pdftocairo -svg "$figure_build/$figure.pdf" \
    "docs-next/_static/$figure.svg" || break
done
```

Temporary PDFs and logs stay in `$figure_build` for inspection. After any
diagram edit, regenerate its SVG, inspect the PDF and HTML for legible labels,
arrow direction, overlaps, and clipping, then run both strict book builds.
Keep source and export in the same commit. The current figures passed those
checks and their accompanying source links were verified locally. This is a
documentation check, not a new solver correctness gate or page acceptance.

## Yices literature checks

The 2026-09-24 survey reads all three local Yices PDFs and checks their
Ackermann/explanation claims against the configured solver source revision.
The 2006 paper's p. 3 and the 2016 talk's printed slides 13–14 were also
inspected as rendered pages. Official Yices links identify the original
publications. Local reading-copy SHA-256 values are:

```text
yices-tool-paper-2006.pdf  f85699440a641ce4247e57ec5aecf5fd99d126ae1a39692b91a47c8399818fde
yices-cav2014.pdf          c8f968f526218c5cb00ad84f904e44c501b37c26f5ed31fe1baa3f6d19990060
yices-smt2016-talk.pdf     b8ee6c2f1fa7442a76776130387b9e55499f434a7ca07c89bddc82e1c153e8c7
```

`docs/papers/yices.md` is the single maintained note; the draft's
`papers/yices.md` includes it. Both books use the same source-link role.
Check both builds and both rendered copies after edits. The draft review and
support ledgers record the wrapper; the old-book page map records the source.
Move the maintained content into the accepted tree before removing `docs-next/`
at final cutover.

The backend comparison uses the installed public `cadical.hpp` API from
CaDiCaL `rel-3.0.0`, checkout `7b99c07f0bcab5824a5a3ce62c7066554017f641`,
and Primo's adapter. It establishes a documented reason-retention mechanism,
not measured deletion behavior. The header's conflicting default descriptions
are recorded as an investigation question. No Yices implementation source was
used. The older Ackermann run remains historical and `needs_revalidation`.

Verification covers `make docs`, `make docs-next`, source paths/line ranges,
local HTML links/anchors, navigation/ledger coverage, preserved baseline HTML
targets, and rendered inspection. No solver code or option changes, fresh
performance results, or fuzz/ASan/clang-tidy claims belong to this survey.
Representation equivalence for Yices parent hooks and ITE transformations is
explicitly deferred to the corresponding chapter audits. User acceptance of
the literature page and the open leads remains pending.

## Clause-forgetting literature checks

The 2026-09-24 follow-up reads the sections named in
[the literature note](papers/clause-forgetting.md) from primary PDFs. The
Blanchette et al. source is the four-author JAR 2018 version, whose restart/
forget strategy is §4.5; the shorter conference version numbers it §4.4.
The three reading copies have these SHA-256 values:

```text
NieOT-JACM-06.pdf          30df2e90891fd19228f2f76c442a1fa8d13af9bc0dc0abf4aef191a57afcdb15
Blanchette-et-al-JAR.pdf   df39fe3e417c0a386fd92cd1d98a588916228a6dc00d79a3d08a6e1208777fe1
LIPIcs.SAT.2025.14.pdf     06a34e609ffcaee547e40d789513e20330c9c6dcb5f19672d0d357a83ba0791a
```

The maintained page is `docs/papers/clause-forgetting.md`; the draft includes
it through `papers/clause-forgetting.md`, with the same cutover rule as the
Yices note. Source inspection covers callback flags, reason streaming, the
adapter API, and the component bridge. It does not measure effective backend
deletion or establish a termination theorem for Primo. No other solver's
implementation source was used.

Checks cover both strict builds, source paths and anchor bounds, local HTML
links, navigation/ledger coverage, preserved baseline targets, identical shared
note rendering, and visual inspection. The new lead remains open, with user
review and all implementation/evaluation work pending. This is documentation
only; solver tests, fuzz campaigns, ASan, and clang-tidy are not rerun.

## Literal-capacity documentation check

The SAT chapter's range note was checked against `sat::Lit`, both backend
allocation paths, and the installed CaDiCaL public literal contract on
2026-09-24. It records unchecked signed increments and the distinction between
the representation's upper bound and supported backend capacity. This is an
open hardening item, with no observed overflow, capacity stress run, code fix,
or completed boundary tests. Both book builds and source/local-link checks
cover the documentation change; the implementation remains deferred.

## Directional CNF and statistics review

The 2026-09-24 follow-up adds a shared
[polarity-aware CNF note](papers/polarity-aware-cnf.md), with precise reading
scope and an explicit access gap for the original 1986 paper. Algorithmic
claims use the accessible 2025 primary SMT paper; the original first-order
translation is not claimed reviewed. Reading-copy SHA-256 values:

```text
arXiv-2303.14971v7.pdf  007fd0b665579bea739c7e2fa0c2d835f2d1b1171716dc55f09107378f3fe2bf
SMTInterpol-2023.pdf   33da7a2b387a04722d2b888c5d573d160ab63ff3dd4040185fe995a02e41366f
```

The canonical page is `docs/papers/polarity-aware-cnf.md`, included by the draft
wrapper. Relocate it at book cutover, as for the other shared literature notes.
The chapter's current full definitions were checked against the clausifier;
the positive-only and negative-only examples were exhaustively checked over
their original atoms with existential projection of the auxiliary.

The [statistics lead](leads/statistics-instrumentation.md) records source
inspection of typed/string updates, local EUF counters, timers, diagnostic
traversals, CLI printing, and conflict-budget consumers. It is an initial
audit and design preference, not an implemented switch, whole-tree
classification, or performance measurement. String-key diagnostics should be
absent from production; operational accounting
must remain. Public library layout and counter availability remain design work.

Verification covers both strict book builds, source links, local HTML targets,
shared note rendering, navigation/ledger coverage, and baseline compatibility.
No solver code changed; solver tests, fuzzing, ASan, and clang-tidy are not
rerun for these documentation additions. User review remains pending.

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


## EUF callback rollback regression

The new `primo_euf_callback_tests` suite uses the existing production EUF
component factory, without exposing private state or depending on SAT decisions.
On the code before repair, root retention, retained-level retention, and
empty-level/repeated-backtrack cases fail with **lost equality propagation**.
Conflict explanations and reassignment/exported-model-fact controls pass.
The corrected decision-boundary convention passes all five groups. Every
returned explanation is independently checked by exhaustive interpretations
of the five fixture constants and against the notified literal assignment.
No incorrect SAT/UNSAT answer was reproduced.

The existing mixed QF_UFLRA rejected-candidate-merge regression is also
strengthened to produce and validate a model. Its previous expectation of a
positive model-callback recovery count depended on the lost markers; the
corrected trace requires zero recovery assignments while retaining the closure
reuse and compatibility-split checks. This is end-to-end coverage of model
reconciliation, not a deterministic test of the deferred-callback queue.

Reproduce the focused check with:

```bash
cmake --build build --target primo_euf_callback_tests
ctest --test-dir build -R primo_euf_callback_tests --output-on-failure
```

`benchmarks/smoke/qf_uf/euf_retained_equalities_unsat.smt2` adds an end-to-end
UNSAT input whose root equalities refute either disjunct. Its decision order is
unspecified; it complements the deterministic callback tests. The consolidated
assignment-view design remains an open lead. Direct tests of borrowed model
scope callback deferral and every combined-theory schedule remain limitations.

Regression verification: `make check` passes all 42 CTest suites and 188 Python
tests, including formatting/lint; `make asan-test` passes all 42 suites with
leak detection. Both strict books build, local links resolve, and all 83
baseline pages/1223 HTML IDs are preserved. The new UNSAT smoke case agrees
with Z3 and cvc5 under default, forced online, conflict-only, lazy, and
assumptions profiles. Fresh fuzz qualification is recorded separately in the
run ledger after the repair commit; these unit results alone are not a full
solver correctness claim.

The pre-commit code/documentation review found no blocking issue in the focused
repair. `make tidy` scanned all 124 tracked source/test translation units: the
19 emitted diagnostics match the previous baseline (18 enum-cast warnings in
unchanged option-name tests and one external CLI11 potential-leak warning).
`make include-cleaner` reported the same 390 advisories as the previous scan;
its 22 unused-header suggestions are 21 paired self-includes and the actually
used `<variant>` include in `instance_simplifier_tests.cpp`. No new diagnostic
was introduced. The source change adds no scans or instrumentation; this is a
correctness repair, with no performance/default-promotion claim. Baseline
static-analysis cleanup and assignment-owner consolidation remain separate.


Fresh merge-gate evidence is recorded in `docs/run-log.json`, entry
`2026-09-24-euf-assignment-rollback-ac2aaaf`. The release binary built from
`ac2aaaf` passed 1,000 QF_UF FuzzSMT cases against Z3/cvc5, 1,000 mixed QF_UFLRA
cases against Z3 with online EUF enabled, and 1,000 QF_UFLIA component cases
against Z3/cvc5. All use seed 20260924 and five-second process timeouts, with
preprocessing disabled and online EUF propagation selected. There were no
wrong answers, crashes, parser/type errors, timeouts, unsupported cases, unknown
answers, or reference disagreements. All generated inputs and CSV summaries
remain in `fuzz-results/euf-rollback-20260924-ac2aaaf/`; exact commands, versions,
binary and summary hashes, resource limits, and local artifact status are in
the ledger. This is answer comparison, not independent printed-model validation
or a performance/default-promotion campaign.

Both books now pin source links centrally to the repair commit; the 37
solver-engine line ranges were advanced against its diff and checked. Other
chapter source bodies are unchanged from the prior audit. The earlier dated
reviews retain their original source/evidence revisions.


## Shared assignment migration review

The assignment protocol now has one owner per active SAT connection. All
production callback consumers read `sat::TrailView`; lazy routes use a completed
model snapshot. Theory solver checkpoints and tentative model-equality undo remain
separate. Full-model token serials identify reconciliation rounds, not a second
Boolean assignment. The old rollback audit above records the pre-migration
implementation and its evidence.

The coordinator tests exercise root retention, empty levels, duplicates,
contradictory/invalid notifications, reentrant delivery, protected model scopes,
registration growth, reason callbacks and reconnect. Protection freezes the
whole stream; replay rechecks whether a consumer opened a new protected scope.
The real backend reason test also exercises fresh-variable propagation through
the shared owner before disconnect and reconnect. Component model callbacks
reject unnotified atoms without backfilling them.

Two existing mixed-arithmetic tests caught a migration error: generated
interface atoms were not observed between SAT rounds when only arithmetic was
online. Observing every new atom after child registration, before each solve,
restores both the Bool-to-Real congruence UNSAT case and model reconstruction.
The existing model-reconciliation regression still checks the produced model,
closure reuse and compatibility splitting. The obsolete backfill counter is
removed; deferred notification counts now belong to the coordinator.

`make check` passes 43 CTest groups and 188 Python tests. `make asan-test` passes
all 43 groups; the final reason/reconnect test also passes separately under
ASan. Static analysis covers all 128 tracked C++ translation units, including
documentation examples. Its 19 diagnostics match the baseline: 18 deliberate
out-of-range enum casts in an option test and one external CLI11 diagnostic.
The include-cleaner advisory has no newly identified removable include; paired
self-includes are retained. The registry iteration at the observation boundary
copies its entry before callbacks can grow the registry.

Efficiency review: callback assignment and rollback operate on deltas; the
normal path does not allocate an event copy. Only deferred/reentrant events
are queued. Final-model completeness scans remain linear in registered atoms;
quantifiers temporarily sort their projection by assignment position. No
performance improvement is claimed. Fresh SLURM six-logic gates, retained-session checks and the paired 120-input
baseline comparison are recorded in `2026-09-25-shared-assignment-slurm`.
All four profiles retain their solved counts. All 240 SAT rows were replayed:
220 direct Dolmen passes and 20 replays of five generated LRA inputs using
the wider `QF_NRA` validator logic for constant-denominator syntax, on the same
saved assertions/models. Those 20 remain incomplete under strict
declared-logic validation. Default mixed search counters can change; two
profiles narrowly exceed the strict 1% common-runtime threshold. No performance
promotion is claimed. Both books pin source links to `45e25a6`;
shifted ranges were mapped and changed spans reviewed against the owning code.


## Boolean-domain cleanup and explanation wording

The completed A/B comparison no longer needs a selectable bridge encoding.
`e6d9fcb` removes that encoding, its option and its counters. The registration
traversal and per-term operation are unchanged; generated-term, polarity,
congruence and preparation-idempotence tests remain. Both books now pin their
source links to this cleanup revision, with shifted ranges checked against the
diff. Historical comparison results remain tied to their original revisions.

The DPLL(T) explanation section now uses `a ∧ ¬b ⇒ q`, whose SAT clause is
`¬a ∨ b ∨ q`. Its table distinguishes storing the premises `[a, ¬b]` from
storing the other clause literals `[¬a, b]`, and states where negation occurs.

The run-log entry `2026-09-25-bridge-atoms-removal` records the SLURM build,
43 normal and 43 ASan CTest groups, 194 Python tests, 1,000 Boolean QF_UF cases,
1,000 mixed cases plus 112 finite cases, and 1,000 quantified cases across six
profiles. The quantified generator produced 564 distinct inputs; every Primo
answer was reference-confirmed. All 1,209 independently checked SAT models
passed Dolmen. This is targeted cleanup qualification, not a full-corpus or
performance evaluation. Quantified models were not independently validated.

The first Boolean model gate mistakenly requested models for UNSAT inputs.
`6f46e32` fixes its selection and adds mocked regressions. The failed attempt
is preserved separately; the repaired gate passed on a fresh run. Whole-tree
static analysis covers 128 C++ translation units, with 17 expected analyzer
diagnostics and no blocking code-review finding. No page acceptance is implied
by these checks; reader review remains pending.

## EUF chapter checks

The new [EUF chapter](implementation/euf.md) follows `FlatCongruenceClosure`
through a conflict and backtrack. Its source review uses the existing
`e6d9fcb` pin: `FlatTermIndex`, `FlatCongruenceClosure`, `ProofForest`, the
prepared/working closures and callback paths in `solver_engines.cpp`, Boolean
argument registration, and the typed model-builder boundary. The chapter maps
the relevant tests to representation, explanation, rollback, and propagation
obligations. The worked trace is explanatory; it does not claim a particular
preprocessor or SAT search order.

The local primary PDF `docs/oliveras-euf.pdf` was read at Sections 3.1–3.5,
4, and 5.2–5.5. The representation introduction now cites Sections 3.1.1 and
3.1.2 at the point of use, distinguishes currying from flattening, and states
that `FlatTermId`s are local to their `FlatTermIndex`.
The review separates the default BFS mechanism from the
optional proof forests and the paper's complexity results. It also records
Primo's sparse undo policy, structural-alias restrictions, and the bounded
existing-atom Ackermann mode. Broader heuristic proposals and experiment
histories remain in the existing book; their migration is not marked complete.

This is a documentation-only change. The existing SLURM qualification is
recorded above; no solver batch was rerun for this chapter. `make docs` and
`make docs-next` pass their strict full builds. Source-link targets and line
ranges were checked against the shared pin; rendered local links resolve.
All 17 draft pages have navigation and ledger entries. The old book's 83
captured pages and 1,223 anchors remain available.

The documentation self-review found no remaining blocking discrepancy in this
chapter. Reader review of the explanation, depth, and chapter boundaries
remains pending; the checks do not mark the page accepted.

## QF_LRA publication and next chapter

On 2026-09-25, the PDF of Janota and Jakubův's
[results paper](papers/primo-qflra-results.md), arXiv:2609.16706v1, was read.
The shared note records selected rows of Tables 1–2, the Figure 3 configuration,
resource limits and reference versions. Current option defaults were checked
in `CheckSatOptions`. The paper's aggregate results still need a verified
mapping to exact Primo revisions and raw experiment records; no solver runs
or new qualification claims were made. The maintained note lives in
`docs/papers/primo-qflra-results.md`; the draft includes it, following the
same relocation rule as the other shared notes at book cutover.

Both strict book builds pass. All 18 draft pages have navigation, review and
support entries; 3,679 local HTML links/assets resolve, and the old book's
83 captured pages and 1,223 anchors remain available. The two books render
identical publication-note content. Source-link paths and line ranges resolve
against the shared pin. These editorial checks do not constitute acceptance
of the performance claims or completion of the LRA chapter.

The initial LRA audit was blocked by HTTP 403 responses for Griggio and
Bromberger, an older Griggio URL timeout, and a HAL bot challenge. The user
subsequently supplied the primary PDFs in `external/papers/`, resolving that
blocker. The completed source reading for the chapter is recorded below;
the earlier access failures are not the current status.

## LRA chapter checks

The [linear real arithmetic chapter](implementation/lra.md) was drafted against
the shared source pin on 2026-09-25. It covers preparation and direct bounds,
exact numbers and infinitesimals, sparse rows and reverse occurrences, repair
and termination, conflict signs, online propagation, rollback and model values.
The trace `x + y <= 2`, `x >= 2`, `y > 0` is a mathematical explanation of the
operations, not a captured SAT execution or a new solver test.

The rewrite preserves the substantial arithmetic work. An explicit inventory
links H1/H2/H3, H5/H7/H8/H9/H10, SOI, preprocessing, equality sharing,
assumption-based sessions and their experiment records. The existing arithmetic
page, diagrams, detailed notes, rejected approaches and run ledger remain in
place. Historical extraction and the integer/model/combination chapters are
not marked complete by this LRA draft.

### Primary sources read

The supplied PDFs remain ignored local reference material. These are the
sections used in this audit, not a claim that every supplied paper was read
in full:

| File or source | Reading and use |
| --- | --- |
| `king_tim.pdf` | §§2.2.7, 2.2.12, 3.2–3.3: sparse structures, eventual Bland fallback and SOI. |
| `griggio.pdf` | §2.5.1–2.5.3: fixed-equation invariants, exact-number/storage choices and least-violated selection. |
| `Bromberger_Decision_Procedures_for_Linear_Arithmetic.pdf` | §§7.1.3–7.1.4: rollback alternatives and representation; opening parts of §§7.1.1 and 8.1.1 for context. The earlier Chapter 5 reading remains in its existing note; no fresh audit of the complete equality-basis algorithm was done. |
| `Brummayer-PhD-Thesis-2009.pdf` | §4.1, introduction to §4.2, §4.3 and FuzzSMT scope in §4.4.2: contributor correctness guidance, not a simplex source. |
| `Monniaux-cav09.pdf` | §§2.1–2.2, the sparse forced-pivot ordering discussion in §3, and §4's rational representation. The downloaded arXiv copy was also consulted. |
| `arithmetic-reimplemented.pdf` | §§2–3 and 6: exact sparse arithmetic, direct/indirect propagation, optional model-unit equality producer and combination boundary. The integer/nonlinear sections are not re-audited here. |
| `SPASS-SATT.pdf` | §3, especially footnote 3: model-based phase recommendations. |
| `Presolving.pdf` | §3.1, especially (iv): fixed-variable elimination as motivation; distinction from Primo's reversible pivot-based adaptation. |
| Dutertre–de Moura, SRI-CSL-06-01 | §§3.1–3.3, downloaded primary PDF: preparation, repair/termination, explanations, rollback, propagation and strict bounds. |

The source review corrected stale King and Griggio comparisons: Primo now
has indexed occurrence removal rather than a linear column scan, and the
least-violated rule is implemented as an opt-in alternative. The chapter also
distinguishes Bromberger's sorted linked rows and common denominators from
Primo's sorted vectors and per-entry rationals. Monniaux's normalization,
sparsity rationale and two-tier arithmetic are credited without claiming his
floating-point acceleration. The 2024 model-unit producer remains optional.
The base-paper note also now distinguishes the report's saved successful
assignment from Primo's retained-assignment rollback and records when the
occurrence index actually needs rebuilding.

The original tableau diagram is reused from the existing book. A browser
check found that its SVG-font export lost glyphs and typography in Chrome;
it was regenerated from the unchanged TikZ source with outlined glyphs:

```bash
mkdir -p /tmp/primo-lra-figure
pdflatex -interaction=nonstopmode -halt-on-error \
  -output-directory=/tmp/primo-lra-figure \
  docs/figures/lra-tableau-occurrence-index.tex
dvisvgm --pdf --no-fonts \
  --output=docs/_static/lra-tableau-occurrence-index.svg \
  /tmp/primo-lra-figure/lra-tableau-occurrence-index.pdf
```

The regenerated SVG was inspected in headless Chrome. Both books use the
same maintained figure; the draft does not introduce a second editable copy.

### Verification and acceptance boundary

Both strict book builds, source-link checks, navigation/ledger coverage,
rendered content/asset checks and the old-page compatibility audit pass for
this chapter. All 19 draft pages have navigation and ledger entries; all
83 baseline pages and 1,223 captured anchors remain available. Solver source is unchanged; no local or remote solver batch
was run. Existing tests are linked by the invariant they exercise, without
claiming a fresh fuzz or benchmark qualification. Reader acceptance, complete
historical extraction and the published table-to-artifact mapping remain open.

### Normalization clarification and D2 lead

The LRA preparation explanation now replaces the unexplained H1a/H1b labels
with the two concrete savings and compares normalization on
`4*x + 6*y <= 10`: the paper's integer coefficients become `2, 3`, whereas
Primo's become `1, 3/2`. Both scale the threshold as well. The explanation
was checked against `canonical_polynomial`, `append_atom` and Monniaux §2.1.

The existing [D2 lead](leads/lra-floating-point.md) is now visible in the draft's
Open work navigation and linked from the LRA chapter. Monniaux §3, Algorithm 2
and its basis-feasibility argument were read for this extraction. The original
investigation and dated comparisons remain in place; no algorithm was added
or solver experiment run. Integration, evaluation and reader acceptance remain
open. Both strict books and source/HTML link checks pass with 20 draft pages;
the existing page and anchor compatibility baseline is preserved.

### Why leading-coefficient normalization was chosen

The follow-up history check inspected the original design (`d5e1a54`), the
[H1b implementation commit](https://github.com/primo-solver/primo/commit/1e6554db8d5688bcb817c49c1ec6607dbb5c9ad5),
the early-canonicalization note and
current code. The original objective was proportional-row sharing; no
isolated comparison with coprime-integer tableau normalization was found.
The chapter now distinguishes the simple scaling operation from a demonstrated
performance advantage, records the possible cost of introducing fractions,
and separates it from the default-off primitive-integer core rewrite.
The earlier {source}`LIA arithmetic profile <docs/qf-lia-profile.md#L171-L176>`
also raised the cost question; it did not isolate the two normalization forms.
Stale descriptions of that early pass as mandatory were corrected in the
retained arithmetic and H1 notes. These are documentation corrections;
normalization behavior and option defaults are unchanged.
Both strict books and source/HTML link checks pass; the old page/anchor
baseline is preserved. No solver batch or new performance experiment was run.

### Rational layout lead

The rational-storage question is recorded separately from the outer term-payload
union. The current fields and constructor/copy ownership were inspected; the
new `examples/rational-layout.cpp` was formatted, compiled with C++20 and
warnings as errors, and run locally without invoking the solver.
On GCC 13.3.0/libstdc++, current and raw-pointer layouts are both 24 bytes;
the pointer itself is eight bytes in either case. The separate-tag union
remains 24 bytes, the denominator-tag union is 16, and the inline-GMP union
is 40. Candidate delta values and row entries measure 32 bytes each versus
48 and 40 currently. No candidate is constructed or used for arithmetic.

The draft now has 21 pages with navigation/review/support entries. Both strict
book builds, source/HTML links and old page/anchor compatibility checks pass.
Lifetime design, implementation, solver qualification and memory/runtime
evaluation remain open; no solver batch ran for this documentation change.

## Editorial consistency pass — 2026-09-26

This pass covers the draft implementation and contributor chapters, home page,
eight lead pages, and four shared literature notes. It resolves the style review
by standardizing work states, defining terminology, linking representation types
directly, and replacing repeated editorial caveats with specific limitations.
The LRA equality-sharing explanation uses row examples checked against
`model_filtered_unit_equalities`; no executable example changed.

Repair chronology and detailed qualification counts now live in this review
record, with links from DPLL(T). The normalization rationale keeps the technical
tradeoffs in LRA and the historical evidence here. Published measurements,
rejected experiments, source attribution, validation gaps, and option defaults
are preserved. The source-link revision remains unchanged.

Contributor fuzzing requirements concern coverage, outcomes, and reproducible
evidence, independent of which machine a contributor uses. This clarification
does not change the workspace's agent execution instructions. Public contribution
governance and sign-off remain outside the correctness guide's scope.

Reading-copy details remain here rather than in chapter introductions: the EUF
review used `docs/oliveras-euf.pdf`; the Yices copies listed above have 5, 8, and
30 PDF pages respectively. The original Plaisted–Greenbaum paper's publisher
returned HTTP 403, and the author's reprint listing offered no download; the
directional-encoding account therefore uses the accessible 2025 primary paper.

Verification passed: `make docs`, `make docs-next`, source-link targets and line
bounds, rendered local links, shared-note consistency, navigation and ledger
coverage, and preservation of old-book and pre-edit draft anchors.
`git diff --check` also passed. Only documentation changed; executable examples
are unchanged, and no solver qualification was rerun. Page-acceptance states
are unchanged; reader acceptance and remaining factual investigations stay
listed in the ledger.

## Hyperlink-checking pass, 2026-09-26

The new `make docs-check-links` command passed for both books: 113 generated
pages, 17,230 local links, and 356 distinct Primo Git URLs. Seven focused Python
tests cover successful checks and failures, including missing anchors, URL
encoding, unavailable Git history, and line ranges checked at the cited commit.
Both strict book builds, `make lint`, and the checker self-review passed.
No solver behavior changed or solver qualification was rerun.

`make docs-check-external` completed both Sphinx audits and returned failure:
12 URLs in the existing book and two in the draft remain unverified. Ten
returned HTTP 403, two were rate-limited with HTTP 429, one had a TLS certificate
error, and one refused the connection. These are access failures, not evidence
that the cited papers have disappeared. The
{download}`audit snapshot <../docs/documentation-link-audit.json>` records the
URLs, source locations, tool versions, and results. It is evidence from this
run, not an exclusion list; subsequent runs check those URLs again.

The pass also fixed accidental web links created from bare `AGENTS.md` text
and used literal HTML anchors for two GitHub headings. The linked CaDiCaL
header's dynamic line fragment has a documented anchor-only exception; its
page remains subject to HTTP checking. The solver source pin and page-acceptance
states are unchanged. External access failures remain open for review.

(theory-combination-chapter)=
## Theory-combination chapter, 2026-09-27

The new chapter consolidates the current contracts of `docs/nelson-oppen.md`,
`docs/modular-theory-combination.md`, and `docs/n-way-combination-migration.md`.
It explains purification, convex equality exchange, non-convex arrangements,
model choices, component ownership and shared scopes, freshness checks, exact
and complete-arrangement fallbacks, and typed model assembly. Source and test
links use the unchanged `e6d9fcb06f476375aded43a13c22a6d6b4939144` pin. Solver
sources are identical to that revision; newer documentation-tool and UF-fuzzer
tests do not change the combination code reviewed here.

### Primary sections read

| Reading copy | Sections used in this pass |
| --- | --- |
| Nelson and Oppen, 1978 Stanford report STAN-CS-78-652; retrieved from the report mirror linked in the chapter | §§2.2–2.4: purification, equality exchange, and convex/non-convex search. The Stanford download endpoint refused the connection; the mirror supplied the authors' original report. This was not a reading of the 1979 journal layout. |
| `external/papers/model-guided-no.pdf` | §§2.1, 4, 5.1–5.2 and Appendix A: arrangements, model decisions, opportunistic equality rules, freedom intervals, and the broader transition system. |
| Bruttomesso et al., LPAR 2006 author manuscript `lpar06_dtc.pdf`, linked in the chapter | §§2–3 and Figure 4: SAT-mediated interface search and the extra hypotheses of the paper's NO-emulation strategy. No branching-bound claim is transferred to Primo. |
| `external/papers/griggio.pdf` | §1.5.1, printed pp. 28–35: purification, shared variables, stable infiniteness, convex closure, and delayed combination. |
| `external/papers/king_tim.pdf` | §§1.5.5–1.5.6, printed pp. 26–28: theory-engine responsibility, partial/full checks, and shared-term explanations. |
| `external/papers/Bromberger_Decision_Procedures_for_Linear_Arithmetic.pdf` | §5.4: equality-basis normalization and justifications. The full basis algorithm remains an open lead; this pass does not re-audit all of Chapter 5. |

The original reports were read as papers, not as solver implementation
sources. The current chapter uses Primo's own code for representation and
control-flow claims. Its clause examples were checked algebraically against
the materializer and explanation conventions; they are reasoning traces,
not newly measured execution traces.

### Corrections and evidence boundary

The old dedicated Real page still described a private SAT-assignment snapshot
and a separate assignment trail in `TheoryCombinationManager`. It now describes
the shared `TrailView` and the distinct undo log for arrangement values. The
draft also distinguishes the production `ComponentModelCoordinator` from the
older experimental `ModelBasedCombinationCoordinator`, and distinguishes
protocol-level mutation support from the production adapters, which disable it.

Existing tests were inspected for guarded equality exchange, congruence effects
of model merges, non-convex choices, independent partition-oracle coverage,
projection freshness, mutation side effects, rollback, arrangement fallback,
and reconstruction of purified arguments. No solver batch, fuzz gate, model
validation, or performance evaluation was run for this documentation change.
The qualification and migration records remain in place with their original
revisions, results, and limits. LIA's internal search and the full model chapter
remain separate writing tasks; reader acceptance is still pending.

### Verification

`make docs-check-links` checks both strict books, generated local links and
anchors, and pinned source files and line ranges. The chapter adds 51 source
links. The compatibility check also preserves the old book's 83 baseline pages
and 1,223 captured anchors, plus every pre-existing draft anchor. Navigation,
review-ledger coverage, and identical rendering of the shared literature notes
were checked separately. `git diff --check` passed.

`make docs-check-external` completed both audits, returning failure for 14
external URLs: 11 in the existing book and three in the draft. Ten returned
HTTP 403, two returned HTTP 429, one failed TLS verification, and one refused
the connection. The newly cited Nelson–Oppen report mirror and DTC manuscript
both passed. The rate-limited draft URL was the existing CaDiCaL API-header
reference; the two thesis repository landing pages again returned HTTP 403.
Their local PDFs supplied the reading copies. Reports are in each book's
`_build/linkcheck/output.json`; no URL was excluded to clear this run.

The document-critic pass found no remaining blocking documentation issue after
correcting the assignment-ownership description and checking the source ranges.
The external access failures, final historical extraction, and reader acceptance
remain explicit limitations. No C++ changed, so solver tests, ASan, fuzzing,
and clang-tidy were not rerun.

### Care-pair explanation, 2026-09-27

Reader review found that the chapter named care pairs without explaining why
a pair matters. The new subsection defines a pair and its graph, works through
`f(x)`, `f(y)`, and `g(z)`, and maps the example to the current flattened EUF
generator and the general component driver's choice ordering. It explains why
an equality question supplies no entailment and why agreement on care pairs
does not replace complete model-partition comparison.

The author PDF of Jovanović and Barrett's *Being Careful about Theory
Combination* was read in §3 (Definitions 6–8) and §§4.1–4.2. The chapter now
cites it directly together with the earlier *Sharing is Caring* paper. The
paper's empty-care-graph guarantee and EUF exclusions are distinguished from
Primo's simpler pair generator and heuristic use of its output. Existing EUF
generation and component selection/validation tests are linked; their source
was inspected, without rerunning solver batches.

The solver source pin and reader-acceptance state remain unchanged. The other
review findings about the architecture introduction, default equality rules,
and option conditions remain pending.

`make docs-check-links` passed both strict books and checked 114 pages,
17,409 local links, and 405 unique Git links with zero failures. The new section
and its source/paper links were inspected in the generated HTML, and the review
CSV was checked for unique page entries. Both newly cited paper PDFs returned
HTTP 200. The full external audit was not repeated; its earlier access failures
remain recorded above. `git diff --check` passed.

(care-combination-leads)=
### Two care-related open leads, 2026-09-27

Recorded [more selective and cheaper care-pair generation](leads/care-pair-generation.md)
and [reducing combination obligations with care graphs](leads/care-guided-combination.md)
as separate open, unassigned investigations. Both appear in the draft navigation,
landing page, combination chapter, review ledger, and support inventory.

The first preserves current acceptance checks and records the existing Real
cache as implemented. The second requires a component contract, a correctness
argument for the admitted combinations, progress under changing state, and
construction of compatible models. Neither is described as active
implementation or qualified performance work.

The paper reading now includes §3.1 of Jovanović and Barrett's 2013 journal
article: the growing care-graph fixed point, Theorem 1, and its model-extension
argument. This supplements Definitions 6–8 and the EUF construction read for
the chapter. The cited theorem covers two theories; adaptation to Primo's
component collections remains an explicit design obligation. Source links
retain the existing solver pin. No solver implementation or batch execution
was part of recording these leads.

`make docs-check-links` passed both strict books: 116 pages, 17,611 local
links, 409 unique Git links, and zero failures. Navigation, review-ledger
coverage, and support-inventory coverage agree on all 24 draft pages. Both new
HTML pages contain the open status, pinned source links, paper citations, and
correctness-gate links. `git diff --check` passed. External URLs are unchanged
from the preceding care-pair update; no full external audit was rerun.

(general-combination-status)=
### General combination is implemented, 2026-09-27

The chapter now leads with `solve_component_recipe` as the implemented general
driver. Its route table identifies QF_UFLIA and quantified UF as current users
and QF_UFLRA as the retained dedicated route. `CombinationRecipe` supplies the
component collection and priorities; existing three/four-component tests exercise
the actual driver. The quantified recipe reuses the lifecycle and SAT loop for
instantiation lemmas; it is not an additional arithmetic-combination claim.

The body heading now names the general driver while preserving the previous
`the-component-collection-route` anchor. The architecture overview, DPLL(T)
introduction, landing page, and restructuring record use the same distinction.
The defaults table separates QF_UFLIA model guidance from the incremental-EUF
requirement for online execution. The older experimental Real-only coordinator
and its flag are distinguished from the production general driver.

The architectural follow-up is migration of QF_UFLRA onto that existing driver,
preserving its current behavior and optimizations. The care leads concern
choice generation and acceptance obligations. Full `DPLL(T^M)` integration
remains separate algorithmic work. These edits resolve the earlier review's
architecture and option-condition findings; the default equality-rule
explanation and reader acceptance remain pending.

The recipe definitions, both production callers, coordinator interface, and
three/four-component test bodies were inspected. Solver sources and their
documentation pin are unchanged. No solver batches or new qualification were
part of this documentation clarification.

`make docs-check-links` passed both strict books: 116 pages, 17,614 local
links, 412 unique Git links, and zero failures. Generated HTML includes the
new general-driver heading, the previous collection-route anchor, and the
production caller links. All prior anchors on the affected draft pages were
preserved. `git diff --check` passed, and comparison with the source pin found
no changes under `src/`. External URLs are unchanged; the earlier external
audit's access failures remain recorded separately.

(default-lra-equality-rule-review)=
### Default LRA equality rules, 2026-09-27

Expanded the combination chapter's default equality producer into worked
examples for `FixedEq`, `EqRow`, `EqOffsetRows`, and `EqRows`. The explanation
defines fixedness using matching bounds, identifies which shared variables
must be basic, and shows how bound premises guard a propagated equality.
It describes Primo's grouping and representative selection, distinguishes
its scan from the paper's scheduling, and uses the existing fixed-slack
regression to illustrate a basis-dependent missed equality. Model agreement
and exact fallback remain separate acceptance mechanisms.

Re-read de Moura and Bjørner, *Model-based Theory Combination*, §5.1 from the
local primary PDF and checked the examples against `entailed_equalities`,
its result type, the combination caller, and the existing rule and
fixed-elimination regressions. The LRA chapter now links to this explanation;
the combination test map links to the positive and negative rule cases.
This resolves the default equality-rule explanation item in the earlier
review. Reader acceptance and the remaining chapter rewrites are still open.
Solver sources and their documentation pin are unchanged. No solver batches
or new solver qualification were part of this documentation edit.

`make docs-check-links` passed both strict books: 116 pages, 17,625 local
links, 415 unique Git links, and zero failures. The generated HTML contains
the rule examples, new section targets, and pinned implementation/test links;
the LRA crosslink resolves and all prior anchors on the affected pages remain.
The review CSV has unique page entries, and `git diff --check` passed.
External URLs are unchanged; the earlier external audit's access failures
remain recorded separately.

(model-mutation-scheduling-review)=
### Model mutation scheduling, 2026-09-27

The combination chapter now states when the dedicated Real route attempts
scratch LRA diversification: after the outer SAT candidate passes component
checks, before the final explained-equality passes and EUF reconciliation.
It records the enable condition, one call per eligible round, repeated calls
after a SAT recheck, and the mutation-disabled projection after targeted
disequality repair. The bounded sweep, early exits, scratch ownership, and
acceptance conditions are distinguished from the later EUF model adaptation
and from the general driver's mutation protocol.

Re-read de Moura and Bjørner, *Model-based Theory Combination*, §§4 and 5.2.
The primary text explicitly leaves `M-Mutate` scheduling unspecified; the
chapter distinguishes that freedom from Primo's concrete call order. The
default order can spend mutation work on a candidate discarded by subsequent
equality propagation. No alternative schedule or performance improvement is
claimed here.

Inspected the outer solve loop, both partition-construction calls, the
diversifier, option defaults and CLI flags, and existing regressions for a
single mutation call, pair pruning, untouched tableau state, and mandatory
disequalities. The LRA chapter links to the schedule, and the review ledger
and combination test map include these checks. Solver sources and their pin
are unchanged. No solver tests, fuzzing, or benchmarks were run for this
documentation-only clarification.

`make docs-check-links` passed both strict books: 116 pages, 17,633 local
links, 428 unique Git links, and zero failures. The generated scheduling
section, LRA crosslink, source/test links, and explanatory text were checked;
all prior anchors on the affected pages are preserved. The review CSV has
unique page entries, and `git diff --check` passed. External URLs are
unchanged; the earlier full external audit's access failures remain recorded
separately.

(combination-lead-separation)=
### Separate combination proposals from implementation, 2026-09-28

The combination chapter now ends with an evidence section and brief links to
separate open leads. The previous `evidence-and-open-leads` anchor is retained.
The implemented-route summary describes the current split between the general
driver and the dedicated Real engine; the proposal details live elsewhere.

Added open, unassigned pages for a complete implied-equality basis, migration
of QF_UFLRA to the existing general driver, and further model-based transition
integration. The two existing care leads remain separate. All five are linked
from the chapter and the landing page's Open leads section. The three new
pages appear in navigation, the review ledger, and the supporting-file map;
the architecture overview links to the migration lead.

The extraction preserves the original equality-basis investigation and the
dated component/transition records as references. Bromberger §5.4 and de
Moura–Bjørner §4 and Appendix A supply the paper context; the current driver
and route boundaries remain those checked in the preceding chapter reviews.
This is documentation organization, not a new implementation plan or a claim
of fresh solver qualification. The new lead pages await reader review and
further design work. Solver source and its documentation pin are unchanged.

`make docs-check-links` passed both strict books and the local/source-link
check. The previous HTML anchors were preserved throughout the draft, and
the navigation, review ledger, and support inventory cover all 27 draft
pages. The three new pages render with open/unassigned status and source
links; all five leads are reachable from the chapter and landing page.
`git diff --check` passed. External URLs are unchanged; the earlier external
audit's access failures remain recorded separately. No solver batches were
part of this cleanup.

(quantifiers-chapter)=
### Quantifiers chapter, 2026-09-28

Added a chapter covering the current quantified-UF route, indexed binders,
capture-safe substitution, all four quantifier/polarity cases, term-pool
growth, tuple orders, redundancy caches, queue scheduling, and budgets.
The answer table distinguishes refutation, Skolem-covered existential SAT,
unresolved universal obligations, and unsupported model requests. A worked
refutation and nested-binder examples accompany links to the owning source
and existing regressions. The general-driver, DPLL(T), core, and landing
pages now link to this chapter.

Inspected `quantified_solver`, `QuantifierComponent`, binder/frontend
operations, tuple generators, instance simplification, the component driver
and dispatcher, option defaults, and the pool export/checker. Checked the
worked clauses and index substitutions against those operations and their
existing tests. The chapter describes the current `sum` default and keeps
earlier default descriptions in the dated development records. Pools and
caches survive backtracking but belong to one solve; generated terms remain
in the session's term manager.

Read these primary sources for the chapter:

- SMT-LIB 2.6 revision 2021-05-12, §3.6: quantifier declaration lists, scope,
  and parallel `let` bindings.
- Charguéraud, *The Locally Nameless Representation*, §§2.3 and 3.3:
  de Bruijn indices and local closure.
- Reynolds, Barbosa and Fontaine, TACAS 2018, §§2–4.1, particularly
  Theorem 1 and the entailment/completion distinction.
- Janota et al., FMCAD 2021, §§III–IV: enumeration orders and redundancy.
- Reynolds's 2013 thesis, §§3.1.1–3.1.2: instantiation and model acceptance.
- Barbosa's 2017 thesis, §5.1 and §§7.1.1–7.1.2: shared terms,
  capture-safe transformations, and contextual Skolemization.

The source table distinguishes implemented ideas from comparison points;
the thesis algorithms are not claimed as fully implemented. The primary
PDFs were read from their linked author/standard URLs. The retained local
paper checklists and original binder note remain traceability records.
The new open-lead page carries forward entailment filtering and fail masks,
alongside bounded work before the first useful lemma and cancellation.
No investigation is presented as active implementation.

Historical correctness and performance records remain attached to their
original revisions. Pool-guided cvc5 confirmations are distinguished from
inconclusive runs and from proof certificates. Solver sources and the
documentation source pin remain unchanged. No solver tests, fuzzing, or
benchmarks were run for this documentation-only chapter. Reader acceptance,
final historical extraction, and the LIA/model/preprocessing/frontend
chapters remain pending.

`make docs-check-links` passed both strict books: 121 HTML pages, 18,205
local links, 471 unique Git links, and zero failures. All prior anchors
across the 31 pre-existing draft HTML files were preserved. Navigation,
review entries, and the supporting-file inventory cover all 29 draft pages.
The generated chapter sections, source/test/paper links, and separate
open-lead status were inspected. `git diff --check` passed, and comparison
with the documentation pin found no changes to solver source or the cited
unit suites. Linked primary PDFs were retrieved successfully for reading;
the full external audit was not rerun, and its earlier access failures
remain recorded separately.

(incremental-draft-builds)=
### Incremental draft builds, 2026-09-28

`make docs-next` and its preview target now reuse Sphinx's cached documents.
`make docs-next-full`, both link-check targets, and the documentation
qualification script retain full builds with warnings treated as errors.
`make docs` retains its existing full-build behavior.

Checked actual HTML modification times: an unchanged draft build rewrote no
book pages; a temporary edit to the quantifier chapter rebuilt that page and
the index, leaving the other 27 pages untouched. The edit was then restored
and rebuilt. Full-build and link checks, shell syntax checks, `make lint`, and
`git diff --check` passed. This changes documentation tooling only; C++ tests,
sanitizers, static analysis, and solver fuzzing are not applicable.

(documentation-coverage-todos)=
### Documentation coverage TODOs, 2026-09-28

The comparison with the older book identified missing explanations alongside
chapters still awaiting their first draft. Visible TODO callouts now identify
the required arguments, worked steps, design rationale, operational references,
and evidence extraction at the relevant locations. Whole missing chapters are
recorded at their existing entry points; no empty chapter placeholders were
added. The review ledger retains these as unresolved coverage work, separate
from earlier factual checks. The writing conventions now require an explanatory
home and a section-level content audit before replacement acceptance.

These edits record work to do; they do not complete the explanations or change
solver behavior. Existing source and historical evidence remain in place.

(euf-filtering-coverage-checks)=
### EUF filtering and Boolean-domain coverage, 2026-09-28

Replaced the filtering/domain TODO with the monotonicity argument, a complete
Boolean-argument counterexample, the protection flag's contract, and a coverage
argument for ground frontend-prepared input. Read Sebastiani's §6.10 and
Proposition 2.3 and SMT-LIB 2.6 Definition 8. Traced frontend naming, polarity
collection, registration, assignment filtering, signed reasons, propagation
exclusions, and model completion in the source baseline. Existing regressions
are linked as obligations, not reported as newly run.

The older coverage claim was too strong under filtering: a predicate-result
class can remain unanchored in the search closure. The new text distinguishes
protected Boolean arguments from those safely completable results and explains
why completion cannot change an application signature. It also makes explicit
that predicate applications used as ground arguments receive frontend names.
The model-construction TODO remains for the next chapter.

Both strict books and local/source links were checked with `make docs-check-links`;
existing HTML anchors were preserved. No solver behavior changed, no solver
batches ran, and no historical qualification was promoted. Reader acceptance
remains pending.

(model-construction-chapter-checks)=
### Model-construction chapter checks, 2026-09-28

Resolved the second EUF TODO with a separate model chapter. It describes the
typed owning representation, both construction adapters, Boolean completion,
authoritative component values, function defaults and evaluated tuple keys,
component-local domain labels, reverse conversion replay, and the boundary
between construction, freezing, checking and printing. The worked example
restores eliminated constants through a fixed function table, with Boolean
completion and arithmetic values explained explicitly.

The source pass covered `core::Model` and its evaluator, both builders and
their callers, the typed conversion log, solved-constant cycle rejection,
fragment validation and assembly, session finalization, and serialization.
The reader pass checked stage order, simultaneous updates, the reason for
fresh evaluation epochs, and the distinction between a cyclic EUF equation
and a self-dependent substitution. The older contract's references to
invalidation are explained by the implemented epoch mechanism; no selective
dependency worklist is claimed. Existing unit regressions are mapped to their
obligations, and earlier qualification stays attached to its original revisions.

Navigation, chapter entry points, the review ledger, and migration inventories
include the new chapter. Both EUF TODOs are now resolved. Reader acceptance,
the broader section-level coverage audit, user guides, and other chapters'
TODOs remain open. Solver sources and the source-link revision are unchanged.
No solver test, fuzzing, or benchmark batch was run for these documentation edits.

`make docs-check-links` passed both full strict builds: 122 HTML pages,
18,380 local links, 522 unique Git links, and zero failures. The 33 existing
draft HTML files retained all their anchors; navigation, ledger, and support
inventory cover all 30 draft pages. Rendered Boolean-domain coverage and
model-replay tables, code examples, and chapter navigation were inspected in
Chromium. `git diff --check` passed. Source and unit-test files still match
the documentation pin. The full external URL audit was not rerun; the cited
primary filtering PDF was retrieved and read separately.

(quantifier-negation-checks)=
### Quantifier negation checks, 2026-09-28

Clarified that `Q` in the polarity table denotes the quantified formula without
its surrounding negation. The clausifier represents `not Q` by the opposite
SAT literal; the component classifies the quantifier kind together with its
assigned truth value. The chapter now derives both negated cases, explains
why an occurrence under negation in a larger Boolean formula need not force
that value, and follows negation through an alternating nested example.

Read the negation constructor, clausifier, quantifier classification and lemma
construction, and existing polarity regressions. Guards and retained witness
records were checked against the backtracking explanation. This is a local
exposition correction; the remaining quantifier TODOs and reader acceptance
are unchanged. Both strict books and local/source links pass
`make docs-check-links`; rendered sections and existing chapter anchors were
checked. No solver behavior changed or solver batches ran.

(quantifier-specialization-and-relevance-checks)=
### Quantifier specialization and relevance leads, 2026-09-28

Recorded three separate investigations: optional NNF through binders, fixed
obligations from directly asserted quantifiers, and omission of quantified
don't-cares. The implementation chapter now states that the current component
processes all assigned quantifiers, including irrelevant ones, and explains
the possible effect on `unknown`. No optimization is claimed as implemented.

Read de Moura–Bjørner's relevance report §§1–3; Sebastiani's 2007 survey §6.9
and §§7.2.1–7.2.2 alongside §6.10; Niemetz–Preiner–Biere's FMCAD 2014 paper
§§II–III and §IV.A–B; and Sebastiani's 2020 partial-assignment analysis §§3–4.
The shared literature note distinguishes online watches, structural candidate
reduction, dual extraction, and entailment versus three-valued evaluation.
Masina–Spallitta–Sebastiani §4, §5.5 and Appendix C provide limited context
for the NNF proposal, with enumeration results separated from decision solving.

Checked the current preprocessing and instance-simplification boundaries, root
clausification, kind/value classification, witness guards, full-model decision,
and the complete-candidate scheduling regression. The proposed relevance
contract requires support of all active roots and required nested obligations;
it does not infer quantified satisfiability from exhausted enumeration.
The older arithmetic relevance lead retains its independent evaluation sequence
and now links the paper survey. Navigation and migration inventories include
the new pages. All three leads remain unassigned and require design, proof,
implementation and evaluation before acceptance as solver mechanisms.

`make docs-check-links` passed both full strict builds and checked 127 HTML
pages, 19,248 local links and 534 unique Git links with zero failures. Existing
anchors in 126 prior HTML files were preserved. The ledger and support inventory
cover all 34 navigated draft pages; rendered titles, tables and links were
inspected. Source and unit-test files still match the source-link pin.
The external URL audit was not rerun; the cited primary texts were accessed
and read for this survey. No solver batches ran or new qualification was claimed.

(row-propagation-exposition-checks)=
### Row propagation and combination links, 2026-09-28

Resolved the LRA row-propagation TODO with a signed-coefficient table, a
forward bound and its SAT clause, a strict-bound variant, and a reverse
derivation that divides by a negative coefficient. The description follows
the implied-bound helpers through atom entailment, queued delivery and reason
streaming. It records selective triggers, missing-bound behavior, index gates,
size/fanout caps and defaults, without claiming complete interval saturation.

Added reciprocal links to theory combination. A separate combination subsection
explains the optional online paired-bound equality producer, lazy equality-atom
registration and an arithmetic-to-EUF conflict chain. It distinguishes this
producer from final tableau equality scans, exact fallback and model guesses.
Existing forward, reverse, cap and online-equality regressions are linked;
the review ledger and migration inventory reflect the resolved coverage gap.

Reread Dutertre–de Moura's technical report §3.2.5 and the strict-bound
convention in §3.3, and Bjørner–Nachmanson §3.3. Checked the owning code,
callers and option defaults, then reviewed the example arithmetic, clause
signs and rendered sections. Both strict books and local/source links were
checked with `make docs-check-links`; existing page anchors were retained.
Solver sources and the documentation pin remain unchanged. No solver tests,
fuzzing or benchmark batches ran for this documentation-only change.
The SOI TODO was resolved in the following pass. The integer-arithmetic and
retained-evidence TODOs remain open.

The follow-up review clarified three boundaries: propagation installs the
existing atom's threshold (8 in the worked example), online LRA propagation
is disabled by model-equality branching, and an already-false interface
equality remains a mandatory disequality for later combination checks.
The relevant assertion and construction paths were rechecked, with both
strict books and local/source links verified after these edits.

(soi-exposition-checks)=
### SOI objective, selection and conflicts, 2026-09-28

Expanded the optional SOI algorithm from King–Barrett–Dutertre, FMCAD 2013,
§V, and King's thesis §§3.2–3.3, including §3.3.5 and Figure 3.6. Checked
the objective/direction updates, breakpoint ordering, entering and leaving
choices, restricted-row conflict test, minimizers, finite fallback, and undo
records against their owning source and existing regression tests.

The chapter now derives the objective, works through Figure 6, explains a
conflict requiring two rows, and distinguishes implemented selection and
minimization choices from the primary algorithms. Historical evidence is
identified by run-log ID, revision and qualification limit. Older claims of
implementing the full paper algorithm were qualified. Both books now enable
MyST dollar-delimited mathematics, with the authoring convention documented.

A single bounded component debug run exposed a zero-breakpoint discrepancy:
the current walk applies a slope change when a variable starts on a bound and
moves into its feasible interval. The retained download and run-log entry
`2026-09-28-soi-zero-breakpoint-debug` reproduce two pivots where the objective
admits a one-pivot repair. The example reaches a valid assignment. Endpoint
correction, dedicated regression coverage and subsequent remote qualification
remain separate implementation work; no solver behavior was changed here.

Both strict books and local/source links passed `make docs-check-links`.
All anchors in the 131 prior HTML files were preserved, solver source and unit
tests still agree with the source pin, and the downloadable debug example
passes `clang-format --dry-run --Werror`. Browser review covered the objective,
worked pivot/table and conflict derivation with MathJax rendering; a long
display was split into three lines. The full external-link audit was not rerun.
No solver test suite, fuzz campaign or benchmark batch ran for this change.
