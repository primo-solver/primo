# OpenSMT on native incremental UF and LRA

OpenSMT 2.9.2 is added as an external reference, using its release binary and
`opensmt --pipe`. The original SMT-LIB streams are passed through real stdin.
No assertion, declaration, scope or query is changed. The
[upstream usage documentation](https://github.com/usi-verification-and-security/opensmt#capabilities-and-usage-examples)
describes native incremental SMT-LIB input; local and remote black-box probes
verify SAT, scoped UNSAT and SAT after pop for both logics. No OpenSMT
implementation source was used.

Only OpenSMT was timed in these new runs. Primo `7ac555c` UF timings and their
independent model qualification are preserved, as are the earlier LRA and
Z3/cvc5 records. The benchmark harness is pinned at `16abc94`; it introduces no
Primo solver change. These are comparisons with recorded runs, not contemporary
alternating measurements. Rebuild remains Primo's default.

## QF_UF: all 1,778 native sessions

SLURM job **142854** uses 30 seconds per whole session, one requested CPU and
8 GiB per task, with four concurrent tasks. The scheduler allocates two logical
CPUs for each requested CPU. An incomplete/error session contributes 60 seconds
to PAR2. Primo profiles retain their recorded 10,000 theory-conflict limit per
query; external references have no equivalent configured cap.

| Profile | Complete | Timeout | Unknown | Crash | Mean PAR2 (s) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Primo assumptions, `7ac555c` | 1,772 | 0 | 6 | 0 | 0.47196 |
| Primo rebuild, `fa2fe5c` | 1,775 | 3 | 0 | 0 | 0.82938 |
| Z3 5.1.0 | 1,778 | 0 | 0 | 0 | 0.16909 |
| cvc5 1.3.4 | 1,768 | 10 | 0 | 0 | 0.83127 |
| OpenSMT 2.9.2 | 1,764 | 10 | 0 | 4 | 0.88779 |

OpenSMT completes 29,489 queries and prints 174 additional definite answers in
incomplete sessions. Every overlapping definite answer agrees with the recorded
Primo/Z3/cvc5 results. There are no observed wrong answers or parser errors;
the four process errors are segmentation faults. OpenSMT's largest direct-child
RSS is 1,201,176 KiB, compared with 925,480 KiB for Primo assumptions and
160,132 KiB for Z3. These are measured peaks, not matched-query memory costs.

| OpenSMT family | Complete / total | Timeout | Crash | Mean PAR2 (s) |
| --- | ---: | ---: | ---: | ---: |
| Goel hardware | 756 / 766 | 10 | 0 | 1.64958 |
| CLEARSY | 1,008 / 1,012 | 0 | 4 | 0.31117 |

The aggregate result does not mean Primo is faster on every session. On the
1,758 sessions both complete, the median Primo/OpenSMT wall-time ratio is 1.393.
For the 1,008 jointly completed CLEARSY sessions it is 1.813; for the 750 jointly
completed Goel sessions it is 0.655. These ratios exclude all incomplete sessions.
Primo completes 14 sessions OpenSMT does not, while OpenSMT completes the six
rushhour sessions where Primo reaches its configured conflict limit. Their exact
indices and outcomes are retained in the summary.

All four OpenSMT crashes reproduce locally with the identical binary, at the
same query positions as on SLURM:

| CLEARSY file | Native index | Answers before SIGSEGV |
| --- | ---: | ---: |
| `0000/00291.smt2` | 778 | 6 |
| `0000/00310.smt2` | 785 | 30 |
| `0001/00313.smt2` | 824 | 33 |
| `0023/00268.smt2` | 1688 | 103 |

Primo and Z3 complete all four inputs; they belong to Primo's previously
qualified native model set. A process crash invalidates the whole OpenSMT
session even though its earlier answers agree. No OpenSMT repair or alternative
parameter tuning is claimed here.

## QF_LRA: the two established BMW sessions

SLURM job **142852** uses the original BMC and induction inputs, 600 seconds per
whole session and the same per-task CPU/memory request as UF. Both LRA tasks run
alongside the UF shards on separate allocated CPUs. These two inputs are a
selection, not the entire QF_LRA archive.

| Profile | BMC, 101 queries | Induction, 202 queries |
| --- | --- | --- |
| Primo assumptions, `04d2e23` | complete, 126.97 s | timeout, 95 answers |
| Primo rebuild, `04d2e23` | timeout, 38 answers | timeout, 53 answers |
| Z3 5.1.0 | timeout, 58 answers | timeout, 73 answers |
| cvc5 1.3.4 | timeout, 69 answers | timeout, 77 answers |
| OpenSMT 2.9.2 | complete, 164.55 s | timeout, 111 answers |

Primo assumptions uses 22.84% less time than OpenSMT on the completed BMC run.
On this two-session selection, mean PAR2 is 663.49 seconds for Primo assumptions
and 682.28 for OpenSMT; rebuild, Z3 and cvc5 each have 1,200 seconds. OpenSMT
reaches 111 induction answers (56 UNSAT, 55 SAT), compared with Primo
assumptions' 95. Both BMC results contain only UNSAT answers matching the source oracle, so there
are no BMC SAT models to check. Induction partial progress is reported separately
from full-session completion; it is not a speedup ratio or correctness
qualification.

## Qualification and reproducibility

Primo UF's recorded independent stage still covers all 1,772 completed sessions,
29,978 queries and 25,027 actual SAT models. No Primo behavior changed in this
comparison, and those binaries/timings were not replaced. OpenSMT's new results
are reference-answer comparisons; no independent OpenSMT SAT-model validation
is claimed. Incomplete LRA induction sessions remain unqualified beyond their
previously checked prefixes. No new default or full-archive LRA qualification is
implied.

The first UF attempt, **142840**, stopped its shard at the first external crash
and was cancelled for triage. Its 558 durable rows (556 complete, one timeout,
one error) are archived separately and excluded from the table. The final run
uses explicit reference-only `--continue-after-error`: errors retain their
penalty and failing driver exit, while later files are attempted. Wrong answers
still stop immediately. The unchanged default driver stops on any error.
OpenSMT's stderr version response is captured in provenance.

Reproduce with `scripts/slurm_opensmt_incremental.sbatch`, using the original UF
inventory and `benchmarks/native-lra-opensmt.json`. The latter retains the two
original input hashes and their hash-qualified status oracles. The comparison
join is `scripts/summarize_opensmt_incremental.py`; it rejects missing coverage
and checks definite answers, including partial answers, before reporting ratios.
Exact tool/input/driver hashes, commands, resource limits, jobs, failure rows and
artifact checksums are in `benchmarks/opensmt-incremental-comparison.json` and
`docs/run-log.json`. Local artifacts are under
`results/opensmt-incremental-67387f3/`.

The helper's binary-download option and the reference error-collection path pass
`make check` (34 C++/CLI and 174 Python tests), `make lint`, `make docs`, shell
syntax and the SLURM validator. Tests exercise post-answer process failure,
continued collection with an overall failing exit, mandatory wrong-answer stop,
stderr version capture, missing coverage and exclusion of partial sessions from
paired ratios. C++/SAT/theory sources remain unchanged, so the prior static,
ASAN and solver fuzz qualification is retained rather than rerun for this
benchmark-only change.
