# Benchmark records in Git with a generated SQLite index

**Status: direction agreed, 2026-09-21; implementation pending.** Store reviewed
experiment records in a dedicated `primo-solver/results` Git repository and
build an untracked SQLite database for queries. Large artifacts remain separate.
This supersedes the earlier PostgreSQL-first proposal. No results repository,
record schema, importer, or CI workflow is created by this document. Continue
recording evidence in `docs/run-log.json` until the coordinated cutover below.

## Problem and decision

Branches independently prepend records to `docs/run-log.json`, so unrelated
experiments repeatedly conflict when merged. At revision `559dcb5`, the ledger
contains 700 entries and 2,807,542 bytes. There are no duplicate IDs, but one
entry lacks an ID. Historical fields and evidence detail vary substantially.

The agreed design separates durable records from query indexes. Each experiment
adds its own directory in Git. Contributors can collect offline, submit a pull
request, and review evidence without database accounts. SQLite supplies indexed
queries over the records at a specified results-repository commit. Deleting the
local database must lose no authoritative information.

This design needs no central database server, ingestion API, or VM. The results
repository is separate from the solver repository so accumulating experiments
do not enlarge every source checkout. Neither repository stores a changing
SQLite database or a shared generated ledger that every experiment must edit.

## Storage responsibilities

| Store | Responsibility |
| --- | --- |
| Results Git repository | Authoritative reviewed run metadata, manageable per-input result tables, validation records, artifact manifests, corrections, and analyses. |
| Solver Git repository | Collection/validation/index tools, versioned schemas, benchmark manifests, design documentation, and solver reports citing exact results commits. |
| Local SQLite | Disposable query index derived from an exact results commit and pinned schema/tool revisions; never independently edited or merged. |
| Artifact storage or release assets | Large result tables, compressed logs, models, profiles, binaries, and input bundles, identified by hashes and retained at stable locations. |
| Published SQLite/CSV snapshots | Downloadable derived evidence for a fixed results commit, with provenance and checksums. |

Large externally stored tables remain part of the evidence through their tracked
manifests. A full index requires those objects; if any are unavailable, indexing
must fail or explicitly report partial coverage. Missing data must not silently
turn into an empty or successful run. Small metadata-only queries may work
without fetching every artifact.

Git repository size and clone cost need explicit budgets before cutover. Decide
which tables remain in Git using measured record sizes and a representative
pilot. GitHub recommends small repositories and limits individual files; see
[GitHub storage guidance](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github).
Moving a large file out later does not remove it from existing history.

## Record layout and contributor workflow

The initial layout is illustrative; filenames and exact fields must be fixed
by a versioned schema before producers switch:

```text
runs/
  <run-uuid>/
    metadata.json
    results.csv
    validation.csv
    artifacts.json
    analysis.md
corrections/
  <correction-uuid>.json
reviews/
  <review-uuid>.json
```

Optional files must declare whether their data is absent, pending, external, or
not applicable. Multi-shard campaigns use unique attempt/shard records and a
campaign identity; workers do not append to one shared CSV or JSON file. A
published directory may describe an incomplete campaign, with expected and
observed coverage recorded explicitly.

1. A contributor runs a versioned experiment specification with a pinned input
   manifest and collects a local result bundle. Solver execution and timing do
   not depend on network access or a results checkout.
2. A packaging tool validates the bundle and writes its uniquely named directory.
   Retain local results until records and referenced artifacts are preserved.
3. The contributor submits a pull request to `primo-solver/results`. Ordinary
   contributors need no direct write access to the accepted branch.
4. CI checks the schema, unique identities, references, coverage declarations,
   and hashes of available artifacts, and builds the SQLite index. Revalidate
   against the merge candidate so separately valid submissions cannot introduce
   conflicting identities or corrections when combined.
5. A maintainer reviews provenance, reproducibility, and evidence limitations.
   A record can be accepted as a report of a failed or unqualified experiment;
   merging it does not certify solver answers or promote a performance claim.
6. Readers check out a results commit and build the index, or download a
   published snapshot generated from that commit.

CI treats submissions as data. It must not execute scripts or reproduction
commands supplied in result bundles with publication credentials or cluster
access. Maintainers arrange any independent solver/model checks separately.

## Data model and invariants

The record schema and generated relational schema must represent the same
identities and relationships. Exact columns and indexes are implementation work.

| Entity | Identity and essential information |
| --- | --- |
| Campaign | Stable UUID, purpose, expected input/shard coverage, submitter, UTC timestamps, and execution state. |
| Build and configuration | Solver/tool identity, source revision, binary SHA-256, build settings, exact arguments, and relevant environment settings. Dirty source snapshots require an archived patch/tree identity. |
| Input and manifest | Input content hash, corpus/version, manifest hash and membership; generator/version/seed/count where applicable. Paths are labels, not content identity. |
| Execution attempt | Stable UUID, campaign, build/configuration, machine/cluster, job and shard identity, resource limits, timing method, and retry relationship. |
| Individual result | Attempt, input, repetition or query identity, process outcome, solver answer, wall/CPU time, memory and counters with explicit units. |
| Validation | Target result/build/input/configuration, validation method/tool, outcome, coverage, and separate resource measurements. |
| Artifact | Role, checksum, size, format, location(s), access/availability status, and retention policy. |
| Analysis, review, and correction | Referenced runs/results or snapshot, tool revision and parameters, conclusion, limitations, author, date, and correction/supersession relationships. |

Allocate UUIDs before collection and retain them across packaging and submission
retries. A new execution receives a new attempt ID. Reimporting identical
records must not duplicate rows; different contents under the same identity
must be rejected. Enforce unique keys and foreign keys in the generated index.
Include repetition and query identity so repeated measurements and incremental
sessions remain distinct.

