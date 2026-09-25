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
5. [Boolean abstraction and SAT integration](implementation/sat.md): can a
   reader derive the worked clauses, distinguish definitions from assertions
   and assumptions, and find the lifetime and explanation obligations?
6. [Library API](leads/library-api.md) and
   [incremental memory](leads/incremental-memory.md), together with
   [compact term storage](leads/term-storage.md): do the work items capture
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
model snapshot. Kernel checkpoints and tentative model-equality undo remain
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
220 direct Dolmen passes and 20 additional passes with wider arithmetic logic
on the same saved assertions/models. Those 20 remain incomplete under strict
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
