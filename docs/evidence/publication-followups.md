# Six-finding publication qualification

The six technical findings A-012, A-013, B-012, B-013, B-014 and C-004 are
repaired and qualified on solver revision
`5c96568bd817eefb3f77e47e6ca55fd8ea99aad1`. The
[closure checklist](../coverage/page-review.md#supporting-tools-and-resource-controls)
identifies each repair and regression. All solver builds, tests, generated gates,
mutations and model validation ran in SLURM. This is a bounded correctness sample;
it makes no performance or publication-rights claim.

## Fresh generated gate

Job `165257` ran four 250-input shards per route, using seed base `202810040`.
Ground seeds are `base + slot`; quantified/session seeds are `base + 10000 * slot`.
Session generators use `seed + index`, so their shard ranges are disjoint.
Z3 5.1.0 supplied reference answers; Dolmen 0.10 independently checked supported
ground SAT models. The solver binary SHA-256 is
`608e6b0f0f06f1de2c65da801632ec59ff0e490486b1b5c218c348b26119411c`.

| Route | Inputs / sessions | Distinct inputs | Passed | Timeout | Other incomplete | Dolmen models |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| QF_UF | 1000 | 999 | 1000 | 0 | 0 | 651 |
| QF_LRA | 1000 | 273 | 1000 | 0 | 0 | 1000 |
| QF_LIA | 1000 | 1000 | 977 | 23 | 0 | 862 |
| QF_UFLRA | 1000 | 1000 | 1000 | 0 | 0 | 578 |
| QF_UFLIA | 1000 | 672 | 999 | 0 | 1 | 498 |
| UF | 1000 | 554 | 1000 | 0 | 0 | 0 |
| incremental QF_UF | 1000 | 793 | 1000 | 0 | 0 | 6260 |
| incremental QF_LRA | 1000 | 1000 | 1000 | 0 | 0 | 8000 |

Of 8,000 generated inputs/sessions, 7,976 passed, 23 had a five-second timeout
and one returned resource-limited `unknown`. Five timeout rows include a Z3
timeout. These incomplete rows are not successful solves. Repeated generator
templates explain lower distinct-input counts; the table does not claim 1,000
unique formulas per route. The session gate covers 20,000 queries. The quantified
UF gate checks six tuple orders and a preprocessing diagnostic against Z3;
quantified models were not independently validated. Dolmen accepted all 17,849
selected ground SAT models, with exact coverage joins.

The QF_UFLIA unknown is input SHA-256
`2e684de5ec74dcd125cd0a643d8f57fd0d0429da02bf6a0af4334ce9b6dcec38`.
Replay job `165296` reproduced `integer branch-and-bound node limit` on both
`5c96568` and the preserved `71cd492` binary, with clean exits; Z3 answered SAT.
The [triage record](../coverage/publication-followups-resource-triage.json) binds
that disposition to the input and replay artifacts. The report verifier requires
that exact record and diagnostic; other unknowns still fail verification.
No untriaged wrong answer, crash, parser/type error, invalid model or reference
disagreement remains in the selected gate.

## Conflict budgets and real-seed mutations

Each generated shard also selected 25 inputs/sessions for a replay with
`--max-theory-conflicts=1`, giving 800 samples: 338 completed with definitive
answers, 461 contained expected resource exhaustion, and one timed out at ten
seconds. The 2,599 returned queries comprise 1,995 definitive answers with zero
reported theory conflicts and 604 unknowns with exactly one. The timed-out query
is incomplete. Every definitive answer agrees with Z3; exhaustion is reported
separately. For completed replays, the verifier rejects duplicate counting and any
overshoot. Partial results from timeout/reference-incomplete rows are not checked
by that acceptance test; the one timed-out sample here returned no answers from
either solver. Unit regressions additionally check typed `ResourceLimit`, unlimited controls,
lazy/online routes, integer conflicts, fallback and query resets.

Mutation job `165258` used the same seed base plus shard index: 14 QF_LRA seeds
with eight iterations each and two QF_UFLRA seeds with 50 each. All 16 seeds and
212 mutants agreed with Z3. The 112 LRA mutants contain 105 distinct inputs; the
100 UFLRA mutants are distinct. Dolmen accepted all 171 mutant SAT models and
six seed SAT models. Validation was separate from measurements and selected
only successful SAT rows, with `--expect-sat` and exact result coverage.

## Verification and provenance

| Check | Evidence |
| --- | --- |
| Clean SLURM qualification build | Preparation helper with `--build-via-slurm`, job `165252`; exact revision preflight passed before submission. |
| Focused runtime/component tests | Job `165253`: 2/2 passed. |
| Release, ASan and examples | Job `165291`: 43/43 release tests, 43/43 ASan tests, 228 Python tests and 12/12 documentation examples passed. |
| Final reporting regressions | Job `165310`: 229 Python tests passed after adding explicit resource-triage reporting. |
| Whole-tree static analysis | Jobs `165255`/`165256`: all 130 tracked C++ translation units scanned. The 18 ordinary diagnostics are unchanged. Include-cleaner reports 404 source/test advisories, matching the pre-repair `a953edd` baseline (job `158839`, `results/symmetry-payloads/static.tar.gz`), plus seven unchanged example advisories. Compared with intermediate `71cd492`, seven advisories were removed and none added. This is not a warning-free claim. |
| Strict artifact verification | Job `165309`: all shard revisions/hashes, selected ground models, bounded replays and mutation coverage accepted. |

The [machine-readable report](../coverage/publication-followups-qualification.json)
contains binary hashes, per-shard outcomes and sorted input-hash manifest hashes.
The [run ledger](../run-log.json) records commands, limits, seeds, jobs, artifacts,
review scope and superseded attempts. Raw inputs and logs remain in the experiment
workspace under `results/public-launch/165257`, `results/public-mutations/165258`
and `results/followup-unknown-165296`.

The report uses metadata-verifier revision `83d35da`; the C++ solver and gate
recipes remain those qualified at `5c96568`. Report repairs after qualification
preserved historical aggregate fields and added explicit hash-bound resource
triage. They did not rerun or relabel incomplete inputs as passes. Earlier
qualification of `71cd492` was superseded after a stricter count audit exposed
callback overshoot. The final gate above uses the repaired callback boundaries.

Publication rights, owner acceptance, GitHub settings and public visibility remain
in the [publication handoff](../work/publication.md). The original October 2
[qualification snapshot](publication.md) and historical strategy fingerprints
remain unchanged as historical evidence.
