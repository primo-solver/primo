# Documentation inventory audit and proposed contents

**Reviewed 2026-09-24 at `3eddb42568278ea6438b8f43ff33867eec6b9760`.**
Codex completed the structural inventory and disposition pass. The full factual
review, extraction, rewrite, and independent reader review remain open.
The solver revision for the existing manual remains
`ac0daf4987281fa33d5e438bacdbaf2492607a03`.

**Subsequent execution update:** the replacement book is now drafted separately
in `docs-next/`, with its own preview and page review ledger. Inventory counts
below describe the audit checkpoint; the supporting map and proposed contents
also record additions made during that draft work.

## Findings and priority

The main problem is mixed document roles: current contracts, historical evidence,
and proposals often occupy the same page. A fresh reader structure is justified,
but replacing the prose without an extraction record would lose important
invariants, failed experiments, and correctness lessons.

| Priority | Finding and evidence | Required correction |
| --- | --- | --- |
| High | The opening of [](../papers/integer-division-and-modulo.md), lines 3–7, says the frontend rejects `div`, `mod`, and `abs`. {source}`src/smt/frontend.cpp` dispatches them at lines 721–727 and implements the handlers at 1076–1110; [](../guide/input.md) already describes the accepted extensions. | Mark the original proposal as historical and add the current implemented subset. Preserve the nonzero literal-divisor restriction and avoid claiming full QF_LIA grammar conformance. |
| High | Root `TODO.md`, lines 3–29, leaves terms, parsing, SAT integration and DPLL(T) unchecked. `DESIGN.md`, lines 3–10, still describes a scaffold and lazy DPLL(T). Current ownership and callback routes are described in [](../dpllt.md). | Preserve the original checklist as history; replace root entrances with pointers to maintained status and architecture. Do not turn these old unchecked boxes into new open leads. |
| High | `AGENTS.md` names `make fuzzsmt` as a possible stronger merge gate, but `Makefile:126–127` runs 20 cases. `skills/fuzzing/SKILL.md` still presents QF_LIA as future work. The updated [](../contributing.md) correctly states the required case counts and command limitations. | Reconcile the workflow wording during the contributor batch. Keep the 100/1,000/3,000 minimums, mechanism coverage, fresh candidate evidence, failure triage, and independent model validation explicit. |
| High | `scripts/create_source_archive.py` excludes Markdown and does not include the documentation source/fixture tree. The archive therefore cannot reproduce the current book or its guide examples. | Complete editable archive packaging and an extracted-archive walkthrough before public release. The inventory does not change packaging. |
| Medium | [](../quantifiers.md) opens as parsing/representation work, describes an implemented UF route, then labels de Bruijn indices a pending proposal at line 752. | Write one current quantified-UF chapter; preserve the binder-choice debate and evaluations as dated history. |
| Medium | [](../bool-argument-congruence-bug.md) opens with a wrong-answer report and records the fix at line 261. [](../nelson-oppen-qf-lra-regression.md) describes a broken main before its fixed-outcome section. | Lead archived incidents with date, affected revision, disposition, regression links, and current implementation link. |
| Medium | The active table of contents puts the explicitly unimplemented [](../quantifier-entailment-design.md), future [](../difference_logic.md), and rejected [](../root-polarity-clausification.md) beside production components. | Route designs through open leads and rejected work through evidence. Retain any current invariant in the relevant implementation chapter. |
| Medium | [](../debugging.md) is titled failure taxonomy, but statistics occupy its first 1,000 lines. Fuzzing, model validation, and reduction are buried beneath them. | Give testing, fuzzing, independent validation, and debugging their own contributor pages; put counters in a statistics reference. |
| Medium | [](../open-leads.md) has 2,752 lines and includes completed investigations. [](../qf-lra-heuristics.md) contains both accepted mechanisms and rejected/open directions. | Keep a short open index; extract individual investigations by stable ID and preserve dated outcomes. An old branch or proposal is not evidence of active work. |
| Medium | The run ledger has 702 entries, no duplicate nonempty IDs, and one missing ID: zero-based entry 460, dated 2026-08-05, commit `931598f`, kind `reference-stat-diagnosis`. [](../run-log.md) requires an ID. | Reconcile this historical metadata with provenance before storage migration. Do not invent an ID or reclassify its evidence during an editorial move. |

These findings make the documentation **not ready for the public-release gate**.
They do not establish a new solver correctness failure. The source spot check
above confirms a stale documentation claim; it is not a fresh solver evaluation.

