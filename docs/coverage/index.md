# Preservation audit

The replacement manual separates current behavior, contributor procedures,
research leads, and dated evidence. The original book remains available during
review. The {source}`file inventory <docs/documentation-page-map.csv>` and
{source}`supporting-file inventory <docs/documentation-support-map.csv>` record
the starting material; a file-level destination alone is not a completeness check.

For each original section, the audit records an explanation or reference
destination, a retained historical record, an open investigation, or a reason
the claim was superseded. Source links preserve precise evidence; they do not
replace an explanation of current behavior.

## Subject audits

- [Frontend and sessions](frontend-sessions.md)
- [Solver chapters](solver.md)
- [Integer arithmetic](lia.md)
- [Tactics and preprocessing](tactics-preprocessing.md)
- [Guides, practice, literature, and leads](practice-literature.md)

## Complete inventories

- {download}`Section inventory <sections.csv>`: 1,150 headings across all 91
  original Markdown pages, with source ranges, current destinations and
  retained records.
- {download}`Supporting-file inventory <supporting-files.csv>`: 126 entries,
  including shared examples, assets, bibliography and build inputs.
- {download}`Compatibility mapping <compatibility.json>`: 1,306 original HTML
  page and fragment targets. The assembled site retains these targets and their
  archived counterparts; the assembler verifies them at every build.
- {download}`External-link audit <external-links.json>`: the full check's
  unresolved access failures and the verified replacement for the refused SOI
  paper host. These are separate from the passing local/Git-link checks.

The section inventory distinguishes current explanations (`preserved`), material
split between the manual and its retained record (`split`), dated history
(`history`), primary-source notes (`literature`), and investigations (`research`).
The [archive catalogue](../evidence/archive.md) links each original page at the
audit snapshot. The `record` role pins that snapshot separately from the
implementation revision used by `source` links.

## Completion checks

Each subject needs its current contracts, ownership, limitations, worked
examples, paper mappings, and relevant test obligations accounted for.
Experiments retain their original revision, configuration, run identity,
outcome, and qualification limits. Rejected attempts remain discoverable.
User acceptance is recorded separately from source inspection and build checks.

These inventories establish destinations, not independent certification of every
historical sentence or experiment artifact. The subject maps identify corrected
claims; [evidence work](../work/evidence.md) names unresolved provenance and
checking limits. Reader acceptance and switching the published book remain
separate steps.
