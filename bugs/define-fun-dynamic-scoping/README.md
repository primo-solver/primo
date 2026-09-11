# define-fun bodies are scoped dynamically, not lexically

## Status

**OPEN, found 2026-09-11, not fixed.** Wrong answer (`unsat` on a satisfiable
input) on conforming SMT-LIB. Found while planning quantifier parsing, where the
same mechanism would capture bound variables; see `docs/quantifiers.md`.

## Witness

`witness.smt2`, in QF_LIA:

```smt2
(declare-fun b () Int)
(define-fun f ((a Int)) Int (+ a b))
(assert (let ((b 5)) (not (= (f 1) (+ 1 b)))))
```

The `b` in `f`'s body is the global constant. Inside the `let`, `(+ 1 b)` is 6
and `(f 1)` is `1 + b`, so the assertion says `b /= 5`: satisfiable.

| solver | answer |
| --- | --- |
| primo `24ede53` (`build/primo`; no `src/` change up to `b529a8b`) | `unsat` |
| z3 (`contrib/local/bin/z3`) | `sat` |
| cvc5 (`contrib/local/bin/cvc5`) | `sat` |
| yices-smt2 (`contrib/local/bin/yices-smt2`) | `sat` |

## Mechanism

`TermBuilder::dispatch_instantiate_definition` (`src/smt/frontend.cpp`) binds
the parameters by pushing one more map onto `let_scopes_`, then builds the
stored body s-expression. The `let` scopes that are live at the call site stay on
that stack, and `lookup_bound_term` consults every scope before the symbol table.
A free name in the body therefore resolves to whatever `let` is in force where
the definition is *called*, which is dynamic scoping. SMT-LIB 2.6 section 3.6.3
("Shadowing") requires lexical scoping.

Not probed, but the same lookup implies two more cases: the parameters of an
enclosing definition being expanded are visible in a nested definition's body,
and a parameter name used as a free name in a callee resolves to the caller's
argument.

## History

Parameter scopes on the let stack date from `4f5696a` (2026-07-11, "Support
QF_UF define-fun macros"), which is in `main`. The current form of the code is
from `bbf74c7` (the tactic-module split).

## Relevance to quantifiers

With binders, the call site's live scopes include quantified variables. A body
that mentions a global symbol spelled like a bound variable would silently refer
to the bound variable, which is capture. A lexically scoped instantiation (the
body built only in the environment of its definition plus its parameters) fixes
both.
