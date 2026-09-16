# Benchmark storage and database: draft

**Status: proposed migration, 2026-09-16.** The working direction is PostgreSQL
for collected results, separate artifact storage, and portable exports. No
service, schema, or importer is implemented by this document. Continue recording
evidence in `docs/run-log.json` until the coordinated cutover described below.

## Problem and scale

Branches independently prepend records to `docs/run-log.json`, so unrelated
experiments repeatedly conflict when merged. At the inspected revision
`e344c5d`, the ledger contains 699 entries, 36,789 lines, and 2,804,534 bytes.
There are no duplicate IDs, but one entry lacks an ID. Historical records also
use varied fields and levels of detail.

One file per run would remove most merge conflicts and remains a possible
interim measure. It does not by itself provide efficient cross-run queries,
consistent relationships, or concurrent collection of individual results over
years. We should design for both campaign summaries and per-input results:
1,000 campaigns over 10,000 inputs would produce 10 million result rows before
retries and validation. This is an illustrative workload, not a measured Primo
volume or database capacity claim.

## Storage responsibilities

| Store | Responsibility |
| --- | --- |
| PostgreSQL | Authoritative collected run metadata, configurations, input identities, individual results, validation outcomes, relationships, and artifact references. |
| Artifact storage | Compressed logs, models, profiles, binaries, and large input/result bundles, identified by checksums. |
| Git | Schema migrations, collection and analysis tools, small benchmark manifests, documentation, and reviewed reports identifying immutable evidence snapshots. |
| JSON/CSV exports | Versioned, portable evidence snapshots for publication, offline analysis, and independent reproduction. |
| Optional local SQLite | Offline collection or analysis of a campaign; an upload source or downloaded snapshot, not a second editable authority for accepted central results. |

PostgreSQL is the proposed shared service because branches and cluster jobs
need concurrent transactional recording and indexed joins. Branches and commits
are metadata in the same database, not separate database histories to merge.
PostgreSQL's [concurrency model](https://www.postgresql.org/docs/current/mvcc-intro.html)
is the relevant infrastructure basis; this document does not claim a new
storage algorithm or a measured throughput result.

