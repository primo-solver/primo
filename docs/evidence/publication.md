# Publication qualification

The publication candidate is `6053dc28a88bc2a7eaa968295987516903898766`.
Its C++ solver sources are identical to `c944fa7`; this work changes the manual,
packaging, policies and verification tooling. All solver builds, tests, fuzzing
and model validation below ran in SLURM compute allocations on 2 October 2026.
This is a bounded correctness sample, not a new benchmark or default promotion.

## Generated inputs and sessions

| Route | Inputs / sessions | Distinct inputs | Passed | Timeout | Dolmen models |
| --- | ---: | ---: | ---: | ---: | ---: |
| QF_UF | 1000 | 999 | 1000 | 0 | 619 |
| QF_LRA | 1000 | 261 | 1000 | 0 | 1000 |
| QF_LIA | 1000 | 1000 | 986 | 14 | 869 |
| QF_UFLRA | 1000 | 1000 | 1000 | 0 | 557 |
| QF_UFLIA | 1000 | 672 | 1000 | 0 | 506 |
| UF | 1000 | 559 | 1000 | 0 | 0 |
| incremental QF_UF | 1000 | 825 | 1000 | 0 | 6284 |
| incremental QF_LRA | 1000 | 1000 | 1000 | 0 | 8000 |

There were 8,000 generated inputs or sessions: 7,986 passed and 14 QF_LIA
inputs timed out at five seconds. The reference answered SAT on those 14;
no answer is inferred for Primo. No wrong answer, crash, parser/type error,
unsupported result, reference disagreement or invalid model remained in the
selected qualification runs. Repeated templates explain the difference between
input counts and distinct hashes; the table does not claim 1,000 distinct
formulas per route.

The quantified UF sample ran six tuple orders and the preprocessing diagnostic
against Z3. It did not independently validate quantified models. The two session
routes exercised 20,000 queries with the assumptions engine. Dolmen accepted
17,835 ground SAT models across the generated and session checks. Model checking
is a correctness stage; these measurements support no runtime comparison.

Each route has four 250-input shards. Ground seeds are `202610020 + slot`;
quantified/session seeds are `202610020 + 10000 * slot`. The latter generators
use `seed + case index`. Each session shard therefore has a disjoint seed range.
The reference was Z3 5.1.0 and the model validator Dolmen 0.10.

The [machine-readable summary](../coverage/publication-qualification.json)
records selected artifacts, binary hashes, per-shard counts and hashes of sorted
input-hash manifests. The
[launch recipe](https://github.com/primo-solver/primo/blob/99e43807d2015678d05ee748eef5d5c65b9da70e/scripts/qualification/public_launch.sbatch)
contains the reproducible commands. The
[qualification summarizer](https://github.com/primo-solver/primo/blob/cebb6c2525fbe2cf4bd00a14108b2f34e92e7964/scripts/qualification/summarize_public_launch.py)
checks all selected shards, reference agreement and SAT-model coverage.
These later recipe and verification-tool revisions are distinct from the solver
candidate; their identities are retained in the summary metadata.

## Real-seed mutations

The existing linear-operator OpFuzz configuration ran through YinYang 0.3.0
at revision `04c8618a134e7a6c8b6d54fcd5ee9fd2ec64a2bb`.
The 14 QF_LRA seeds and two QF_UFLRA seeds first passed strict answer comparison.
OpFuzz then retained 112 and 100 mutants respectively, with 105 and 100 distinct
input hashes. All 212 mutants passed independent, exit-status-aware answer
replay. Dolmen accepted all six original-seed SAT models and 152 mutant SAT
models. The mutation seeds were `202610020` and `202610021`, with eight mutations
per LRA seed and 50 per mixed seed. Mutant comparison used a ten-second limit;
original-seed and model checks used 30 seconds. The mutator itself used five
seconds per solver and a 900-second wall limit.

The [mutation recipe](https://github.com/primo-solver/primo/blob/99e43807d2015678d05ee748eef5d5c65b9da70e/scripts/qualification/public_mutations.sbatch)
records the commands. Imported seed
publication terms remain unresolved; successful testing does not establish
redistribution permission.

## Tests, analysis and source archives

- Release: all 43 CTest targets, 208 Python tests and 12 documentation checks
  passed on the candidate. The later report checker has five additional unit
  tests; those tooling-only tests passed locally.
- AddressSanitizer: all 43 CTest targets passed with leak detection and strict
  initialization order enabled. The cluster lacked its ASan runtime; a
  signature-verified Rocky Linux `libasan-11.5.0-14.el9` package was extracted
  into the experiment directory. No system installation was changed.
- Static analysis: all 126 tracked C++ translation units were checked. Eighteen
  diagnostics matched the existing baseline: two ownership-analysis reports
  and 16 enum-probe reports. The ownership paths and guarded enum casts were
  reviewed. The 404 include-cleaner advisories remain documented; this is not a
  warning-free claim. Twenty paired-header reports preserve self-containment,
  one `<variant>` report covers a real `std::get` use, and the rest concern
  transitive includes.
- The source archive at `f95ba8a` built without `.git`, printed its full archive
  revision, and passed all 12 documentation examples. It used provisioned
  dependencies. The extracted source books also rebuilt without `.git`.
- A separate empty-cache bootstrap stopped at a CLI11 download timeout on the
  compute node. Fresh dependency bootstrap and the prepared GitHub-hosted CI
  remain unverified.

## Attempts and evidence limits

The initial job recipes exposed harness errors: executable names where wrappers
required paths; model requests after UNSAT; a missing `ctest`; and an unsuitable
quantifier mutator for ground arithmetic. The original artifacts are retained
and excluded from accepted counts. Session shards with overlapping seed ranges
were superseded by the disjoint-range run. No solver code was changed to obtain
these results.

The [run ledger](../run-log.json) records jobs, resource limits, hashes and
artifact locations. Raw inputs, models and transcripts remain in ignored local
and cluster experiment storage; they are not bundled into the public site.
The committed summary alone is not the complete raw evidence archive.
The [publication checklist](../work/publication.md) separately records reader
acceptance, rights and GitHub configuration. Quantified model checking and the
paper's historical table mapping remain [evidence work](../work/evidence.md).
