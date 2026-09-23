# QF_UFLRA scale mutation seeds

Real-world files, used as YinYang mutation seeds, chosen specifically for
**shared-interface scale** rather than for illustrating one mechanism the way
`benchmarks/seeds/qf_uflra/`'s compact witnesses do.

## Why this directory exists

`scripts/qf_uflra_smoke_fuzz.py`'s generators (including `--shape shared-real`)
and mutations of the small compact seeds in `benchmarks/seeds/qf_uflra/` never
exercise `consume_no_extra_atom_probe_budget`/the arrangement and
model-reconstruction final-check probe paths under real stress: a local check
of 60 generated `shared-real` cases needed zero real probes, and a 6,000-case
mutation campaign over the compact seed corpus (11 seeds x 2,000 iterations x
3 option profiles) produced zero non-timeout `unknown` answers even with
`--nelson-oppen-arrangement-probe-limit` forced low. The full real 1,284-file
QF_UFLRA library hit two such cases on its very first pass. The probe-budget
machinery only gets stressed by formulas with a genuinely large shared-Real
interface (many EUF-forbidden candidate pairs to separate), which is a
property of real translation pipelines (CPAchecker unrolling a C program,
MathSAT's random-instance generator), not of either fuzz source above.

## Provenance and why these two specifically

Both files are copies of already-tracked, already-reviewed benchmarks:
`tmp/QF_UFLRA/mathsat/RandomDecoupled/pb_real_40_80_60_01.smt2` and
`.../pb_real_40_80_60_36.smt2` from the SMT-LIB QF_UFLRA division (`_01` is
also tracked at `benchmarks/performance/qf_uflra/
mathsat_randomdecoupled_pb_real_40_80_60_01.smt2`, byte-identical). Both have
70-83 shared Real terms -- real interface scale -- while solving in well under
0.1 seconds by default.

**Read `benchmarks/performance/qf_uflra/README.md` before assuming these are
safe.** Both files were historically *excluded* from mutation seeding because
their runtime under some option profiles (`--qf-uflra-candidate-final-scan`,
`--nelson-oppen-prop=online --qf-uflra-online-euf`) could stall YinYang
startup -- `pb_real_40_80_60_36.smt2` specifically needed 60,718 LRA
extra-atom probes and timed out under the old candidate-final-scan path
(`docs/qf-uflra-model-guided-performance.md`). That was re-checked directly
(2026-08-16, commit b930a7c) against today's binary: both files resolve in
under 0.1s under all three mutation-gate profiles (default, candidate-final,
online-euf). The historical slowness was fixed by the retained-epoch and
split-frontier batching work landed since those measurements, not something
this directory works around. If a future regression makes either file slow
again under any of the three profiles, re-verify before mutating -- that
verification is exactly the check this note is asking you to redo, not a
one-time fact to trust indefinitely.

## How to use

Not part of the default `benchmarks/seeds/qf_uflra` corpus or its bounded
pre-mutation seed gate (`scripts/check_solver_seed_corpus.py`'s default
4096-byte `SEED_SIZE_LIMIT` would reject both files). Point
`PRIMO_FUZZ_YINYANG_SEEDS` at this directory explicitly and raise
`PRIMO_FUZZ_SEED_SIZE_LIMIT` (e.g. to 65536) when submitting
`scripts/slurm_qf_uflra_mutation_fuzz_gate.sbatch`. Combine with a low
`PRIMO_FUZZ_EXTRA_SOLVER_ARGS="--nelson-oppen-arrangement-probe-limit N"` to
actually target the probe-budget code paths this directory exists to cover --
without it, mutating these seeds still only stresses ordinary scale, not
budget exhaustion.