SQLite is useful for local collection and portable analysis. Do not put one
SQLite database on a shared cluster filesystem for many nodes to write directly;
its documentation recommends a client/server database for that access pattern.
See [SQLite deployment guidance](https://www.sqlite.org/whentouse.html).

There must be one authoritative store after cutover. JSON exports and SQLite
snapshots carry their source snapshot identity and are not independently edited
and synchronized back as competing ledgers. Importing additional observations
creates new results or explicit corrections through the ingestion workflow.

## Queries the design must support

- Compare solved counts, PAR2, memory, and per-family regressions for two exact
  binaries/configurations under compatible execution conditions.
- Find individual inputs that became slower or changed their answers.
- List SAT results without independent model validation and report coverage.
- Reuse reference answers by input content hash while preserving solver,
  configuration, and resource-limit provenance.
- Identify incomplete campaigns, missing shards, failed attempts, and retries.
- Retrieve the exact evidence and analysis supporting a release claim, even
  after later experiments or corrections arrive.

## Data model and invariants

The initial relational schema should distinguish the following entities. Exact
column names and indexes are implementation work, to be reviewed against these
queries before deployment.

| Entity | Identity and essential information |
| --- | --- |
| Campaign | Stable UUID, purpose, expected input/shard coverage, creator, UTC timestamps, and execution state. |
| Build and configuration | Solver/tool identity, source revision, binary SHA-256, build settings, exact arguments, and relevant environment settings. Dirty source snapshots require an archived patch/tree identity. |
| Input and manifest | Input content hash, corpus/version, manifest hash and membership; generator/version/seed/count where applicable. Paths are labels, not content identity. |
| Execution attempt | Stable UUID, campaign, build/configuration, machine/cluster, job and shard identity, resource limits, timing method, and retry relationship. |
| Individual result | Attempt, input, repetition or query identity, process outcome, solver answer, wall/CPU time, memory and counters with explicit units. |
| Validation | Target result/build/input/configuration, validation method/tool, outcome, coverage, and separate resource measurements. |
| Artifact | Role, checksum, size, format, location(s), access/availability status, and retention policy. |
| Analysis and correction | Referenced runs/results or snapshot, analysis script revision and parameters, conclusion, limitations, author, and explicit correction/supersession relationships. |

Allocate UUIDs before collection, persist them locally, and retain them when
retrying uploads. A new execution gets a new attempt ID. Branch names,
timestamps, commits, and SLURM job numbers alone do not uniquely identify runs.
Use uniqueness constraints and foreign keys to prevent double-counting and
orphaned records. Include repetition and query identity so intentional repeated
measurements and incremental sessions are not mistaken for duplicate uploads.

Keep execution completion, correctness coverage, and acceptance of an experiment
separate. A completed timing run is not automatically validated. Retain unknown,
timeout, crash, unsupported, and disagreement outcomes separately; process
success alone does not mean an input was solved. A stopped campaign remains
visible with actual versus expected coverage.

Record the code that ran separately from the collection or analysis revision.
Reference results reused from an older campaign keep their original provenance.
Missing historical fields remain explicitly unknown with reasons; a successful
import must not upgrade their evidence status.

## Collection, concurrency, and corrections

Cluster workers should collect results locally and submit bounded batches
through an authenticated ingestion API or controlled importer. Ordinary workers
must not need database administration credentials. Keep solver execution and
measured solver time independent of uploads and database availability.

Uploads are transactional and idempotent: the same upload identity and payload
can be retried without adding rows; the same identity with different data is an
error requiring investigation. Interrupted uploads resume from acknowledged
batches. Local journals or bundles must survive until collection is verified,
so service downtime does not require rerunning a finished solver experiment.

Verify uploaded artifact checksums and collection completeness before claiming
that a campaign's evidence is fully retained. A campaign can be execution-complete
while artifact collection or model validation is still incomplete.

Lifecycle metadata may advance as a campaign runs. Accepted measurements remain
immutable. New executions, model validation, reference confirmation, and revised
analysis add linked records rather than overwriting timing rows. Metadata
corrections identify the original value, replacement, author, time, and reason.
Preserve original observations and failed attempts. Conflicting corrections must
be visible and resolved explicitly, not by silently preferring a timestamp.

The database should enforce structural integrity; reviewed analysis defines
scientific comparability. Matching input hashes alone does not make timings
from different machines or limits comparable. Model validation costs never enter
the original runtime/PAR2, and validation of one configuration cannot qualify
another.

## Publication, reproducibility, and operations

A live database is not sufficient archival evidence. Published reports cite an
immutable snapshot ID and manifest hash, not a query whose answer changes as
new rows arrive. Export a transactionally consistent selection with schema
version, IDs, relationships, provenance, result rows, artifact hashes, and the
analysis revision/parameters needed to reproduce the report. Keep exact large
integers and other typed values recoverable in the interchange format. Archive
the export and publish a stable location and checksum.

The source repository holds a small citation or report for published evidence,
not a dump refreshed after every run. Public read access and downloadable
snapshots should support analysis without a contributor account. Confidential
inputs or artifacts need explicit access restrictions and visible reproducibility
limitations; never include secrets in stored command lines or environment data.

Before relying on the service, assign an operational owner and settle:

- Hosting and artifact storage, access roles, ingestion permissions, and costs.
- Backup frequency, retention, recovery objectives, and a tested restore process
  covering both database records and referenced artifacts.
- Schema migration and server upgrade procedures, with compatibility between
  collectors, exports, and readers.
- Monitoring of capacity, ingestion failures, missing artifacts, and backups.
- Data retention, correction/deletion authority, and an exit plan that preserves
  portable exports if hosting ends.

Use [PostgreSQL backup and restore guidance](https://www.postgresql.org/docs/current/backup.html)
as an operational reference. JSON exports are useful for portability but are not
a substitute for backing up the service's full state and configuration.

Evaluate indexes and query latency with a representative scaled dataset before
choosing partitioning or extra infrastructure. Record the workload and measured
results; do not assume either that millions of rows require elaborate machinery
or that an untested design will remain fast indefinitely. A normal source build
and documentation build must work without this service or its credentials.

## Migration and verification

Implement the migration as a dedicated tooling task:

1. Agree on the data model, hosting/backup ownership, collection protocol, and
   export contract. Commit schema migrations and tools to Git.
2. Build and test ingestion, queries, exports, and offline collection with a
   disposable database before directing production campaigns to it.
3. Import the old ledger losslessly. Retain original entry payloads, IDs, order,
   top-level metadata, and an archived original with its byte hash. Assign the
   missing-ID entry a deterministic migration ID with an explicit mapping.
   Normalize known fields without discarding unexpected or ambiguous content.
4. Verify counts and canonical per-entry JSON hashes. A legacy round-trip export
   must preserve all JSON values and original order; formatting may differ.
   Preserve old IDs as searchable aliases for existing documentation citations.
5. Import available per-input artifacts with provenance and checksums. Mark
   missing artifacts and summary-only historical records as such; do not
   manufacture detailed results from aggregate counts.
6. Pilot two independent branch/cluster campaigns and verify concurrent uploads,
   analysis, export, and restoration before the cutover.
7. Switch producers, policy pages, `AGENTS.md`, and relevant skills together.
   Retire the shared ledger as a writable source. Explain how outstanding
   branches import their additions using existing IDs without duplicates, and
   provide a generated legacy export for consumers still expecting an array.

Required tests include duplicate/retried and interrupted uploads; concurrent
collection; differing payloads under one identity; malformed data and unknown
schema versions; missing/cyclic provenance relationships; repetitions and
incremental-query identity; partial shard coverage; conflicting corrections;
consistent snapshot export; artifact checksum mismatch; legacy round-trip
preservation; service-outage recovery; and backup restoration. Verify explicitly
that adding validation cannot mutate timing results or qualify a different
binary/profile. Measure representative query performance separately.

Completion requires reviewed tools and schema migrations, reconciled imported
data, preserved citations, tested backup/restore and offline collection, stable
public evidence exports, and a passing strict documentation build. Separate
branches must record runs without changing shared Git metadata. Until then,
[](run-log.md) remains the operational policy.
