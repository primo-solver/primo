# Boolean Arguments in EUF

## Semantic invariant

SMT-LIB permits uninterpreted functions over arbitrary sorts, including
Boolean arguments. If `p` and `q` are Boolean terms, EUF congruence requires

```text
p = q  =>  f(p) = f(q)
```

This remains true when the Boolean equality is written explicitly, lowered to
CNF, or implied transitively by several Boolean equivalences.

For example, the following is unsatisfiable:

```text
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun f (Bool) Bool)
(assert (= p q))
(assert (not (= (f p) (f q))))
```

The same obligation can be expressed propositionally:

```text
(assert (or (not p) q))
(assert (or (not q) p))
(assert (not (= (f p) (f q))))
```

The CNF clauses are handled by SAT, but SAT alone does not know the UF
congruence rule. The EUF layer must retain the Boolean arguments and connect
their assigned truth values to the application terms.

## Current Primo architecture

The current mixed solver keeps Boolean theory atoms available to EUF. This is
deliberate: SAT assignments and CNF clauses do not currently form a complete
SAT-to-EUF equality bridge. In particular, the clauses

```text
(not a) or b, (not b) or a,
(not b) or c, (not c) or b,
(not c) or d, (not d) or c
```

force `a = b = c = d` in every satisfying SAT assignment, but they are still
propositional clauses. They are not delivered to the EUF closure as a proof
that the terms `a` and `d` are equal. Removing `b` and `c` from EUF because a
syntactic dependency analysis says they are not direct UF arguments can
therefore lose the explanation needed to derive `f(a) = f(d)`.

Consequently, the current implementation does **not** prune nullary Boolean
terms from mixed EUF. A future architecture could add an explicit, explained
SAT-to-EUF equality bridge or compile Boolean UF applications into guarded
congruence clauses, but that is outside the current implementation.

## Scope and limitations

The current implementation covers ground Boolean UF applications and their
arguments by retaining the relevant Boolean theory atoms in the mixed path. It
does not claim a general Boolean rewriting, SAT-to-EUF bridge, or
Ackermannization algorithm. Prepared-EUF reuse across SAT checks is a separate
performance optimization.

MathSAT has historically required special rewriting for UF applications with
Boolean arguments. That is an implementation compatibility issue, not an
SMT-LIB semantic restriction; the benchmark corpus may therefore underrepresent
this EUF shape.

## Verification cases

The mixed-theory seed corpus mirrors these shapes under
`benchmarks/seeds/qf_uflra/`: direct Boolean-argument predicates, compound
Boolean arguments, Boolean arguments mixed with an uninterpreted sort, and an
UNSAT case whose contradiction is purely Boolean congruence. Each seed also
contains a Real constraint so that the QF_UFLRA dispatch path is exercised.

The required regression suite includes explicit equality, CNF equality, the
transitive CNF case `a = b = c = d` with `f(a) != f(d)`, and nested Boolean
equalities. Any future pruning change must prove these cases with independent
SAT explanations and must not be accepted based only on matching model truth
values.

The design follows the many-sorted ground congruence-closure treatment of
EUF in Nieuwenhuis and Oliveras, *Fast Congruence Closure and Extensions*,
and the SAT/theory separation used by de Moura and Bjørner, *Model-Based
Theory Combination*. SMT-LIB Core defines equality for every sort, including
`Bool`.
