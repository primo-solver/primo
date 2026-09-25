# Documentation restructuring before public release

**Status: rewrite in progress, 2026-09-24; first user-manual batch implemented,
full structural inventory audited.**
The project owner has made substantial documentation restructuring a priority
before the first public release. A private repository transfer can happen first.
The three status entry points added in
`559dcb5` establish the distinction between done, active, and open work; they do
not by themselves reorganize the underlying material. Codex is executing the
current rewrite; independent review ownership remains unassigned.

The [2026-09-24 inventory audit](work/documentation-audit.md) records per-page
dispositions, supporting files, concrete stale claims, captured compatibility
targets, and the proposed final contents. It is a structural review, not full
code/paper/evidence sign-off. Start there for the current extraction priorities.

**Execution update, 2026-09-24:** the user chose a separate draft book in
`docs-next/` for page-by-page review. `make docs-next` and
`make docs-next-serve` build and preview it independently. The current `docs/`
book remains available until replacement acceptance. The first draft set is
the home page, contributor correctness guide, architecture overview, and review
ledger. Local verification and user acceptance are separate states; none of
these pages is accepted yet. The destination paths in the inventory remain
relative to the final `docs/` tree, with `docs-next/` used during preparation.

The core draft in `docs-next/implementation/core.md` covers
representation and ownership with function/test links and a compiled example.
Library embedding and incremental memory are separate open-work pages. Their
investigations remain pending; documenting the current core does not choose a
new public API or reclamation design.

**Theory chapters, 2026-09-25:** DPLL(T), EUF and LRA drafts are now available
for reader review. Supplied PDFs in `external/papers/` resolved the LRA source
access blocker. The rewrite preserves the arithmetic implementation and its
development evidence; an explicit inventory in the LRA chapter links detailed
engineering, experiments and rejected approaches. King, Griggio, Bromberger,
Monniaux and the 2024 arithmetic paper are mapped to their implemented subsets.
`docs-next/review.md` records section-level reading and remaining extraction.
The shared [](papers/primo-qflra-results.md) note links the published evaluation
separately; its exact revision/artifact mapping remains open work.

The compact-term-storage lead separately records union/variant layout probes,
duplicated interning data, and future leaf-payload constraints. The current
representation remains in use; implementation and solver measurements are
deferred.

The architecture draft now includes a code-grounded query-flow map and an
explanatory SAT/theory loop adapted from the supplied TikZ sketch. Editable
sources and HTML-ready SVG exports live in `docs-next/`; figure regeneration
and review are recorded in its review guide. Online callbacks and limited
quantified completion are stated explicitly in the surrounding text.

`docs-next/implementation/sat.md` is now the Boolean abstraction and SAT
integration draft and the implementation-template pilot. It covers the
clausifier/adapter boundary with a runnable clause/guard example and a
paper-to-code checklist. Five focused regression suites and both book builds
pass; reader review and pilot acceptance remain pending. The separate planned
clausification chapter can later expand preprocessing/encoding policy; this
draft already owns the basic encoding and interface contract.

## Outcome and publication gate

An outside reader must be able to build and use Primo, understand its supported
fragment and limitations, find the implemented design, and contribute without
reconstructing a sequence of branch experiments. Active work and open leads
must be clearly distinct from the current manual. Historical evidence must
remain accessible and attributable to the revision it measured.

Merge the code intended for the release, then complete and review the
restructuring against that revision before making the repository public. The
repository can move privately into the destination organization first. Rights
and infrastructure planning can proceed in parallel, but a passing documentation
build alone does not satisfy this gate.
The results-storage implementation is a separate workstream: this rewrite can
link existing run IDs while the new storage tools are still pending.

## Source links and documented revisions

Use GitHub links pinned to a commit for code explanations, optionally including
line anchors. A moving `main` link is suitable for browsing current development,
but cannot identify the implementation described by a released book. Configure
the repository URL and source revision centrally in the documentation build
and generate code links from those settings. Keep revision metadata in the
review ledger or build information; ordinary chapters explain the code without
revision boilerplate. The replacement book uses one commit-pinned URL for its
`source` role. Automatic path/line validation, build-revision metadata and
archive-aware provenance remain planned tooling.