Keep execution completion, correctness coverage, record review, and acceptance
of an experiment's proposed change separate. Preserve unknown, timeout, crash,
unsupported, and disagreement outcomes. A stopped campaign remains visible with
actual versus expected coverage. CI format checks are not solver validation.

Record the solver revision separately from collector, schema, and analysis
revisions. Compare performance under compatible machines, limits, and timing
methods; input identity alone is insufficient. Prefer paired baseline/candidate
runs on the contributor's machine. Reused reference answers retain their original
provenance. Model-validation costs never enter timing/PAR2, and validation of one
binary/profile cannot qualify another.

The schema must specify numeric types, units, null values, and CSV encoding.
Preserve exact large integers and other values without conversion through a
lossy floating-point representation. Historical unknowns retain their reasons;
importing them cannot upgrade their evidence status.

## Corrections, validation, and query generation

After merge, measurements are immutable observations. Later validation,
reference confirmation, and revised analysis add linked records with fresh IDs.
Corrections name the target, original value, replacement, author, date, and
reason. Preserve original observations and failed attempts; conflicting
corrections require explicit resolution, not timestamp precedence. Git history
supports review, but an unexplained edit is not a scientific correction record.

The SQLite builder reads a fixed results commit and records that commit, its
schema version, tool revision, and external artifact hashes in the index.
It must reject unsupported schema versions and invalid relationships. An
interrupted build must not replace a previously valid index with a partial one.
A clean rebuild must produce equivalent records and query results; byte-identical
SQLite files are not a requirement. Users change authoritative records through
Git, never by editing the index and attempting to synchronize it back.

Required queries include:

- Compare solved counts, PAR2, memory, and per-family regressions for two exact
  binaries/configurations under compatible execution conditions.
- Find individual inputs that became slower or changed their answers.
- List SAT results without independent model validation and report coverage.
- Find reusable reference answers by input hash with provenance intact.
- Identify incomplete campaigns, missing shards, failed attempts, and retries.
- Reproduce the evidence and analysis supporting a release claim at its cited
  results commit, even after later corrections are merged.

SQLite is suited to local analysis; see its
[data-analysis and deployment guidance](https://www.sqlite.org/whentouse.html).
No worker opens a shared database over the cluster filesystem. Measure build
cost, query latency, and disk usage with representative data before promising
scale. PostgreSQL is a future option only if measured needs justify a service;
record identities and portable formats should survive such a change.

## Publication and maintenance

Published reports identify the solver revision/binary, results-repository commit,
selected run IDs, manifest hashes, and analysis revision/parameters. A moving
branch or a database query without a snapshot identity is not a citation.
Publish downloadable SQLite/CSV snapshots with a provenance manifest and
checksum; give corrections new versions rather than silently replacing them.
Generated static reports can be hosted alongside the documentation.

Assign maintainers for results review, schema/tool compatibility, artifact
retention, repository mirrors, and restoration. Test rebuilding an index from a
fresh clone plus restored external artifacts. A Git mirror cannot back up
objects stored only in release assets or external storage; preserve those
separately. Keep confidential inputs restricted and disclose reproduction
limitations. Audit records for secrets and redistribution rights before public
submission, including command lines and environment metadata.

Normal source and documentation builds must require neither a results clone
nor network access to a database. Contributor collection and local validation
must remain usable offline; public snapshots support analysis without an account.

## Migration and verification

Implement this as a separate tooling task, with the first milestone being the
versioned result-bundle format, validator, legacy importer, and SQLite builder.
Repository creation, access settings, and publication are separate setup work.

1. Assign owners and finalize the schema, artifact-size policy, review workflow,
   and query contract. Version the schema and tools in the solver repository.
2. Test packaging, validation, indexing, and queries on a local results fixture
   before directing real campaigns to the new layout.
3. Import the old ledger losslessly. Retain original entry payloads, IDs, order,
   top-level metadata, and an archived original with its byte hash. Assign the
   missing-ID entry a deterministic migration ID with an explicit mapping.
   Preserve unexpected or ambiguous content. Publication auditing may restrict
   the original archive; any public redaction must be documented, not disguised
   as a lossless copy.
4. Verify counts and canonical per-entry JSON hashes. A legacy round-trip export
   must preserve all JSON values and original order; formatting may differ.
   Preserve old IDs as searchable aliases for existing documentation citations.
5. Import available per-input artifacts with provenance and checksums. Mark
   missing artifacts and summary-only historical records explicitly; do not
   manufacture detailed results from aggregate counts.
6. Pilot independent campaigns from two contributors or machines, including
   simultaneous PRs, a duplicate submission, an interrupted run, later model
   validation, and a correction. Verify offline collection, merge checks,
   report generation, snapshot export, and restoration.
7. Switch producers, policy pages, `AGENTS.md`, and relevant skills together.
   Freeze the shared ledger as historical evidence. Explain how outstanding
   branches migrate additions by existing ID without duplicates. Keep a generated
   legacy export available for older consumers, outside the tracked writable path.

Required tests cover duplicate/conflicting identities; malformed records and
unknown schema versions; missing/cyclic relationships; incremental queries and
repetitions; partial shard coverage; conflicting corrections; external artifact
loss or checksum mismatch; interrupted/repeated index builds; equivalent clean
rebuilds; lossless legacy round trips; and restoration. Verify that additional
validation cannot alter timing rows or qualify a different binary/profile.
Measure index/query performance separately from solver timings.

Completion requires reviewed tools and schemas, reconciled imported data,
preserved citations, an independent-contributor pilot, verified restoration,
reproducible snapshots/reports, and a strict documentation build. Until that
cutover is recorded, [](run-log.md) remains the operational policy.