## Scope and review depth

The snapshot contains **83 book Markdown pages**, **24 Markdown files outside
the book**, and **31 book support files**. No tracked notebooks exist; the
notebook directory contains a marker. The support inventory also covers seven
build, verification, packaging, or policy files and the three new audit
metadata files.

Every Markdown file received a role/disposition pass using its introduction,
section structure, status statements, and selected implementation/evidence
sections. This is deliberately a structural audit, not a claim that every
paragraph, paper theorem, source mapping, experiment, or external link has been
verified. No historical benchmark or fuzz campaign was rerun. The primary
papers were not reread in this pass; their existing read-depth declarations and
access gaps remain intact.

The review artifacts are:

- {download}`Page dispositions <../documentation-page-map.csv>`: all 83 baseline
  book pages plus this audit page, with material to preserve, specific risks,
  proposed destinations, verification targets, and compatibility obligations.
  Verification targets name what to inspect in the rewrite; they are not a
  claim that those files have all been reviewed.
- {download}`Supporting-file inventory <../documentation-support-map.csv>`:
  initially 65 files covering root documents, agent skills, bug and benchmark notes,
  fixtures, figures, configuration, evidence, and relevant workflow code.
- {download}`Compatibility snapshot <../documentation-compatibility.json>`:
  all 83 old book paths, **1,223 rendered content targets**, and **139 explicit
  MyST labels**. Capturing them does not yet provide redirects or decide every
  target's new destination.
- {download}`Proposed final table of contents <../documentation-target-toc.yml>`:
  a complete proposed navigation tree, separate from the active `_toc.yml`.
  It includes retained research/history and individual lead pages. New paths
  are destinations for reviewed content, not requests to create empty pages.

The page and support maps together cover every tracked Markdown file at the
checkpoint. Source comments, docstrings, external websites, downloaded papers,
and ignored generated reports are outside this Markdown inventory; relevant
ones must be inspected when verifying their owning chapter.

## Preserve the old documentation without a second live manual

The local annotated tag `docs-before-restructure-2026-09-24` points to the
snapshot commit above. It includes the first manual batch and the fuzzing-guide
follow-up. For the documentation before that first manual batch, the earlier
checkpoint is `13b226c`. Neither checkpoint changes the revision of historical
experiments. The new tag has not been pushed.

Use Git to recover complete old pages. The new `archive/` should retain selected
dated investigations, decisions, and evidence, rather than another complete
copy of the old manual. Proposed archive paths in the map are extraction
containers: keep the historical material they own, not duplicate current
reference prose. No existing page is removed by this audit.

Before moving a page, route every captured target to current explanation or
dated evidence. Preserve the old path through a compatibility page containing
the old labels and explicit onward links, or through tested redirect tooling
if that is introduced. A file-level redirect alone may lose section links.
Update internal links in the same batch, including figure paths and references
from root documents, skills, bug records, and benchmark notes.

## Proposed final contents

The home page retains the three status entrances: **done**, **active work**, and
**open leads**, plus a direct first-solve link. These express work state; the
manual itself is organized by reader task. An implemented component can have
both active extensions and open research questions.

| Section | Chapters and reader task | Canonical location |
| --- | --- | --- |
| Work status | Supported behavior and qualification; confirmed ongoing work; open hypotheses | `current-status.md`, `active-work.md`, `open-leads.md` |
| Using Primo | Build and first solve; SMT-LIB input/results; requesting models; sessions/scopes | Existing `guide/` pages |
| Reference | CLI options and failure behavior; tactic inventory; statistics and their meaning | `reference/options.md`, `reference/tactics.md`, `reference/statistics.md` |
| Implementation: foundations | Layer map; terms/sorts/symbols and ownership; exact numerics; frontend; sessions; model reconstruction | `implementation/architecture.md`, `core.md`, `numerics.md`, `frontend.md`, `sessions.md`, `models.md` |
| Implementation: solving | Tactics; preprocessing; clausification; SAT adapter; DPLL(T); EUF; LRA; LIA; theory combination; quantified UF | Remaining `implementation/` chapters in the proposed contents |
| Contributing | Contribution contract; adding a construct/theory/regression; local tests and sanitizers; fuzzing and coverage; independent model validation; debugging/reduction; experiment reproduction; tuning; documentation | `contributing.md` and `development/` |
| Research and evidence | Reading index; paper notes; run ledger; dated investigations and rejected results; predecessor retrospective | `literature.md`, `papers/`, `run-log.md`, `archive/`, retained retrospective |
| Work and lead details | Scoped migration/rewrite/storage plans; one investigation per stable lead ID | `work/`, `leads/`, reached from the status indexes |

