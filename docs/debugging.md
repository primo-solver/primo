# Failure taxonomy

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
