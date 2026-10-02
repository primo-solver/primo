# Public launch preparation

**Status: implementation in progress; repository visibility unchanged.**

The first launch publishes the repository and manual as a research snapshot.
A new numbered release, results-repository/SQLite migration, and solver research
leads are separate work. Primo-owned material retains MIT. New contributions
will require DCO sign-off; the historical record will not be rewritten to add it.

## Completion record

| Milestone | State |
| --- | --- |
| Documentation factual corrections | Committed as `9ea9221`; both strict books and local/source links passed. |
| Main-book replacement and historical routing | Prepared; strict builds, 1,306 compatibility targets, 29,872 local links and 874 pinned Git links pass. Twelve documentation-tool tests and lint pass. |
| Editable source archives and notices | Prepared; local packaging tests pass. Nineteen imported benchmark files lack declared license headers; publication terms remain unresolved. |
| Public CI and Pages workflow | Prepared and actionlint-checked; GitHub execution/configuration pending. |
| Qualification and publication audit | Gitleaks 8.30.1 scanned 1,481 commits across local/fetched refs with no detections. Solver qualification, hosted-artifact review remain in progress. Documentation dependency notices now travel with the site. |
| Consolidated reader acceptance | Pending owner review. |
| Institutional/contributor publication authorization | Pending owner resolution. |
| GitHub settings and public visibility | Pending final authorized handoff; API authentication unavailable here. |

SSH access to the existing repository works. Absence of GitHub CLI login does
not prevent local preparation or Git inspection; it does prevent verification
and configuration of repository settings through the API. Credentials stay
outside tracked files and reports. The owner can perform the final documented
settings steps without transferring credentials to this workspace.

## Qualification recipe

`scripts/qualification/public_launch.sbatch` selects eight routes: QF_UF,
QF_LRA, QF_LIA, QF_UFLRA, QF_UFLIA, quantified UF, and the QF_UF/QF_LRA
assumptions engines. Each route has four independently seeded 250-input shards.
The job verifies the embedded candidate revision and retains commands, binary
hashes, reference/validator versions, inputs and per-case outputs. Ground SAT
models are checked with Dolmen; quantified answers use reference comparison
and retain the documented quantified-model limitation. A missing shard, skipped
fuzzer or incomplete validator run is not a pass. Inspect actual row counts and
classifications, not only the SLURM exit status.

This is fresh correctness qualification of a research snapshot, not a new
full-corpus performance result. Test/sanitizer and real-seed mutation records
are separate parts of the closing qualification report.

## Qualification progress

The source candidate `6053dc2` built successfully in SLURM job 156656 using
provisioned dependencies. Its solver sources are byte-identical to the previously
reviewed `c944fa7` source tree; publication preparation changes documentation and
tooling. Job 156671 runs the 32-shard correctness matrix. Test/sanitizer, static
analysis, archive-build, and real-seed mutation jobs have separate records.

An empty-dependency-cache build (job 156670) failed downloading CLI11: the compute
node timed out reaching the GitHub release URL. This does not establish a source
compilation defect, but fresh dependency bootstrap remains unverified until the
prepared GitHub-hosted CI or another suitable connected allocation runs it.
The source archive's books rebuilt without `.git` using the pinned documentation
toolchain; solver build/example checks from the archive are separate work.

The external-link check retains access failures (publisher HTTP 403, rate limits,
and a certificate error) in [its report](../coverage/external-links.json). These
are not silently ignored and are distinct from passing local/source-link checks.

The first execution exposed harness setup errors: some wrappers require reference
and validator executable paths rather than PATH-only names; QF_LIA model replay
must select successful SAT rows rather than request models after UNSAT; and the
test allocation needs `ctest` as well as `cmake` on PATH. The job recipes were
corrected and affected shards are rerun in new output directories. Original
outputs remain available and are not counted as completed gates. No solver
implementation changed for these harness corrections.

The closing collection also checks per-input hashes. Session generators use
`seed + case index`, so their shard seeds now occupy disjoint ranges. The
arithmetic real-seed stage uses the existing linear-operator OpFuzz configuration
with retained mutants, strict answer replay, and SAT-only model validation. The
quantifier-specific mutator is not suitable for these ground arithmetic seeds.
The sanitizer recipe can use a separately provisioned runtime directory when the
cluster compiler installation is incomplete; it does not change system files.
`scripts/qualification/summarize_public_launch.py` rejects incomplete shards,
mixed binaries, missing SAT-model checks, and overlapping session inputs.
