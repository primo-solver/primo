---
name: primo-model-validation
description: Validate Primo's printed SAT models on real SMT-LIB corpora or after solver benchmarks, independently with Dolmen. Use for real-corpus correctness checks and before accepting full evaluations or default promotions; keep validation separate from speed measurements.
---

# Primo real-corpus model validation

Keep timing results immutable. Produce a separately identified correctness run
whose coverage can be joined to those results. This skill does not authorize
new cluster jobs beyond the user's task. Resolve missing run scope or approval
before launching substantial work; a speed-only diagnostic can remain explicitly
unvalidated without silently broadening it into a campaign.

## Select the work

- **Post-benchmark:** select every successful Primo `sat` row, even when the
  benchmark's declared status is unknown. Require zero exit status and no timeout
  or error. Keep different option profiles and query identities distinct.
- **Standalone correctness check:** use the requested corpus or a deterministic
  family-balanced manifest. Record this as fresh correctness evidence, not as
  validation of an older timing run on a different binary. A sample is not a
  full-corpus gate. Known-SAT cases answered UNSAT require separate answer triage.
- Full evaluations and default promotions require coverage of every selected
  SAT row for each candidate profile being claimed. Development samples and
  generated/mutation gates do not replace that coverage.

Record the parent run ID, binary SHA-256 and git/build/dependency versions, exact
ordered solver arguments and random seeds, input content hashes, manifest hash,
timeouts, validator version/options, and artifact location. Preserve the binary
and inputs before a workspace rebuild can replace them. Missing provenance or
missing files mean incomplete coverage, not permission to substitute current HEAD.

## Run separately

1. Estimate the rerun budget from recorded SAT runtimes plus model production
   and validator work; known satisfiability does not remove the search cost.
   Use bounded, resumable SLURM shards for nontrivial work. Follow
   [benchmark execution](../benchmarking/references/execution.md) and the available
   `slurm-yinyang` skill for preparation/preflight and collection. Do not run
   validation concurrently on the same resources as headline timing measurements.
2. Replay the same solver profile, adding model production, `get-model` and
   `--check-model`. Preserve the original assertions; modify only a separate
   command-adapted copy. Check process exit status before accepting an answer.
3. Require independent Dolmen acceptance with `scripts/validate_model.py
   --require-dolmen-pass`; pass each solver option as a repeated `--solver-arg`.
   `--dolmen-arg=--strict=false` permits nonessential command/attribute handling;
   input-grammar rejection still needs separate classification. Do not silently
   count limited Z3 fallback or a changed logic as direct Dolmen acceptance.
4. Check the replay answer against the selected row. SAT-to-UNSAT is a failure,
   not a harmless `not-sat` result. A nonzero exit after an answer is an error.
   A timeout/unknown or unsupported validator input leaves validation incomplete.
5. Persist each completed row and retain original/adapted inputs, printed models,
   stdout/stderr, command lines and durations. Resume only with identical binary,
   input, profile and validator identities; do not reuse cached passes across them.

## Current machinery and boundaries

`scripts/slurm_qf_uflra_model_validation.sbatch` already accepts a manifest,
corpus root, sharding, exact solver options and independent solver/Dolmen budgets;
despite its name it has also been used for QF_UFLIA. It calls the shared validator.
Use its explicit environment overrides, not its QF_UFLRA defaults accidentally.

Automatic timing-results-to-manifest submission and report joining are not yet
implemented. Until they are, preserve the selected-row manifest and perform an
explicit coverage/answer join after collection. The validator currently accepts
UNSAT as `not-sat`; this is NOT acceptable for an expected-SAT manifest.

Inspect input command adaptation before using the current string-based adapter.
Existing `:produce-models false`, `exit` before an appended `get-model`, multiple
queries, assumptions and push/pop require a query-aware adapter or explicit
triage. Do not flatten sessions or omit affected files and call the gate complete.
Initially use supported single-query, non-incremental inputs. A requested wider
scope remains incomplete until its command handling is supported and tested.

## Acceptance and reporting

Report expected SAT rows, direct Dolmen passes, invalid models, changed answers,
solver errors, solver timeouts/unknowns, validator timeouts, unsupported inputs,
and missing rows, with per-family/profile coverage. For a standalone mixed-status
sample also report the total inputs and UNSAT rows, without calling them model passes.

Keep solve/model-production/validation durations outside the original runtime and
PAR2 data. Mark the correctness stage pending, complete, incomplete or failed;
only complete coverage with no failures is fully model-validated. Preserve partial
speed results without labeling them correctness-qualified. Model validation does
not certify UNSAT answers or replace reference comparison, fuzzing or proof checks.

Record the linked runs, exact commands, job IDs, counts and artifact paths in
`docs/run-log.json` before citing results. Stop performance promotion on invalid
models or answer changes; retain/reduce failures and add regression coverage.
Do not start an unrelated solver redesign merely because this workflow finds a bug.
