# October evaluation refresh

This run establishes a new Primo timing baseline on revision
`5efbd02293e15a53fbaad748ceda78bc7d27bbac`. All timings are complete. Ground SAT-model replay and native UF qualification
passed; **two native LRA sessions remain incomplete** because Z3 reference
checks timed out. Full-session replays with Yices and OpenSMT are running. The
[run ledger](../run-log.json), entry `2026-10-05-full-evaluation-refresh`, records
the jobs, commands, snapshots and remaining work. Earlier measurements retain
their original revisions and dates.

## Measurement scope and limits

All measurements run in SLURM compute allocations, with one solver process per
single-CPU task. The fresh non-incremental profile is `primo --quiet`, with the
current defaults. Each input receives 60 seconds of wall time; the allocation
provides 8 GiB per task. These are process measurements, including input parsing.
The job allocation's three-hour limit covers a shard of many separate inputs;
it is not the per-input solver timeout.

| Non-incremental division | Inputs | Per-input timeout |
| --- | ---: | ---: |
| QF_UF | 7,503 | 60 s |
| QF_LRA | 1,753 | 60 s |
| QF_LIA | 13,306 | 60 s |
| QF_UFLRA | 1,284 | 60 s |
| QF_UFLIA | 659 | 60 s |
| UF, quantified | 7,590 | 60 s |

