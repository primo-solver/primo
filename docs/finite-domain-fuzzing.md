# Finite-Domain Fuzzing

Primo has a dedicated finite-domain differential fuzzer because general random
QF_UFLRA generation does not reliably construct finite-domain choices together
with pairwise value disequalities. A 3,000-case mixed campaign missed a valid
CPAchecker input where raw finite-domain AMO clauses reintroduced Real equality
after the lowering pass. Counting generated formulas without checking mechanism
activation was therefore insufficient evidence.

## Generator contract

`scripts/finite_domain_fuzz.py` allocates cases deterministically across QF_UF
and QF_UFLRA, seven scenarios, and SAT/UNSAT verdicts. The scenarios cover flat
and nested choices, residual choices, observed value equalities, nullary
`define-fun` aliases, shared domains, and transitive domain equalities. A seed
randomizes domain size and presentation within each slot; it cannot remove a
scenario from a sufficiently large campaign.

The generator supports the cluster's Python 3.9 runtime. Its Python tests parse
the complete script using the Python 3.9 grammar so newer local interpreters
cannot silently admit newer syntax. Before invoking a solver, each process also
generates one complete logic/scenario/verdict cycle. This makes runtime failures
in a generator branch fail before the campaign spends solver work.

Each verdict is known from construction. Primo is checked against that verdict
and external references. QF_UFLRA cases contain both Real arithmetic and
uninterpreted applications. The fuzzer runs Primo with `--stats`. Its default
`--activation-profile=amo` requires positive raw and emitted finite-domain AMO
activation for every solved case. QF_UFLRA cases also require Real-equality
lowering and mixed-solver dispatch. Strong/default profiles may additionally
require nonzero aggregate post-AMO activation; raw-only profiles intentionally
do not. Opt-in structural-selector campaigns use
`--activation-profile=structural`: each case must activate the structural
selector, detect finite-domain structure, and select a structural profile, but
need not materialize AMO clauses. Solver-route, Real-equality-lowering, answer,
and reference checks remain unchanged.

The result directory contains generated cases, copied failures, `metadata.json`,
and `summary.csv`. Metadata is written with status `running` before solver
execution. Every CSV row is appended, flushed, and synced immediately, so a
terminated SLURM task leaves a countable prefix rather than an apparently clean
empty result. `scripts/summarize_fuzz_results.py` reports expected versus
completed rows, scenario counts, and aggregate activation counters.

## Permanent gates

`make fuzz-finite-domain` runs a bounded local smoke against Z3. It is useful for
development but is not merge-grade evidence.

The standard `scripts/slurm_qf_uflra_fuzz_gate.sbatch` keeps its 3,000 general
mixed cases and adds 1,000 finite-domain cases: 500 strong-profile QF_UF and 500
default-profile QF_UFLRA. Both finite-domain portions compare against Z3 and
cvc5. `scripts/slurm_finite_domain_fuzz_gate.sbatch` is the focused profile
matrix for finite-domain or preprocessing changes; it covers QF_UF strong and
raw-only profiles plus QF_UFLRA default, strong, and raw-only profiles.

The compact seed
`benchmarks/seeds/qf_uflra/finite_real_domain_amo_unsat.smt2` keeps the escaped
Real-domain/AMO interaction in YinYang mutation campaigns. Large industrial
witnesses remain benchmark inputs rather than mutation seeds.

## Acceptance

Unsupported valid input, crashes, wrong answers, reference disagreements,
missing scenarios, incomplete metadata, missing rows, or absent required
activation fail the gate. Solver timeouts are counted separately and do not by
themselves establish a correctness defect. Every merge-grade run must record
the exact commit, commands, seeds, references, counts, SLURM job IDs, and
artifact paths in `docs/run-log.json`.

## Current evidence

The preprocessing-order repair is validated at commit `e6930d0` (solver change
`1a5b0c0`). SLURM jobs `121938` and `121939` completed 1,000 focused
finite-domain cases and the standard 3,000 general plus 1,000 finite-domain
cases with no failures. Mutation job `121947` processed all 11 QF_UFLRA seeds
in default, candidate-final, and online-EUF profiles with zero invalid seeds and
zero bug triggers. Full-corpus jobs `121950` and `121951` then covered all 1,284
QF_UFLRA inputs with no wrong answers, unsupported inputs, errors, or
cross-profile disagreements. The formerly unsupported CPAchecker input now
enters solving and honestly times out at the five-second benchmark limit.

These runs establish correctness and mechanism coverage for the repair. They do
not establish a solved-count improvement: default and candidate-final solved
counts are unchanged from the pre-fix run because the repaired CPAchecker input
does not solve within five seconds.
