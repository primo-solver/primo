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
- **Being worked on:** making the documentation rewrite executable. The
  [rewrite plan](documentation-restructuring.md) now specifies target paths,
  canonical facts, source-revision handling and milestones M0–M6; its downloadable
  page map covers all 76 baseline documentation pages. Dispositions are proposed,
  and content/anchor reviews remain pending. The next step is M0: confirm the
  merged source baseline, review the map and assign rewrite/review ownership.
  GitHub Pages hosting is proposed in the same plan, with private build-artifact
  review first and website deployment after the publication gate; no hosting
  workflow or site is configured yet.
  Substantial documentation restructuring is required before public release;
  its inventory and execution plan are recorded, but the rewrite is pending.
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
