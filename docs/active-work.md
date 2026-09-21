# Active work

This page records work actually underway. Use [](current-status.md) for what
has been implemented and qualified, and [](open-leads.md) for possible next
work. A draft, an old branch, or a historical job marked pending does not by
itself establish that implementation or execution is active today.

## Public repository migration planning

**Last reviewed: 2026-09-21. Status: planning and documentation review.**
The work is recorded on `docs/public-repository-migration`. A migration owner
and owners for individual milestones are **unassigned**; assigning them is an
open planning action, not an implied assignment to a contributor.

- **Done so far:** reviewable drafts of the migration plan, public contribution
  guide, and benchmark-storage proposal were committed in `869000f`, `4ec07e4`,
  and `2381f8f`. The project owner reports that the `primo-solver` organization
  has been created and is empty; this is not a repository publication. The book
  now has separate entry points for completed work, active work, and open leads.
- **Being worked on:** reviewing the migration plan. The next step is to assign
  milestone owners and settle the release prerequisites.
- **Remaining acceptance criteria:** the [migration milestones](public-repository-migration.md#migration-milestones)
  must have owners and evidence of their outcomes. Rights, licensing, history,
  repository setup, public verification, packaging, and release qualification
  remain pending. The plan must settle whether the proposed database cutover is
  a prerequisite for publication.
- **Related proposal:** [](benchmark-record-storage.md) is design work only;
  this entry does not assert that service implementation or deployment has begun.
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
