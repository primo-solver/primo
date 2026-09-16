# Benchmark record storage: draft

**Status: proposed migration, 2026-09-16.** This page specifies a replacement
for the shared experimental ledger. It does not migrate records or change the
current requirement to record evidence in `docs/run-log.json`.

## Problem and decision

Branches independently prepend records to `docs/run-log.json`, so unrelated
experiments repeatedly conflict when merged. At the inspected revision
`e344c5d`, the ledger contains 699 entries, 36,789 lines, and 2,804,534 bytes.
There are no duplicate IDs, but one entry lacks an ID. Historical records also
use varied fields and levels of detail.

Keep JSON as the record format and Git as the reviewable metadata history.
Replace the single editable array with **one file per run record**. Generate
indexes, aggregate JSON, and reports from those files. Do not commit a shared
index that every branch must update, and do not replace the JSON array with a
single shared JSONL or YAML file: those retain the same editing bottleneck.

## Proposed layout

```text
benchmarks/runs/
  schema-v2.json
  2026/09/
    2026-09-16-uf-baseline-<uuid>.json
    2026-09-17-uf-model-validation-<uuid>.json
    2026-09-18-uf-comparison-<uuid>.json
docs/run-log.md                     # policy and generated-catalog entry point
results/run-catalog/                # ignored generated indexes and reports
```

Each new record has a UUID allocated once when the work is created. A readable
date and label help navigation; the UUID provides identity across machines and
branches. Use the full UUID in the filename and record, retain it on retrying
metadata collection, and reject duplicate IDs. Do not derive identity from a
branch name, commit, timestamp alone, or SLURM job number. Several runs can use
the same binary, and job numbers are specific to a cluster.

A record can describe one campaign with several shards. The campaign's manifest
identifies expected shards and inputs; concurrent workers write separate raw
outputs, and a collector produces the campaign record. Separate executions or
retries get new records linked by `retry_of`; their results do not overwrite the
first attempt. A comparison is another record referring to its input runs.

## Record contents

Define a versioned schema for new records, with these groups:

| Group | Required information |
| --- | --- |
| Identity | Schema version, UUID, human title, kind, and UTC timestamps. |
| Relationships | Applicable campaign, baseline, retry, correction, and validation target IDs. |
| Executed software | Source revision for each solver/tool, binary SHA-256, build configuration, and exact arguments; identify a dirty source snapshot by its archived patch/tree hash. |
| Inputs | Corpus/manifest identity and hashes, generator/version/seed/count where applicable, and expected coverage. |
| Environment | Host or cluster identity, job IDs, CPU/memory allocation, per-input and whole-job limits, and timing method. |
| Outcome | Execution completion, covered/expected inputs, separate answer/error counts, and available timing/memory summaries. |
| Evidence | Correctness/model-validation coverage, accepted/rejected/inconclusive conclusion, and explicit limitations. |
| Artifacts | Role, location, SHA-256, size, format, and availability/access status for each retained artifact. |

Keep execution status, correctness coverage, and the decision to accept an
experiment separate. A completed timing run is not automatically validated; a
timeout is not a correctness pass. Do not use a mutable global `current` flag
to qualify HEAD. Reports state which revisions and scopes their evidence covers.

Distinguish the code that ran from the commit adding the record. The latter
cannot be embedded as its own Git commit hash. Also distinguish a reused
reference solver's provenance from the current analysis workspace revision.

Use structured arguments and numeric values with explicit units in new records,
with prose for rationale and limitations. Permit explicit missing/unknown values
with reasons where evidence is incomplete; never invent missing historical data.
The schema and validator must make clear when such gaps prevent qualification.

## Publication and follow-up lifecycle

During execution, keep mutable progress journals in the run's working artifact
directory. Write collected records atomically and validate them before committing.
A stopped or failed campaign still gets a record explaining its actual coverage.

Once committed as evidence, treat a run record as immutable. Later model
validation, additional reference confirmation, corrected analysis, artifact
relocation, or a closing report adds a new record with an explicit relationship
to the earlier record. Preserve the original measurements and failed attempts.
Correcting a metadata error must identify the corrected fields and reason;
changing the data used for an analysis requires a new analysis record.

A generated catalog follows these relationships and displays unresolved or
competing corrections. It must not silently choose the newest timestamp as the
truth. Missing references, cycles, and conflicting IDs are validation errors;
incomplete validation remains visible. Human review resolves competing
corrections by an explicit follow-up record.

## Raw artifacts and durable access

Keep small manifests and reviewable metadata in Git. Store large per-input
results, logs, profiles, models, and binaries in compressed artifacts outside
the source repository. Record cryptographic hashes so moving an artifact does
not change its identity. Local and cluster paths are useful collection locations,
but public evidence needs a durable accessible location or an explicit access
limitation.

The organization still needs to choose an artifact host, retention policy,
upload permissions, and backup owner. The metadata format should accept ordinary
URIs without requiring a particular storage service or credentials to build
Primo. An optional local database may accelerate queries, but it is regenerated
from the records and is never the authoritative file committed to Git.

Preserve timing artifacts unchanged. Independent model validation and reference
confirmation have their own records, linked to the exact input/binary/options
they validate. Public comparisons must distinguish unavailable artifacts and
partial coverage from reproducible, fully validated results.

## Migration and verification

Implement this as a dedicated tooling change before switching writers:

1. Add a schema, record creation/validation tool, deterministic catalog builder,
   and a legacy importer. Initially read the old ledger without modifying it.
2. Import every legacy entry losslessly: preserve the original payload and IDs
   in versioned envelopes, record its original position, and retain the ledger's
   top-level metadata. Assign the missing-ID entry a deterministic migration ID
   and record that assignment. Maintain old IDs as lookup aliases so existing
   evidence citations continue to resolve.
3. Verify record count and per-entry canonical JSON hashes, including original
   order through migration metadata. A round-trip export must preserve all JSON
   values; formatting need not be byte-identical. Keep an archived original and
   its byte hash for comparison. Do not discard unexpected fields.
4. Switch producers, policy pages, `AGENTS.md`, and relevant skills together.
   New evidence is written only as individual records after the cutover. Provide
   a generated compatibility export for consumers still expecting an array.
5. Generate the searchable catalog and documentation links without editing a
   tracked central index. Retire the old ledger as a writable source and explain
   how outstanding branches import their additions using the existing IDs.

Required tests include independent additions on two branches merging cleanly;
duplicate IDs; malformed records; unknown schema versions; missing or cyclic
relationships; incomplete shard coverage; partial/failure records; conflicting
corrections; deterministic generation; legacy round-trip preservation; and
artifact checksum mismatch. A test must show that linking model validation
cannot silently change timing results or qualify a different binary/profile.

Completion means the tooling and migrated records are committed, existing
citations remain discoverable, the strict documentation build passes, and two
independent branch additions require no shared metadata edit. Until that
cutover, [](run-log.md) remains the operational policy.
