# Documentation restructuring before public release

**Status: planning and inventory, 2026-09-23; restructuring not implemented.**
The project owner has made substantial documentation restructuring a priority
before the first public release. A private repository transfer can happen first.
The three status entry points added in
`559dcb5` establish the distinction between done, active, and open work; they do
not by themselves reorganize the underlying material. Owner and reviewers for
the rewrite remain unassigned.

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
the repository URL and source revision centrally in the documentation build,
display the documented revision, and generate code links from those settings.
This is planned tooling, not functionality already provided by the current build.

Check the source revision and referenced paths in the destination before
publishing. If a curated history changes commit identities, use the explicit
provenance mapping and available public revisions rather than linking to
unavailable private commits. Keep historical evidence attached to the code
actually measured. See [GitHub's permanent-link documentation](https://docs.github.com/en/repositories/working-with-files/using-files/getting-permanent-links-to-files).

## Inventory findings

The baseline at `70e7aae` has 75 tracked Markdown pages under `docs/`. This is
an audit of navigation, headings, introductory status, and selected content,
not a completed code-to-documentation correctness review.

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

Final paths will be chosen during the inventory step. The table specifies
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

## Execution sequence

1. **Finish the inventory and freeze the review baseline.** Account for every
   tracked page, including pages outside the table of contents. Assign an owner,
   audience, disposition, destination, and any split to a page map. Record source
   and evidence revisions; do not infer current activity from an old branch name.
2. **Build the reader-facing skeleton.** Add getting-started, user-reference,
   contributor, implementation, and archive entry points under the three status
   entrances. Decide canonical ownership for supported fragments, options,
   defaults, and commands so those facts are not independently maintained in
   several narratives.
3. **Rewrite current material against code.** Work by subsystem, starting with
   onboarding, input/mode/model boundaries, sessions, and quantified UF. Inspect
   the actual types and options, retain ownership/lifetime and algorithmic
   invariants, and cite the implemented paper subset. Capture contradictions as
   review findings rather than resolving them by picking the newer prose.
4. **Extract history and proposals.** Preserve original evidence, rejected
   experiments, run IDs, source revisions, commands, and limitations. Add dated
   outcome headers and current-reference links. Move confirmed active work into
   its register and unfinished hypotheses into open leads. Do not manufacture
   fresh qualification or erase inconvenient outcomes during editing.
5. **Update navigation and preserve links.** Apply the reviewed page map in
   coherent commits. Update MyST references and the table of contents; preserve
   explicit labels. For old paths/fragments used in reports or external links,
   retain a compatibility page/anchor or tested redirect. Do not leave two
   independently maintained copies of the same reference text.
6. **Verify examples, packaging, and reader journeys.** Run behavior-bearing
   examples against the documented binary, record expected output and limits,
   build the book strictly, and inspect rendered navigation and long tables.
   Update release packaging so MyST sources ship and the documented archive
   workflow works. Have a reader unfamiliar with the development history follow
   the user and contributor paths.

Keep source behavior changes separate from this reorganization. A discovered
solver bug requires its normal implementation, review, and correctness gates;
a documentation rewrite cannot waive them. Documentation-only moves and prose
need factual/link review and strict builds, not unrelated performance campaigns.

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

The inventory and this plan are the current deliverables. Passing their build
checks does not complete the restructuring described above.
