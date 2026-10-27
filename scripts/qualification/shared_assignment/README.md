# Shared assignment qualification recipes

These scripts preserve the SLURM jobs used to qualify the shared assignment
migration. The source implementation is `45e25a6`; the measured candidate build
is `e4abefb`, with the same C++ source, against baseline `fd4d1eb`. Results and
limitations are recorded in `docs/run-log.json`. They are a bounded migration
comparison, not a full-corpus performance evaluation.

All scripts require a SLURM allocation. Use the repository's
`slurm_prepare_benchmark_workspace.py` for source/build preparation and run its
`--check-only --no-sync` preflight immediately before submission. Preserve the
two Release binaries under separate paths before preparing a later revision.
Pass cluster locations through environment variables; no workstation solver
batches are part of this workflow.

## Inputs and comparison

`benchmarks/shared-assignment-20260925/manifest.json` records the 120 inputs,
SHA-256 hashes, source locations, and selection rule fixed before timing. Stage
those exact bytes under `tmp/shared-assignment-data/comparison`, preserving each
`path`, and stage the profile's input list under `tmp/<profile>-inputs.txt`.
Generated cases come from the recorded fuzz artifacts, not a new random sample.
The raw artifact archive must therefore accompany reproduction of this sample.

Submit `compare.sbatch` once per profile with:

- `PRIMO_WORKDIR`: prepared remote workspace;
- `PRIMO_COMPARE_ROOT`: staged input root;
- `PRIMO_COMPARE_MANIFEST`: profile's staged input list;
- `PRIMO_COMPARE_OUT_ROOT`: a fresh directory for this profile;
- `PRIMO_BASELINE_SOLVER`, `PRIMO_CANDIDATE_SOLVER`: preserved binaries;
- `PRIMO_BASELINE_GIT=fd4d1eb`, `PRIMO_EXPECTED_GIT=e4abefb`;
- `PRIMO_COMPARE_TIMEOUT=5`, `PRIMO_COMPARE_SHARDS=4`;
- `PRIMO_SOLVER_ARGS`: the profile's argument list from the manifest.

Each allocation runs two pairs in opposite arm order. The RSS wrapper records
Linux `RUSAGE_CHILDREN.ru_maxrss`: the peak includes the Python benchmark child
and its solver descendants, and is not an isolated per-input solver measurement.
Full work counters and durable answer rows remain in each timing artifact.

After all timing jobs finish, submit `references.sbatch` with
`PRIMO_REFERENCE_OUT`, and `replay.sbatch` with `PRIMO_COMPARE_OUT_ROOT` pointing
to the directory containing all four profiles. Reference budgets are five
seconds; model and Dolmen budgets are 30 seconds. Replay verifies binary/input
identity and exact successful-SAT coverage. A full-coverage attempt visits every profile even when a validator rejects an input; its nonzero status remains recorded. `lra-model-triage.sbatch` separately checks saved models for LRA inputs rejected solely for constant-denominator syntax, with Dolmen forced to `QF_NRA`. It preserves the original assertions and models and does not relabel these as direct passes under the declared logic. No quantified-UF input in this
sample produced SAT; its unknown/timeout rows are not model-validation passes.

Summarize each profile with `scripts/summarize_component_pairs.py`, using
expected counts 120, 120, 31, and 16. Preserve its strict 1% timing verdict even
when a small sample is too noisy to support a performance conclusion. Any lost
solve needs focused paired reruns; wrong answers or invalid models block
acceptance.

## Correctness and documentation

`scripts/slurm_shared_assignment_gate.sbatch` is the main gate. The supplementary
recipes use the same candidate and record outputs separately:

| Recipe | Additional environment / purpose |
| --- | --- |
| `checks.sbatch` | `PRIMO_CHECK_OUT`, `PRIMO_EXPECTED_GIT`: all C++/Python tests and strict mixed regression seeds. |
| `asan.sbatch` | `PRIMO_ASAN_OUT`, `PRIMO_EXPECTED_GIT`, `PRIMO_ASAN_TOOLCHAIN`, `PRIMO_C_SYSTEM_HEADERS`: all C++ tests with address sanitization and leak detection. |
| `docs.sbatch` | Both strict documentation builds; provision the docs environment before submission if compute nodes lack network access. |
| `uf-extra.sbatch` | `PRIMO_UF_EXTRA_OUT`, `PRIMO_UF_CORPUS`, `PRIMO_EXPECTED_GIT`: 1,000 generated quantified cases across twelve profiles and 200 scope-preserving real-seed mutations across two profiles. |
| `uf-triage.sbatch` | `PRIMO_GATE_OUT`, `PRIMO_UF_TRIAGE_OUT`: independently classify all retained TypeFuzz inputs; inspect Dolmen rejection diagnostics before declaring input invalidity. |
| `mixed-models.sbatch` | `PRIMO_MIXED_RETRY_OUT`, `PRIMO_MIXED_MODEL_OUT`, `PRIMO_EXPECTED_GIT`: successful-SAT-only replay of existing general/finite answer summaries. |

The sanitizer recipe uses a complete GCC toolchain's C++ headers/runtime and
an independently provisioned matching C system-header tree. `-idirafter` is
intentional: placing C headers before the C++ wrappers breaks `#include_next`.
The historical setup failures are retained in the raw results. No such failure
was worked around by moving solver batches to the workstation.

The original submitted temporary recipes differ only in helper locations from
these repository versions. Their exact text and job logs are retained with the
raw artifacts; the run log identifies the jobs and hashes of result metadata.
