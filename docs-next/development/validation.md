# Independent model and session validation

Primo's `--check-model` evaluates original assertions with its own model
implementation. Independent validation must check the printed response using
another implementation. It is a separate correctness stage after timing,
not part of solver speed measurements. The current model capabilities are in
[the user guide](../guide/models.md) and [model construction](../implementation/models.md).

## Validate printed models

{source}`validate_qfuf_models_with_dolmen.py
<scripts/validate_qfuf_models_with_dolmen.py>` supports more than QF_UF despite
its historical filename. With a built solver and installed Dolmen:

```bash
python3 scripts/validate_qfuf_models_with_dolmen.py \
  --solver ./build/primo --dolmen contrib/local/bin/dolmen \
  --timeout 10 --dolmen-timeout 20 --require-dolmen-pass \
  benchmarks/smoke/qf_uf/*.smt2 --out results/model-review
```

Pass each solver option with a separate `--solver-arg=...`. Do not put options
inside the executable path. For a corpus, use the driver's manifest/root and
shard options. Preserve input, `.rsmt2` response, stdout/stderr, metadata and CSV.
Require complete metadata and one classified row for every selected input.

Only successful SAT rows can supply models. Select them using
{source}`select_fuzz_sat_inputs.py <scripts/select_fuzz_sat_inputs.py>` when
validating a fuzz run. Requesting `get-model` after UNSAT creates a protocol
error, not a model counterexample.

## Grammar and validator boundaries

Record the exact Dolmen version and arguments. Some generated LIA and real-corpus
linear expressions require `--dolmen-arg=--strict=false`; that relaxes grammar
acceptance, not assertion evaluation. Record any logic widening separately,
preserve the original problem, and do not count a widened-logic check as a
direct pass under the original declaration.

The driver's `validator` field separates `dolmen`, `z3-fallback`, and `z3-only`.
A fallback may aid diagnosis but does not satisfy a gate requiring direct
Dolmen passes. Solver timeout, validator timeout, validator memory exhaustion,
unsupported grammar and invalid model are distinct outcomes. Do not reinterpret
resource failure as validation success or increase limits without recording it.
Build Dolmen through `make contrib-dolmen` when a prebuilt binary is incompatible
with the execution environment.

## Validate native sessions

{source}`validate_incremental.py <scripts/validate_incremental.py>` compares
query sequences and checks their models:

```bash
python3 scripts/validate_incremental.py \
  --solver ./build/primo --ref contrib/local/bin/z3 \
  --dolmen contrib/local/bin/dolmen --count 1000 --seed 4101 \
  --timeout 10 --dolmen-timeout 20 --out results/session-review
```

For retained mode, add `--solver-arg=--incremental-mode=assumptions` and use
inputs admitted by that engine. Also exercise actual corpus sessions, live
stdin, nested scope restoration, redeclaration, cancellation, and model
invalidation. A single-query replay checks one active context; it does not
check native response ordering or retained-state lifetime by itself.

Use the native-prefix workflows when a whole session exceeds the available
budget. Report exactly which prefixes and SAT models were checked. A prefix
pass is not a fully validated session. Keep validation artifacts separate from
the unchanged speed run and link both in the run ledger.

## Quantified answers

Printed models of quantified formulas are unsupported. The separate pool-guided
cvc5 checker addresses selected quantified UNSAT answers; it is not the ordinary
Dolmen model stage. Its satisfiability-preservation conditions and remaining
unconfirmed answers are documented in the
[quantifier checking record](../evidence/quantifiers.md).
