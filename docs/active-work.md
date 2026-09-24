# Active work

This page records work actually underway. Use [](current-status.md) for what
has been implemented and qualified, and [](open-leads.md) for possible next
work. A draft, an old branch, or a historical job marked pending does not by
itself establish that implementation or execution is active today.

## Public repository migration planning

**Last reviewed: 2026-09-23. Status: planning and documentation review.**
The work is recorded on `docs/public-repository-migration`. A migration owner
and owners for individual milestones are **unassigned**; assigning them is an
open planning action, not an implied assignment to a contributor.

- **Done so far:** reviewable drafts of the migration plan, public contribution
  guide, and benchmark-storage proposal were committed in `869000f`, `4ec07e4`,
  and `2381f8f`. On 2026-09-23 the project owner reported the transfer to
  [primo-solver/primo](https://github.com/primo-solver/primo) complete; public
  release remains pending. The book
  now has separate entry points for completed work, active work, and open leads.
  The results-storage direction is agreed: Git records in `primo-solver/results`
  with a generated local SQLite query index and separately retained large artifacts.
  On 2026-09-23, the project owner decided to retain the existing MIT license.
- **Being worked on:** the documentation rewrite described below. The
  [rewrite plan](documentation-restructuring.md) specifies target paths,
  canonical facts, source-revision handling and milestones M0–M6. Its page map
  now includes reviewed structural dispositions for 83 baseline pages and the
  new audit report. The [inventory audit](work/documentation-audit.md) captures
  old paths/targets; full factual review and destination routing remain pending.
  GitHub Pages hosting is proposed in the same plan, with private build-artifact
  review first and website deployment after the publication gate; no hosting
  workflow or site is configured yet.
  Substantial documentation restructuring is required before public release;
  its user-facing manual has begun, while the deeper rewrite remains underway.
  The reported transfer is complete; visibility and access settings still need
  verification while the repository is intended to remain private.
- **Remaining acceptance criteria:** the [migration milestones](public-repository-migration.md#migration-milestones)
  must have owners and evidence of their outcomes. Publication rights, sign-off, history,
  repository setup, public verification, packaging, and release qualification
  remain pending. The plan must settle whether the results-record cutover is
  a prerequisite for publication.
- **Documentation gate:** finish the page map and code-grounded rewrites,
  separate current reference from proposals and history, verify examples and
  links, review the rendered book, and include editable sources in the archive.
  Completing this planning entry or its documentation build does not close the gate.
- **Related proposal:** [](benchmark-record-storage.md) is design work only;
  repository setup, record tooling, and legacy migration remain pending. No
  central database service is required by the agreed design.
- **Completion boundary:** completing the planning documents does not complete
  the migration. Publication requires the checks and outside-user reproduction
  described in [](public-repository-migration.md).

## Documentation rewrite

**Last reviewed: 2026-09-24. Status: first manual batch and structural inventory
audit implemented; wider rewrite in progress.** Execution in this session:
Codex. Independent review ownership remains unassigned.
Branch: `docs/public-repository-migration`.

- **Completed subset:** merged `origin/main` in `13b226c`; solver sources match
  the documented baseline `ac0daf4987281fa33d5e438bacdbaf2492607a03`. Added build,
  input, model and session guides, a practical CLI reference, pinned source
  links, executable examples and a shorter capability map. Preserved the old
  qualification narrative in [](archive/current-status.md).
  Added explicit contributor fuzzing requirements in `3eddb42`. The subsequent
  [structural audit](work/documentation-audit.md) covers all 83 baseline book
  pages and 24 outside Markdown files, records stale claims and proposed
  destinations, and captures compatibility targets. The local tag
  `docs-before-restructure-2026-09-24` preserves the pre-audit snapshot.
- **Verification:** the first manual batch passed 11 manual checks, 41 CTest
  tests and the strict book build. The inventory batch passed coverage,
  proposed-navigation, and compatibility checks plus strict `make docs`; its
  rendered report was inspected. These are editorial checks, not fresh solver
  qualification. Detailed batch limits belong in
  [](documentation-restructuring.md) and [](work/documentation-audit.md).
- **Next:** correct the audit's stale status claims and reconcile fuzz-gate
  wording, then extract the contributor correctness procedures and complete
  the SAT chapter pilot before the deeper implementation rewrite. The audit's
  proposed final contents now gives those batches concrete destinations.
  Do not move historical pages without preserving their evidence and targets.
- **Remaining acceptance:** the SAT template pilot, deeper implementation and
  contributor rewrites, open-lead/history extraction, clean-checkout and archive
  walkthroughs, independent reader review and publication gates remain open.
  No solver behavior or defaults are changed by this manual batch.

This is an initial register based on the work reviewed on this branch. Other
contributors should add confirmed active work; absence here is not evidence
that no work exists elsewhere.

## Maintaining this register

For each item, record the last review date, owner (or explicitly unassigned),
scope, branch/PR or design link, completed subset, next step, blockers, and
remaining acceptance criteria. Distinguish implementation, review, qualification,
and blocked work. Update the entry when any of those changes; a blocked item
needs the condition that would let it resume.

When work is accepted, update the relevant reference page and
[](current-status.md), link the closing revision and evidence, and remove the
active entry. Split out unfinished follow-ups as open leads. When an experiment
is rejected or work is deferred, record the outcome and reason in its design or
evidence page and update the lead's disposition. Preserve historical run records.
