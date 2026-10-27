# Repeated queries and scopes

A Primo process can handle several queries while retaining the active
assertions and declarations. The default `rebuild` engine creates fresh solving
state per query; it still supports interactive sessions and scopes.

## Add and retract an assertion

```{literalinclude} ../examples/session.smt2
:language: lisp
```

```bash
./build/primo --quiet docs/examples/session.smt2
```

Expected output:

```{literalinclude} ../examples/session.txt
:language: text
```

The base context permits a nonnegative `x`. The pushed assertion contradicts
it. Popping removes that assertion, so the base context is satisfiable again.
Primo also accepts this file on stdin, or commands typed one at a time:

```bash
./build/primo --quiet
```

Each completed command is processed without waiting for EOF. `exit` ends the
session. Successful declarations and assertions are silent by default;
`(set-option :print-success true)` enables acknowledgments.

## Scope rules

`push n` creates `n` levels; `pop n` removes them. Popping beyond the current
depth is an error. Declarations and definitions made inside a pushed scope are
removed with that scope, as are its assertions. Global declarations are not
supported; `:global-declarations true` returns `unsupported`.

`reset-assertions` clears assertions, scoped declarations/definitions and the
base-level declarations/definitions too, returning the assertion depth to zero.
It retains the selected logic and session options. `reset` also clears the logic
and restores SMT-LIB model/acknowledgment options to their defaults; choose a
logic again afterwards. Command-line settings remain process settings.

An assertion, declaration, scope operation or reset invalidates the previous
query's model. Obtain a fresh `sat` result before calling `get-model` again.
See [](models.md).

## Optional reuse between queries

For the admitted QF_LRA and QF_UF inputs, try:

```bash
./build/primo --quiet --incremental-mode=assumptions docs/examples/session.smt2
```

The sample produces the same `sat`, `unsat`, `sat` responses. This opt-in mode
retains SAT and prepared theory structures between checks. It does not support
other logics or quantified formulas, and it does not implement the SMT-LIB
`check-sat-assuming` command.

Use its default companion options initially. Assumptions mode accepts
`--preprocessing-mode=auto` or `off`; it rejects strong/basic preprocessing,
portfolios, Boolean splitting, fixed-variable elimination, preprocess-only,
custom CaDiCaL options and assertion-dependent preprocessing. QF_UF additionally
requires incremental EUF, Boolean registration, auto/online/conflict propagation,
and no experimental Ackermann modes. Forced pure-theory filtering is rejected;
`auto` resolves it to off. These restrictions protect the retained state and
are checked by {source}`src/smt/smt_session.cpp`.

Assumptions mode is not promised to be faster for every input. Historical
qualification and timing limits are recorded separately for
[QF_LRA](../assumption-incremental-lra.md) and
[QF_UF](../assumption-incremental-uf.md). Keep the default rebuild mode as the
starting point unless your workload calls for the opt-in route.

## Implementation and evidence

The session's public boundary is {source}`src/smt/smt_session.hpp`; the command
loop is in {source}`src/main.cpp`. The existing
[implementation chapter](../incremental-smt.md) records replay, lifetime and
scope invariants. Its historical gates apply to the revisions they measured,
not automatically to every subsequent checkout.