Check the source revision and referenced paths in the destination before
publishing. If a curated history changes commit identities, use the explicit
provenance mapping and available public revisions rather than linking to
unavailable private commits. Keep historical evidence attached to the code
actually measured. See [GitHub's permanent-link documentation](https://docs.github.com/en/repositories/working-with-files/using-files/getting-permanent-links-to-files).

## Inventory findings

The historical baseline at `70e7aae` has 75 tracked Markdown pages under `docs/`.
This is an audit of navigation, headings, introductory status, and selected content,
not a completed code-to-documentation correctness review. The findings below
describe that earlier state; the later [audit](work/documentation-audit.md)
distinguishes completed manual work from still-open corrections.

| Finding | Concrete example | Required treatment |
| --- | --- | --- |
| The landing page leads with engineering status, without a user guide | [](index.md) and `README.md` | Add direct paths for installation, first solve, models, sessions, and supported input. |
| Entry points disagree about shipped capabilities | `README.md` says quantifiers are unimplemented and describes sessions as feature-branch work; [](current-status.md) records their merges | Reconcile against the chosen source revision and make the README a short entry point. |
| Reference and development chronology share large pages | [](arithmetic.md): 2,087 lines; [](preprocessing.md): 1,701; [](qf-lia-branch-and-bound.md): 1,401 | Extract current contracts and algorithm explanations; retain dated investigations separately. |
| A reference title undersells current behavior and retains old proposals | [](quantifiers.md): 1,119 lines, titled parsing/representation, with implemented UF solving and a later pending-proposal heading | Write a current quantified-UF chapter; separate representation decisions and qualification history. |
| Operational instructions mix reference, procedure, and experiments | [](debugging.md): 1,420 lines; [](experiments.md): 884 | Separate statistics reference, debugging procedures, experiment workflow, and dated reports. |
| Leads are still a large historical ledger | [](open-leads.md): 2,752 lines, including closed investigations | Keep a short open-work index and move detailed investigations into dated notes without breaking lead labels. |
| Unimplemented designs sit among implemented components | [](difference_logic.md) under Theories; [](quantifier-entailment-design.md) under Core architecture | Route them through open leads unless an owner confirms active implementation. |
| Historical narratives can sound like current incidents | [](nelson-oppen-qf-lra-regression.md) opens with a regression on main before describing its repair | Lead with dated disposition and link the current implementation. |

These counts describe the baseline, not length limits for the new book. Split
by reader task and ownership of facts, not by an arbitrary number of lines.

## Proposed reader structure

Keep three main entrances, as agreed, and make the completed-work entrance a
usable manual rather than a chronological development log:

| Entrance | Contents | Required boundary |
| --- | --- | --- |
| Done: using and understanding Primo | Getting started; supported SMT-LIB and modes; CLI/options/models/statistics; contributor workflow; architecture and implemented theories | Describe current behavior at the documented source revision, including opt-in features and limitations. |
| Being worked on | Short active-work register linking scoped design/implementation/review tasks | Owner or explicitly unassigned, date, branch/PR, completed subset, next step, blockers, and remaining acceptance criteria. |
| Open leads | Short candidate-work register linking designs and investigations | Motivation, evidence, prerequisites, next investigation, and falsifier; no implied active implementation. |

Paper notes and the historical evidence archive are supporting libraries linked
from all three entrances. A paper summary is not itself an implemented feature.
A completed experiment can belong in the archive even if its implementation
was rejected. Readers should reach history when they want evidence or rationale,
without reading it first to discover how the current solver works.

## Content moves and rewrites

The reviewed working destinations are now in the page map and proposed contents
linked below. The table specifies
responsibility and extraction work, not a claim that a rename alone is sufficient.

| Existing material | Intended treatment |
| --- | --- |
| `README.md`, [](index.md), [](current-status.md) | Short onboarding and capability map; one authoritative supported-fragment/mode table; links to dated qualification. |
| [](architecture.md), [](model-interpretation.md) | Current layer/ownership/term/model contracts; separate command-line material and development chronology. |
| [](incremental-smt.md), [](assumption-incremental-lra.md), [](assumption-incremental-uf.md), [](lexical-conformance.md) | User session guide and frontend reference; implementation contracts under architecture; dated gates/audits in evidence. |
| [](tactics.md), [](tactic-reference.md), [](preprocessing.md), [](structural-auto-preprocessing.md), [](boolean-dag-flattening.md) | Current preprocessing/tactic reference with explicit default versus opt-in behavior; experiments and rejected alternatives separated. |
| [](sat-ipasir-up.md), [](dpllt.md), [](euf.md), [](euf-boolean-arguments.md) | Current SAT/theory interface and EUF chapters, preserving invariants, explanation obligations, and paper scope. |
| [](arithmetic.md), [](qf-lia-branch-and-bound.md), [](qf-lra-equality-sharing.md) | Current Real/Int arithmetic and equality-sharing references; historical rollout, tuning, and measurement notes extracted. |
| [](nelson-oppen.md), [](modular-theory-combination.md), [](n-way-combination-migration.md) | Current combination architecture and separate convex/integer routes; migration history and qualification retained as dated evidence. |
| [](quantifiers.md) | Current quantified-UF user boundary, representation, operations, and algorithm chapter; binder alternatives and experiment progression archived. |
| [](debugging.md), [](experiments.md), [](tuning-parameters.md), [](run-log.md), [](finite-domain-fuzzing.md) | Task-oriented contributor procedures plus CLI/statistics/parameter reference; experiment records retain exact provenance. |
| [](open-leads.md), [](qf-lra-heuristics.md), [](qfuf-disequality-propagation.md), [](quantifier-entailment-design.md), [](difference_logic.md) | Extract current accepted mechanisms where present, then separate genuinely open proposals, confirmed active work, and closed investigations. |
| Profiles, comparisons, bug reports, performance plans, fuzz-coverage reports, and merge checklists | Review each disposition; keep dated evidence under an archive with revision, outcome, limitations, and links to any current mechanism. A filename containing "plan" is not proof of active work. |
| [](literature.md), `docs/papers/`, [](reference/llm2smt-retrospective.md) | Separate reading/index guidance, primary-source notes, implementation mappings, and predecessor retrospective; retain clean-room boundaries. |
| [](contributing.md), [](public-repository-migration.md), [](benchmark-record-storage.md), this plan | Current contributor requirements plus clearly marked migration/design work; agreement on a design must remain distinct from implementation. |

## Page map and target layout

The {download}`page map <documentation-page-map.csv>` accounts for all **76
tracked Markdown pages under `docs/` at `4921f07`**, including paper notes and
the predecessor retrospective. Each row proposes a milestone, audience,
disposition, and destination. New pages and completed batch reviews are now
included too. The 2026-09-24 pass gives all **83 baseline book pages** a reviewed
structural disposition and adds this audit's report as the 84th page.
Full factual reviews and per-anchor destination routing remain pending;
file coverage is not a correctness audit. The
{download}`compatibility snapshot <documentation-compatibility.json>` captures
old paths, rendered targets and explicit labels, while the
{download}`supporting-file map <documentation-support-map.csv>` covers root
documents, skills, figures, fixtures and workflows. Destinations are relative
to `docs/` and no longer contain unresolved path placeholders.
The {download}`proposed final contents <documentation-target-toc.yml>` is a
review artifact, not the active book configuration. A proposed archive destination
does not mean all facts in that page are obsolete: extract any current contract
first. Reconcile the map with newly added or removed pages at every milestone.

Use these paths as the working layout. Create pages as their content is ready;
do not fill the published navigation with empty placeholders.

```text
index.md                       Three entrances and first-solve link
current-status.md              Done: capability and qualification map
guide/                        Getting started, input, models, sessions
reference/                    Options, tactics, statistics; retrospective retained
implementation/               Architecture, core, numerics, frontend, sessions, models,
                              preprocessing, tactics, clausification, SAT,
                              DPLL(T), EUF, LRA, LIA, combination, quantifiers
contributing.md                Contributor entrance
development/                  Contributing changes, testing, fuzzing, debugging,
                              validation, experiments, tuning, docs
active-work.md                 Short index of work actually underway
work/                         Scoped active plans and review checklists
open-leads.md                  Short index of open hypotheses
leads/                        One investigation per stable lead ID
archive/                      Dated investigations, decisions, rejected results
literature.md + papers/        Primary-source notes and implementation mappings
run-log.md + run-log.json      Existing evidence interface until storage cutover
```

The done entrance links the guide, reference, implementation and contributor
material. Active work and open leads remain equally visible entrances; the
archive and literature are supporting navigation. Directories organize page
roles, not feature maturity: a component can have an accepted implementation,
an active extension, and several open leads at the same time.

## Canonical facts and page contracts

| Fact or document | Canonical home and required content |
| --- | --- |
| Supported behavior | `current-status.md`: logic/mode/model matrix, defaults versus opt-in, limitations, source revision and qualification links. `guide/input.md` explains syntax and examples without maintaining a second independent support table. |
| Options and commands | `reference/options.md`, checked against CLI registration in `src/main.cpp`, defaults in `src/smt/check_sat.hpp`, enum spellings in `src/cli/option_names.hpp` and the candidate binary's help. The `src/cli/options.*` files are placeholders. Include default, domain, affected modes, failure behavior and a useful example; subsystem pages link here. |
| Implementation contract | Each `implementation/` chapter: concrete types/files, inputs/outputs, representation, ownership/lifetime, invariants, allocating operations, failure/unknown paths, one worked example, limitations and test links. |
| Algorithm basis | Same chapter: primary source and section, implemented operations/subset, deviations, complexity expectations, proof obligations and invariant tests. Link detailed paper notes; do not repeat whole summaries. |
| Active item | `active-work.md` indexes a plan with owner or unassigned, last review date, branch/PR, completed subset, next action, blockers and remaining acceptance criteria. |
| Open lead | Stable ID, hypothesis, motivating evidence/revision, prerequisites, next experiment and falsifier. Accepted pieces link current reference; rejected pieces link a dated decision. |
| Historical result | Dated outcome, measured revisions/configurations, run IDs, commands/artifact links, correctness limits, accepted/rejected/deferred disposition and current-reference link. Preserve original negative results. |

Keep `README.md` short: identity, first build/solve, documentation entrances,
license and contribution link. Put an executable example's SMT-LIB input in one
small fixture and include or link it from prose; record expected output and
mode restrictions. Do not introduce a generated full API manual in this pass.
Source links and selected type explanations are enough for the initial rewrite.

## Source-revision workflow

M0 records the merged source baseline explicitly; it is not automatically the
head of this documentation branch or an older benchmark commit. M1 implements
one source-link helper configured by repository URL and a full source commit.
Authors supply a repository-relative file path and optional line range; the
helper emits the GitHub `blob/<commit>/...` URL. Use file links by default and
line anchors only when they improve the explanation. Validate tracked paths and
line bounds against that commit, and record the inspected revision in review
metadata. Do not repeat it in chapter prose. Store the book/build revision
separately.

Historical reports keep their measured commit links, even when the book's
source revision advances. They must not inherit the current source revision
through the helper's default. Release archives use the recorded source revision
and `SOURCE-REVISION` provenance without requiring a `.git` directory; the
packaging step must include any source-link manifest required for that build.

Source development need not stop during the rewrite. Before closing a batch,
compare its inspected source paths with the intended merged revision. Re-review
affected claims/examples after changes; do not merely replace the commit in
every URL. At final integration, choose one source revision for the current
manual and resolve remaining differences. Benchmark evidence retains its own
revision and is never silently requalified by this process.

## Execution milestones

Each row is a reviewable batch, split into smaller coherent commits where
necessary. Every batch updates the page map and active-work entry, preserves
links for moved content, and passes strict `make docs`. Owners and reviewers
remain unassigned; executing this plan does not assign work to other people.

| Milestone | Deliverable and order | Evidence required to close it |
| --- | --- | --- |
| M0: baseline and inventory review | Confirm intended code is merged; record full source commit; review every page-map disposition, section split and old path/label. Add README, AGENTS, relevant skills, build/config/assets and packaging files to the supporting-file checklist. | Complete map with concrete destinations and compatibility obligations; contradictions recorded with source paths to inspect. No unresolved ownership of canonical facts. |
| M1: navigation and source links | Add the three-entrance navigation and source-link helper, documented revision, page templates and archive convention. Use the SAT adapter chapter as a pilot for the implementation template. | Pilot traces paper → types → tests → evidence; source links validated locally and authenticated destination links checked where available. Light/dark rendered navigation reviewed. |
| M2: usable manual | Rewrite README and capability map; add `guide/getting-started.md`, `guide/input.md`, `guide/models.md`, `guide/sessions.md`, and `reference/options.md`. Explicitly reconcile quantified UF and session claims. | Clean-checkout build and first solve; SAT model, scoped session, supported quantified example and unsupported/unknown boundaries exercised against the selected binary. Help/default comparison recorded. |
| M3a: representations and sessions | Rewrite architecture, core terms/symbols, frontend, model ownership and incremental-session contracts. Extract dated gates into the archive in the same batch. | Read relevant `src/core/`, `src/smtlib/`, CLI and SMT interfaces; review identity, allocation/invalidation, scoping and saved-model invariants against code and tests. |
| M3b: Boolean pipeline | Rewrite tactics, preprocessing, clausification, SAT adapter and DPLL(T); separate default mechanisms from experiments. | Trace an input through the layers; verify option names and defaults, explanations/callback lifetimes, paper scope and test references. |
| M3c: theories | Rewrite EUF, LRA, LIA, combination and quantified UF in separate chapter commits. | Each chapter satisfies the implementation contract; distinguish specialized convex and N-component routes, bounded integer completion and quantifier/model limitations. Resolve current claims from source, not chronology. |
| M4: contribution and reproduction | Rewrite testing, debugging, statistics, validation and experiment procedures. Add a root `CONTRIBUTING.md` pointer and synchronize affected AGENTS/skill links. | A contributor can run the local path without a maintainer's cluster account; optional cluster qualification and separate model validation are clear. Result-record instructions match the storage actually implemented. |
| M5: work registers and evidence | Finish extraction into short active/open indexes, individual leads and dated archive pages; review literature mappings. Move migration/storage plans under `work/`. | Every old page and explicit label accounted for; accepted/rejected/deferred outcomes preserved; run IDs and evidence links unchanged or mapped explicitly. No proposal presented as implementation. |
| M6: integration and release readiness | Reconcile against final merged source; update archive packaging to include editable docs; verify links, examples and reader journeys; close map rows with review evidence. | Strict book build, compatibility-link check, example report, extracted-archive build/docs check and rendered review. Record closing commits and remaining limitations; obtain the outside-reader walkthrough before declaring the public-doc gate closed. |

M0 → M1 → M2 is the initial path. Complete M3a–M3c in subsystem batches,
then M4 and M5, with history extraction happening alongside each rewrite rather
than deferred to the end. M6 requires all previous milestones. Results-storage
tooling and site hosting can proceed separately; neither is needed to start
rewriting. Keep the existing run-log authoritative until its coordinated cutover.
Public hosting remains a migration task and does not require changing book
technology during this rewrite.

For each batch, record inspected source revision/paths, rewritten and archived
pages, example commands/results, strict build outcome, link/visual review and
commit IDs in the work plan. Use `docs/run-log.json` for new solver experiments
when needed, not routine editorial builds. Any inaccessible primary source
needed to substantiate an algorithm claim blocks that chapter's claim review;
record the missing paper and section rather than infer its content.

Keep source behavior changes separate from this reorganization. A discovered
solver bug requires its normal implementation, review, and correctness gates;
a documentation rewrite cannot waive them. Documentation-only moves and prose
need factual/link review and strict builds, not unrelated performance campaigns.

## First manual batch: 2026-09-23

The baseline merge is `13b226c`, bringing in `main` at
`ac0daf4987281fa33d5e438bacdbaf2492607a03` without conflicts. Solver sources,
tests, build files and dependency installers match that source revision.
No solver behavior/default was changed and no fresh benchmark qualification is
claimed. The documentation commit following this baseline carries the first
manual batch and its review record.

Delivered:

- Short README and landing page, the three status entrances and a user-guide
  navigation section.
- Build/first-solve, input/results, model and session guides, plus a practical
  options reference and captured exhaustive CLI help. The old claim that
  quantifiers were wholly unimplemented and sessions only existed on a feature
  branch is removed.
- A concise current capability map. The former qualification narrative is
  preserved in `archive/current-status.md` with relative links adjusted;
  original current-status headings remain as compatibility targets.
- Centrally configured, commit-pinned source links through Sphinx's `extlinks`
  extension; the documented source revision is visible in the book. The 13
  source targets used by the new manual exist at that revision and match the
  checkout. This was checked locally, not by unauthenticated GitHub requests.
- `docs/examples/` contains the programs, expected outputs, help snapshot and
  invocation manifest. `scripts/check_doc_examples.py` checks all 11 invocations
  including help; both default and admitted assumptions engines are exercised.
- `development/documentation.md` records authoring, preview, source-link and
  example-check conventions, and the current archive-format limitation.

Validation on the local Linux workspace: `make build`, `make test` (41 tests),
`python3 scripts/check_doc_examples.py --solver ./build/primo` (11 checks),
strict `make docs`, source-path/revision checks, `git diff --check`, and
`make lint`. The lint pass also makes the pre-existing non-raising subprocess
behavior explicit with `check=False` in `scripts/quantifier_pool_check.py`;
this does not change its treatment of external solver exits and is committed
separately as `85a18f5`. All 14 quantifier Python tests pass. C++ source is
unchanged, so ASan, clang-tidy and fresh fuzz campaigns are not part of this
documentation batch.

Rendered review covered the landing page in light mode and the getting-started
guide in dark mode (`tmp/doc-rewrite/`, ignored preview artifacts). All ten old
heading targets on the rewritten index and capability pages remain in the HTML.
The page map accounts for all 83 current Markdown pages. An empty-output solver
negative control fails all 11 manual checks, confirming failures are reported.

This is a completed manual slice across M0–M2, not completion of those full
milestones or of the public-doc gate. Still required: disposition/anchor reviews
for untouched pages, acceptance of the drafted SAT implementation-template
pilot, automated source link checks and build metadata, private CI previews, full option semantics,
deeper chapter extraction, clean-checkout and archive reproduction, and the
independent reader walkthrough. The local build bootstrapped dependencies but
was not an isolated end-to-end fresh-clone test. Website deployment remains off.

## Acceptance checklist before public release

- Every baseline page has a recorded disposition; additions during the rewrite
  are accounted for. No important explanation or evidence disappears by omission.
- A new reader can build from a clean checkout and the release archive, run a
  first example, request a supported model, and use a scoped session with the
  documented expected results and limitations.
- The README, capability map, CLI/reference pages, and component chapters agree
  on supported logics, default and opt-in modes, and unsupported boundaries.
- Current reference pages state actual implemented contracts. Dated experiments
  and design-only mechanisms are visibly separate; historical measurements
  remain tied to their measured revisions.
- The done/active/open registers reflect reviewed dispositions. Active tasks
  identify their owner or missing ownership, next step, and completion criteria.
- A contributor can find the documentation, testing, fuzzing, model-validation,
  and result-recording requirements without needing a private cluster account.
  Maintainer-only qualification is explicitly routed to a maintainer.
- The paper basis, implemented subsets, deviations, limitations, and relevant
  invariant tests remain traceable after extraction and rewriting.
- `make docs` passes with strict checking. Changed behavioral examples are
  executed, internal and compatibility links are checked, and rendered pages
  are reviewed. Notebook execution remains disabled unless explicitly enabled
  and tested; a book build alone is not behavioral verification.
- Code links resolve to the displayed documented revision in the destination
  repository; source-link configuration is reviewed separately from links for
  editing the documentation on an active branch.
- Editable documentation sources are included in the public release artifact.
  The packaging change has its own appropriate checks, and the documented
  archive workflow is exercised.
- Reviewers record the source revision, page map, example/build results,
  remaining known limitations, and closing commits. All required documentation
  and packaging changes are committed before the publication gate is closed.

## Website and deployment plan

**Recommendation, 2026-09-23: publish the existing book with GitHub Pages.**
Keep authored documentation in `primo-solver/primo` alongside its code. The
proposed project URL is `https://primo-solver.github.io/primo/`. A separate
organization landing page at `https://primo-solver.github.io/` would require
the `primo-solver.github.io` repository; it can later link to the manual and
results without becoming a second documentation source. No custom domain is
needed initially. These URL conventions are documented in
[GitHub's Pages overview](https://docs.github.com/en/pages/getting-started-with-github-pages/what-is-github-pages).
Neither site has been configured by this planning task.

Add the following hosting work alongside the rewrite:

1. **Build checks during M1.** Add `.github/workflows/docs.yml` to install the
   documented Python/docs dependencies and run strict `make docs` for pull
   requests and pushes to `main`. Save `docs/_build/html` as a downloadable
   artifact for review. PR builds receive read-only repository permissions and
   do not deploy. While private, reviewers use authorized Actions artifacts or
   `make docs-serve` locally.
2. **Deployment preparation during M6.** In repository **Settings → Pages**,
   choose **GitHub Actions** as the source when publication is ready. Prepare a
   deployment job using `actions/configure-pages`,
   `actions/upload-pages-artifact` with `docs/_build/html`, and
   `actions/deploy-pages`. Select and pin supported action revisions when
   implementing. Restrict deployment to successful `main` builds, with
   `pages: write`, `id-token: write`, the `github-pages` environment and a
   concurrency group. Keep PR artifact uploads separate from Pages deployment.
   Follow [GitHub's custom-workflow instructions](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).
3. **Publication after the public-release gate.** Enable the deployment only
   when publication is authorized. Verify the deployed `/primo/` base path,
   navigation, search, downloads, logo/theme assets and source links. Add the
   live URL to the README and repository About field. Record the deployed book
   and source revisions and workflow run. A failing build must leave the last
   successful site intact.
4. **Release versions.** Initially label the root site as the development
   manual for `main`, with its exact documented revision. Before advertising
   stable documentation for a tagged release, add preserved `releases/<tag>/`
   builds and a release index; each Pages upload must retain prior versions.
   Do not suggest that updating the root site preserves old release URLs
   automatically. Define that retention workflow before promising those URLs.

**Private source does not imply a private website.** GitHub Free for
organizations supports Pages from public repositories; private-repository Pages
requires an eligible paid plan. Restricting website access itself requires
GitHub Enterprise Cloud for eligible organization project sites, and is not
available for an organization-root site. See the
[plan availability](https://docs.github.com/en/pages/getting-started-with-github-pages/what-is-github-pages)
and [Pages access-control rules](https://docs.github.com/en/enterprise-cloud@latest/pages/getting-started-with-github-pages/changing-the-visibility-of-your-github-pages-site).
The organization's current plan has not been verified. Keep deployment off
during the private rewrite; a public preview would itself publish documentation
and is not part of this plan's preparation work.

The first manual batch, contributor fuzzing requirements, and structural audit
are completed subsets. Passing their build checks does not complete the
restructuring or publish the website.
