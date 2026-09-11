# define-fun bodies are scoped dynamically, not lexically

## Status

**OPEN in `main`; fixed on `feature/incremental-smt-sessions`, not yet merged.**
Wrong answers on conforming SMT-LIB, in both directions. Found 2026-09-11 while
planning quantifier parsing, where the same mechanism would capture bound
variables; see `docs/quantifiers.md`.

The unmerged branch fixes it in `5bc811b` (2026-09-09). `lookup_bound_term`
stops at the innermost definition's scope base (`definition_scope_bases_`), and
`definitions_are_lexical_not_dynamic` in `tests/unit/smt_session_tests.cpp`
covers both the call-site `let` shape and the enclosing-parameter shape. A
separate fix on branch `quantifiers` duplicated it and was dropped.

## Witnesses

`witness.smt2`, in QF_LIA:

```smt2
(declare-fun b () Int)
(define-fun f ((a Int)) Int (+ a b))
(assert (let ((b 5)) (not (= (f 1) (+ 1 b)))))
```

The `b` in `f`'s body is the global constant. Inside the `let`, `(+ 1 b)` is 6
and `(f 1)` is `1 + b`, so the assertion says `b /= 5`: satisfiable.

Two QF_UF shapes, both unsatisfiable. Both use
`(declare-fun a () A) (declare-fun b () A) (assert (not (= a b)))` and
`(define-fun is_b ((x A)) Bool (= x b))`:

- a call-site `let`: `(assert (let ((b a)) (is_b b)))`;
- an enclosing definition's parameter:
  `(define-fun wrap ((b A)) Bool (is_b b)) (assert (wrap a))`.

| input | primo `24ede53` | z3 | cvc5 | yices-smt2 |
| --- | --- | --- | --- | --- |
| `witness.smt2` | `unsat` | `sat` | `sat` | `sat` |
| call-site `let` (QF_UF) | `sat` | `unsat` | `unsat` | not run |
| enclosing parameter (QF_UF) | `sat` | `unsat` | `unsat` | not run |

`24ede53` (`build/primo`) has the same `src/` as `b529a8b`, the commit
`quantifiers` started from. The reference solvers are the binaries in
`contrib/local/bin`.

## Mechanism (as in `main`)

`TermBuilder::dispatch_instantiate_definition` (`src/smt/frontend.cpp`) binds
the parameters by pushing one more map onto `let_scopes_`, then builds the
stored body s-expression. The scopes live at the call site stay on that stack,
and `lookup_bound_term` consults every scope before the symbol table. A free name
in the body therefore resolves to whatever `let`, or enclosing definition
parameter, is in force where the definition is *called*. That is dynamic
scoping, where SMT-LIB 2.6 section 3.6.3 ("Shadowing") requires lexical scoping.

Nullary definitions are not affected: they are compiled at their declaration,
where no scope is live.

## History

Parameter scopes on the let stack date from `4f5696a` (2026-07-11, "Support
QF_UF define-fun macros"). The current form of the code in `main` is from
`bbf74c7` (the tactic-module split).

## Relevance to quantifiers

With binders, the call site's live scopes include quantified variables. A body
that mentions a global symbol spelled like a bound variable would silently refer
to the bound variable, which is capture. Quantifier parsing should therefore be
built on top of the lexical fix.
