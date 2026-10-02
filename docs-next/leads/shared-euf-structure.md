# Sharing immutable EUF preparation between checks

**Status: open; ownership design required. Owner: unassigned.**

Retained UF sessions keep prepared term/atom meanings but create fresh working
theory state per query. Investigate whether immutable preparation can be shared
without copying the entire flat representation into each working closure.
This concerns per-query copying, distinct from
[reclaiming unused incremental storage](incremental-memory.md).

{source}`Lead 25 <docs/open-leads.md#L2830-L2858>` records copies and reset costs
on historical CLEARSY sessions. Reprofile after the accepted atom-scan changes
so savings are not counted twice. Separate copied bytes, allocations, preparation,
working-state reset, callback reconnection and peak memory.

Write the ownership and mutation boundaries first. Per-query equalities,
disequalities, reasons and undo logs must stay fresh; term/atom IDs must retain
their meanings. Backend reconnection, observation and permanent-assignment
replay must work after SAT, assumption-UNSAT, unknown and cancellation. No live
callback or retained clause explanation may refer into discarded storage.

Reject the change if copying is already negligible or a repeated native-session
comparison shows no useful runtime/memory reduction. Skipping required replay
is not an admissible optimization. Models and all scoped behavior require
independent validation and the normal fresh fuzz gate.