Numerics is an explicit addition to the earlier outline: rational arithmetic
and infinitesimals deserve a home outside a history of LRA tuning. Likewise,
`development/contributing-changes.md` must explain how a contributor adds an
SMT-LIB construct, a theory, or a regression; an architectural tour alone does
not teach those tasks.

At the audit checkpoint the proposed tree had 147 paths, including extracted history, individual leads,
and small indexes. That is an extraction plan, not 147 new chapters or a page
count target. Collapse redundant destinations when the material warrants it,
updating the map and compatibility routing together. Closed top-level leads
2b, 3, 3b, 7, and 9 remain in the historical ledger; partially accepted leads
retain only their unresolved scope in the open index. Preserve their labels
regardless of where the explanatory text lands.

## Ownership of facts and extraction rules

The capability matrix owns supported logic/mode/model claims. User guides
explain commands and examples; the option reference owns defaults and domains.
Implementation chapters own current types, lifetime, invariants, failure paths,
algorithm subsets, and test mappings. Paper notes own what was read and what
the source establishes. The ledger and dated archives own measured results.

Contributor requirements remain binding in [](../contributing.md) and
`AGENTS.md`; detailed procedures must agree with them. During extraction, keep
one concise mandatory gate summary in the contribution guide and link the full
fuzzing, validation, and evidence procedures. Do not weaken the gate by moving
its instructions to an obscure appendix.

For each rewrite batch:

1. Read the old sections and the owning code/tests at the documented revision.
   Record source changes since the checkpoint that affect the explanation.
2. Extract current contracts, then dated evidence and open hypotheses. Preserve
   failed approaches, unsupported cases, and paper-specific limitations.
3. Write the current chapter with a worked example and traceable verification.
   Keep an explicit list of claims still requiring paper/code/evidence review.
4. Route old paths/labels, update callers and the page map, run the strict book
   build, and exercise changed examples. Review the rendered result.
5. Mark factual review and migration complete only with recorded evidence.
   Structural review alone never closes the row.

## Recommended next batches

| Order | Deliverable | Acceptance |
| --- | --- | --- |
| 1 | Repair the confirmed stale entrance/status claims and reconcile fuzz-gate wording | Each corrected claim points to current source or is explicitly dated history; no algorithm or old measurement silently reclassified. |
| 2 | Contributor correctness path: testing, fuzzing, independent model validation, bug reduction | A new contributor can select a gate, run a local path, distinguish pass/unknown/timeout/failure, preserve a reproducer, and attach a run record. Cluster qualification has an explicit maintainer handoff. |
| 3 | SAT adapter pilot, then architecture/core/frontend/session/model chapters | Paper → owned types → invariants → tests → evidence traceability; captured links survive the move; numerics has an explicit owner chapter. |
| 4 | Boolean pipeline, theories, and quantified UF, one coherent chapter at a time | Current mechanisms, opt-in features and limits agree with code; historical and rejected alternatives remain accessible. |
| 5 | Complete paper/lead/archive extraction and integration | Every map row and old target reconciled; fresh-checkout and extracted-archive walkthroughs; source-link/build provenance checks; independent reader review. |

These are slices of the existing M0–M6 plan, not replacement acceptance gates.
The contributor correctness path is prioritized because the user identified
fuzzing discoverability as essential. Private preview CI, full option semantics,
packaging, and publication remain outstanding work in
[](../documentation-restructuring.md). Website deployment remains off.
Local source-path and line-range validation is now automated alongside HTML
link checking; see the [maintenance guide](../development/documentation.md#check-hyperlinks).

## Verification of this audit

Coverage checks passed, reconciling the page map with all current book Markdown, the
support map with the 24 outside Markdown files and 31 baseline support files,
and the proposed contents with every destination. Verification paths exist
locally. All 1,223 captured content targets remain in the rendered book after
adding this report, and the checkpoint tag matches the manifest revision.
YAML/JSON/CSV parsing, strict `make docs`, and `git diff --check` passed. The
rendered audit page was visually inspected. The build log is local at
`/tmp/primo-documentation-audit-build.log`; it is not benchmark evidence.

This is documentation and inventory metadata only. No solver code, options,
or defaults change, so no new solver fuzz, ASan, or clang-tidy gate is claimed
or required for this audit. Independent content review and external artifact
availability remain unverified.