The five ground corpora match every relative path and content hash in the
historical ground inventory. The missing QF_LIA copy was restored from the
[official SMT-LIB 2025 snapshot](https://zenodo.org/records/16740866), with the
published archive checksum checked before extraction. Its 13,306 files also
match the historical hashes. The fresh inventory records all 32,095 inputs,
binary identities and CPU information.

Native incremental measurements retain each file's assertions, scopes and query
order. A session means the entire file, not a single `check-sat`. Both `rebuild`
and `assumptions` modes use `--quiet --stats --max-theory-conflicts=10000`, plus
`--incremental-mode=MODE`. The conflict limit resets per query; it does not bound
parsing or elapsed time. Both profiles receive 8 GiB per task and a two-hour
shard allocation.

| Native corpus | Sessions | Queries per session | Total queries | Whole-session timeout |
| --- | ---: | --- | ---: | ---: |
| QF_UF | 1,778 | 2–494; median 3 | 29,990 | 30 s |
| QF_LRA | 10 | Five with 101; five with 202 | 1,515 | 600 s |

A completed session must return a definite answer to every query, agree with
known status annotations and exit successfully. Partial query progress, unknown answers and timeouts are reported
separately. An unknown from the conflict limit is resource exhaustion, not a
successful solve. PAR2 charges an unsolved input or incomplete session twice its
corresponding timeout; completed cases contribute their measured wall time.

## Completed timing results

All 32,095 non-incremental inputs were processed, with no execution errors or
reported wrong answers. Timeouts and unknown answers remain unsolved.

| Division | Solved | Timeout | Unknown |
| --- | ---: | ---: | ---: |
| QF_UF | 7,497 | 6 | 0 |
| QF_LRA | 1,537 | 216 | 0 |
| QF_LIA | 9,705 | 2,850 | 751 |
| QF_UFLRA | 1,270 | 14 | 0 |
| QF_UFLIA | 574 | 85 | 0 |
| UF, quantified | 1,954 | 5,602 | 34 |

Independent Dolmen checking passed all 11,699 selected ground SAT models.
Native UF completed 1,775 sessions in rebuild mode and 1,772 in assumptions
mode; their separate qualification checked 24,746 and 25,027 SAT models,
respectively, with no changed answers or validation failures. The remaining
three rebuild sessions timed out; six assumptions sessions exhausted their
conflict limits.

Native LRA completed no rebuild sessions (all ten timed out), and three
assumptions sessions (seven timed out). One completed assumptions session is
qualified. The other two stopped during Z3 reference checking after 82 and 68
successful query comparisons, respectively; a reference timeout does not
establish a wrong Primo answer. Alternative references must check the full
101-query sessions before they can close coverage.

## Reference reuse and model qualification

The user requested cancellation of redundant Z3 and cvc5 timing reruns. Their
partial fresh artifacts are retained but excluded from the comparison. Audited
historical reference rows keep their original origin and resource conditions.
Nine complete ground reference sets match the fresh input hashes, unchanged
reference binary hashes, arguments and 60-second limit. Historical cvc5 QF_LIA
is incomplete and is not treated as a complete reference result. The strict
report currently has no eligible historical quantified UF reference set.

The older ground reference allocations used one CPU and 4 GiB; the new Primo
allocations use one CPU and 8 GiB. Comparisons provide historical context, not a
controlled same-node measurement of a source change. Native UF reference rows
cover the same 1,778 sessions, manifest hash, binary identities and 30-second
limit. Native LRA reference timings are unavailable in this refresh report.
These timing limitations do not remove the fresh correctness references in the
generated gates or live native model replay.

Model production and checking begin after the entire timing phase, so they do
not enter the timing or PAR2 measurements. Ground replay selects only measured,
successful SAT rows and requires exact coverage and independent Dolmen acceptance.
Its allocation is 12 GiB per task, with 90 seconds for model production and
30 seconds for Dolmen; Dolmen's own accounting limit is `--size=8G`. Native replay
validates completed sessions, compares their replayed answers to the timed
answers and checks their SAT models separately. Quantified models remain outside
the independent ground-model workflow. Missing, changed or failed model rows
leave qualification incomplete.

## Completed correctness qualification

The [machine-readable qualification report](../coverage/evaluation-refresh-qualification.json)
binds the fresh eight-route gate to this revision and its binary hash. Each
route received 1,000 generated inputs or sessions. Of 8,000 cases, 7,978 passed
and 22 QF_LIA cases timed out; these timeouts are not successful solves. Dolmen
accepted all 17,877 selected ground SAT models. The session routes cover 20,000
queries. This is a bounded sample; repeated generator templates do not imply
8,000 distinct formulas, and quantified models were not independently checked.

Conflict-limit replay selected 800 generated inputs/sessions at a limit of one:
344 completed definitively and 456 contained expected exhaustion, with no replay
timeout. The 2,600 returned queries comprise 1,965 definitive answers with zero
conflicts and 635 exhausted queries with exactly one. No overshoot was observed.
All 16 real seeds and 212 mutations passed their answer checks; independent
validation accepted 165 mutant SAT models and six seed SAT models.

A supplemental campaign passed 3,000 mixed QF_UFLRA cases and 500 cases in each
of finite QF_UF and finite QF_UFLRA. Its general and finite-UFLRA SAT selections
have exact independent model coverage: 1,709 and 252 models respectively. The
finite-UF portion supplies answer comparisons, without an additional model
replay in this supplemental campaign. No untriaged wrong answer, crash or
reference disagreement remains in these completed gates.

Release and ASan suites passed 43/43 tests each, with 229 Python tests and 12/12
documentation examples. Static analysis rescanned 126 source/test translation
units plus four documentation examples. The 18 ordinary diagnostics and 404
source/test include advisories match the retained baseline; seven example
include advisories also remain. This is not a warning-free claim.

## Reproduction and acceptance

Follow the [refresh workflow](../development/experiments.md#refreshing-a-timing-baseline)
with a fresh SLURM workspace and `--build-via-slurm`. The measured binary SHA-256
is `d0f69f50540efa7561e7895d2f24acff26787756a85b9bdb9f67e7eaaed496e0`.
Reporting-only commits do not change that binary. Raw artifacts remain under
the `primo-evaluation-20261005` experiment workspace; the ledger names each
result directory and job. The final metadata report requires complete timing
coverage and supported model qualification before returning success. Its first final attempt (job `168188`) correctly failed on the two native LRA
reference timeouts. Alternative replay job `172138` retains those attempts and
the original timing data unchanged. Final native LRA acceptance remains pending.
