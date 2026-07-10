# Failure taxonomy

## Solver statistics

Use `--stats` to print numeric counters to stderr while leaving stdout as the
single SMT result line:

```bash
./build/primo --stats input.smt2
```

Statistics are intentionally stable, colon-prefixed keys so benchmark scripts can
compare runs mechanically. Current counters include:

- `:input.assertions`;
- `:frontend.ite.boolean`;
- `:frontend.ite.auxiliaries`;
- `:preprocessing.assertions`;
- `:preprocessing.passes`;
- `:preprocessing.changed-passes`;
- `:preprocessing.eq-bridge.equalities`;
- `:preprocessing.eq-bridge.skipped-or-nodes`;
- `:preprocessing.unit-equality.consequences`;
- `:preprocessing.finite-domain.domains`;
- `:preprocessing.finite-domain.amo-clauses`;
- `:preprocessing.equality-definitions`;
- `:preprocessing.ground-congruence-clauses`;
- `:preprocessing.ground-congruence-skipped-groups`;
- `:preprocessing.uf-symmetry.accepted-sets`;
- `:preprocessing.uf-symmetry.constraints`;
- `:preprocessing.uf-symmetry.rejected-noninvariant`;
- `:solver.boolean`;
- `:solver.qf-uf`;
- `:sat.solve-calls`;
- `:euf.theory-atoms`;
- `:euf.checks`;
- `:euf.theory-conflicts`;
- `:euf.propagation.rounds`;
- `:euf.propagation.candidates`;
- `:euf.propagation.delivered`;
- `:euf.propagation.reason-lits`;
- `:euf.propagation.budget`;
- `:euf.propagation.delivery-budget`;
- `:euf.propagation.budget-cutoffs`;
- `:euf.propagation.hit-rate-cutoffs`.

The propagation counters are used by both `--euf-prop=model` and
`--euf-prop=online`. In online mode, one round corresponds to a callback pass
that inspects currently unassigned observed theory atoms.

Add a counter when implementing a nontrivial preprocessing rule, generated
lemma, SAT interaction, or theory-solver action. Keep names stable once they are
documented; add a new key instead of changing the meaning of an old one.

## Fuzzing and Reduction

Primo follows the Brummayer-Biere SMT fuzzing/debugging pattern: generate or
mutate well-sorted SMT-LIB formulas, compare Primo against trusted reference
solvers, and reduce each crash, timeout, unsupported-input regression, or answer
disagreement to a smaller reproducer.

Current entry points:

- `scripts/yinyang_fuzz.sh` runs YinYang/typefuzz against Primo and a reference
  solver;
- `make fuzz` runs the same path when `typefuzz` is installed and otherwise
  records a clear local skip;
- `make fuzzsmt` runs Brummayer's FuzzSMT generator for `QF_UF`, compares
  Primo with Z3, and copies disagreements or crashes under
  `fuzz-results/qf_uf-fuzzsmt/bugs`;
- `scripts/compare_with_z3.py` classifies Primo/reference outcomes;
- `scripts/bench_interesting_qf_uf.py` records `file,status,answer,seconds`
  plus family and PAR2 summary rows for the current hard QF_UF target families
  under `tmp/QF_UF`; pass `--expected expected.csv` with `file,answer` columns
  to count checked wrong answers;
- `scripts/minimize_smt2.py` delegates to ddSMT when `ddsmt` is installed and
  otherwise copies the input unchanged with a clear message.

Use YinYang for mutation of real SMT-LIB seeds and FuzzSMT for grammar-based
black-box generation. Both are useful: YinYang stays close to benchmark shapes,
while FuzzSMT stresses frontend and Core/QF_UF coverage with independently
generated formulas.

Long fuzzing campaigns should run on SLURM, not on developer workstations.
Preserve generated failures under `yinyang_bugs/` until each one is copied back,
classified, minimized, and either turned into a regression test or documented as
an unsupported feature.

## Result failures

- `UNSOUND_SAT`: Primo says sat, trusted solver says unsat.
- `UNSOUND_UNSAT`: Primo says unsat, trusted solver says sat.
- `UNKNOWN_REGRESSION`: Primo now says unknown on a previously solved case.
- `REF_DISAGREEMENT`: reference solvers disagree.

## Runtime failures

- `CRASH`
- `ASSERTION_FAILURE`
- `TIMEOUT`
- `MEMORY_LIMIT`
- `NONTERMINATION`

## Model failures

- `INVALID_MODEL`
- `PARTIAL_MODEL`
- `SORT_INCORRECT_MODEL`
- `FUNCTION_INTERPRETATION_MISSING_DEFAULT`

## Infrastructure failures

- `PARSER_REJECTS_VALID_INPUT`
- `PRINTER_PRODUCES_INVALID_SMTLIB`
- `TYPECHECKER_ACCEPTS_INVALID_INPUT`
- `NONDETERMINISTIC_OUTPUT`
