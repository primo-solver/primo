# Request a model

Enable model production **before** `set-logic`, then request the model after a
successful `check-sat`:

```{literalinclude} ../examples/model.smt2
:language: lisp
```

```bash
./build/primo --quiet --check-model docs/examples/model.smt2
```

Expected output for this fixed-value example:

```{literalinclude} ../examples/model.txt
:language: text
```

The interpretation assigns `x` the exact value one third. In a less constrained
problem, different satisfying assignments are equally valid; do not require a
particular arbitrary value or function-table order in an application.

`--check-model` asks Primo to evaluate the original assertions against its own
constructed model. In the default rebuild engine it requires model production
to be enabled separately. The opt-in assumptions engine constructs a model
internally when checking is requested, but `get-model` still requires
`:produce-models true`. This is not independent validation by another solver. The separate
[Dolmen workflow](../experiments.md) is used when qualifying experiment results.

## When a model is available

The [capability map](../current-status.md#supported-routes) identifies routes
with model support. `get-model` requires all of the following:

- `:produce-models true` was set in start mode;
- the most recent query returned `sat` and constructed a model;
- the context has not changed since that query.

Adding an assertion or declaration, changing the scope, or resetting the
session invalidates the previous query result. Run `check-sat` again before
requesting another model. A model is not available after `unsat` or `unknown`.
Quantified formulas do not support model requests.

For example, `docs/examples/stale-model.smt2` obtains SAT, pushes a scope and
then calls `get-model` without a fresh query. It produces:

```{literalinclude} ../examples/stale-model.txt
:language: text
```

This is a command-state error; it does not mean the assertions became
inconsistent. See [](sessions.md) for scoped use.

## Representation and source

Printed models contain typed SMT-LIB definitions. Values for uninterpreted
sorts and function entries are solver-chosen interpretations, not additional
constraints on future solves. The maintained model must assign one result to
each function and evaluated argument tuple.

The {source}`session interpreter <src/smt/smt_session.cpp>` enforces model
availability. The implementation's ownership, reconstruction and evaluation
contracts remain in [](../model-interpretation.md), with the owning model in
{source}`src/core/model.hpp`. That implementation chapter will be separated
from its development history in the next rewrite batches.
