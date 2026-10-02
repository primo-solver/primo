# Public launch preparation

**Status: publication blocked by an unresolved correctness finding; owner review
and GitHub handoff also pending.**

The first launch publishes the repository and manual as a research snapshot.
A new numbered release, results-repository/SQLite migration, and solver research
leads are separate work. Primo-owned material retains MIT. New contributions
will require DCO sign-off; the historical record will not be rewritten to add it.
Repository visibility has not been changed.

## Completion record

| Milestone | State |
| --- | --- |
| Documentation correction and replacement | Current book in `docs/`, historical book in `docs-history/`; strict builds, local/source links and 1,306 historical targets checked. |
| Editable source archives and dependency notices | Prepared and tested, including documentation rebuild without Git and a SLURM source-archive build. Dependency notices travel with rendered HTML. |
| Public CI and Pages workflow | Prepared and actionlint-checked; actual GitHub runs and settings pending. |
| Correctness qualification | [Recorded](../evidence/publication.md): 8,000 generated inputs/sessions, 14 timeouts, 212 real-seed mutants, independent ground-model checks, release and ASan tests. |
| Page-by-page source review | {ref}`Symmetry payload finding <symmetry-payloads>` requires reproduction, repair and fresh qualification. Recorded samples do not establish correctness of the affected transformation. |
| Local Git publication audit | [Recorded](../coverage/publication-audit.json); no secret detections or committed reading PDFs in the inspected named history. Hosted assets remain unaudited. |
| Consolidated reader acceptance | Pending owner review of the [assembled manual](../review.md). |
| Institutional/contributor publication authorization | Pending owner resolution. |
| Imported benchmark publication terms | Unresolved for [19 files](../coverage/third-party-inputs.json); source attribution alone does not grant redistribution rights. |
| GitHub configuration and public visibility | Pending the [owner handoff](github-handoff.md). |

## Remaining publication work

Resolve the [correctness findings](../coverage/page-review.md#correctness-follow-ups)
before treating this candidate as ready to publish. In particular, the symmetry
invariance check omits numeric payloads; documentation edits do not repair it.

The owner must resolve the rights inventory, accept the manual, and review hosted
branches, attachments, release assets and Actions logs before changing visibility.
The prepared workflows then need to run on the approved revision. Configure
required checks, DCO, the protected Pages environment and private vulnerability
reporting using the handoff. Verify the public clone and deployed site after
publication. No new numbered release is needed for this launch.

SSH access to the repository works. GitHub CLI/API authentication is unavailable
in this workspace, so repository settings, hosted artifacts, workflow execution
and deployment cannot be verified here. The owner can perform those steps with
an authenticated browser or CLI; credentials need not enter this workspace.

The empty-dependency-cache SLURM build timed out downloading CLI11. Builds with
provisioned dependencies and the Git-free source archive passed, but fresh
bootstrap remains unverified until GitHub-hosted CI or another connected
allocation runs it. External-link checks retain four current-book and 13
historical-book access failures in [their report](../coverage/external-links.json).
They are separate from passing local/source-link checks.

## Reproducing qualification

{project}`public_launch.sbatch <scripts/qualification/public_launch.sbatch>`
covers QF_UF, QF_LRA, QF_LIA,
QF_UFLRA, QF_UFLIA, quantified UF and the QF_UF/QF_LRA assumptions engines.
Each route has four 250-input shards. Tests/ASan, static analysis, archive
rebuilding and real-seed OpFuzz mutations have separate job recipes in the same
directory. Use the
{project}`workspace-preparation helper <scripts/slurm_prepare_benchmark_workspace.py>`
with `--build-via-slurm` and its built-in preflight before solver jobs.
The owner runs solver batches on SLURM; contributors may use any suitable
machine and must supply the required fuzzing evidence.

{project}`summarize_public_launch.py <scripts/qualification/summarize_public_launch.py>` rejects missing or failed
shards, mixed binaries, skipped SAT-model checks and overlapping session seed
ranges. It reports distinct input hashes separately: independent seeds can still
produce the same small template. Timeouts remain incomplete results. See the
qualification record and run ledger for the exact selected attempts, commands,
resource limits and limitations. These samples make no full-corpus performance
claim; unresolved historical paper-table mappings remain [evidence work](evidence.md).
