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
- `:preprocessing.assertions`;
- `:preprocessing.passes`;
- `:preprocessing.changed-passes`;
- `:preprocessing.eq-bridge.equalities`;
- `:preprocessing.eq-bridge.skipped-or-nodes`;
- `:preprocessing.unit-equality.consequences`;
- `:preprocessing.uf-symmetry.accepted-sets`;
- `:preprocessing.uf-symmetry.constraints`;
- `:preprocessing.uf-symmetry.rejected-noninvariant`;
- `:solver.boolean`;
- `:solver.qf-uf`;
- `:sat.solve-calls`;
- `:euf.theory-atoms`;
- `:euf.checks`;
- `:euf.theory-conflicts`.

Add a counter when implementing a nontrivial preprocessing rule, generated
lemma, SAT interaction, or theory-solver action. Keep names stable once they are
documented; add a new key instead of changing the meaning of an old one.

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
