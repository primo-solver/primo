# Results storage

**Status: agreed design; implementation pending.**

The current authoritative record is the [run ledger](../run-log.json).
The proposed replacement stores separate run directories in a results Git
repository, with a locally generated SQLite index for queries. Large artifacts
remain separate and are identified by hashes; published references identify
the results commit. SQLite is derived data, not a concurrent shared database
that contributors must host.

Before implementing the change, follow the original
{source}`storage design and migration checks <docs/benchmark-record-storage.md>`:
preserve stable run IDs and historical records, validate metadata and artifact
identities, support results from different contributors, and retain the old
ledger until import/reproduction checks pass. This documentation rewrite does
not create that repository or change the evidence format.
