# Integer division, modulo, and absolute value

Design note for supporting the SMT-LIB `Ints` operators `div`, `mod`, and
`abs`, which Primo's frontend currently rejects with `unknown function symbol`
(`src/smt/frontend.cpp` dispatches only `=`, `+`, `-`, `*`, `/`, and the four
comparisons). Written before implementation, as the paper-grounded rule
requires.

## What the standard requires

The SMT-LIB `Ints` theory fixes `div` and `mod` by this constraint:

```smt2
(forall ((m Int) (n Int))
  (=> (distinct n 0)
      (let ((q (div m n)) (r (mod m n)))
        (and (= m (+ (* n q) r))
             (<= 0 r (- (abs n) 1))))))
```

This is Boute's *Euclidean* definition. The remainder is non-negative
regardless of the sign of either operand, which is the point of the `abs n` in
the upper bound.

This matters because it is not the convention most programming languages use.
For `n > 0` Euclidean agrees with floor division; for `n < 0` it does not:

| expression | Euclidean (SMT-LIB) | floor (Python) | truncated (C) |
| --- | --- | --- | --- |
| `div (-7) 3` | `-3` | `-3` | `-2` |
| `mod (-7) 3` | `2` | `2` | `-1` |
| `div 7 (-3)` | `-2` | `-3` | `-2` |
| `mod 7 (-3)` | `1` | `-2` | `1` |

Getting the negative-divisor row wrong is the likely implementation bug, so it
belongs in the tests rather than in a comment.

Division by zero is **not** undefined. SMT-LIB's Reals theory says, and the
Ints theory refers to it: "Since in SMT-LIB logic all function symbols are
interpreted as total functions, terms of the form `(/ t 0)` *are* meaningful in
every instance of Reals. However, the declaration imposes no constraints on
their value." So `(div t 0)` and `(mod t 0)` denote *some* value that the
theory leaves free; a solver may pick any, but must stay consistent with
congruence, so `(div 1 0)` and `(div 1 0)` are equal while `(div 1 0)` and
`(div 2 0)` need not be.

## What the decision-procedure literature says

Essentially nothing, and that is itself the finding: these operators are
eliminated before the arithmetic core ever sees them, so the core LIA
literature is written for a fragment that excludes them.

- **Bromberger, *Decision Procedures for Linear Arithmetic*** (Saarland
  University, 2019, HAL `tel-02427371`). The input grammar in §8.2.1 admits
  `+ - * /`, `ite`, `let`, and numerals as the only linear-arithmetic term
  formers. `div`, `mod`, and `abs` are absent by construction. Divisibility
  constraints `d | aᵀx − b` do appear, but §2.2 is explicit that they are
  "introduced only during the execution of some of our algorithms (CutSat,
  CutSat++, and CutSatg). Our actual input problems never contains divisibility
  constraints."
- **Bromberger §2.3.1, "Reducing Divisibility Constraints"**, is nonetheless
  the closest thing to a treatment, because it states the equivalence in the
  direction we need. It gives the *diophantine representation*
  `{d | aᵀx − b} ≡ ∃q ∈ Z. {d·q − aᵀx = −b}` and, for the negated form,
  `{d ∤ aᵀx − b} ≡ ∃q ∈ Z, r ∈ Q. {d·q + r − aᵀx = −b, 0 < r < d}`, noting that
  "both of these transformations describe the formal definition of dividing
  aᵀx − b by d, i.e. aᵀx − b = d·q + r, where q is the quotient of the division
  and r the remainder". It attributes this to Cooper.
- **Cooper, "Theorem proving in arithmetic without multiplication"** (*Machine
  Intelligence* 7:91–99, 1972) is where divisibility constraints and this
  representation originate, as the machinery of quantifier elimination for
  Presburger arithmetic.
- **Griggio's and King's theses cover none of it.** Every apparent hit for
  "mod" in both is the phrase "Satisfiability **Modulo** Theories"; neither
  mentions `div` at all.

Bromberger also records a caveat worth carrying: the reduction is "only mixed
equisatisfiable and not rationally equisatisfiable", because it "moves the
integer guarantee out of the evaluation semantics of divisibility constraints
and into the type condition of the new variable q". For us that is benign —
the introduced quotient is an Int-sorted variable and Primo's branch-and-bound
enforces integrality on those — but it does mean the *rational relaxation* of
a goal changes when we introduce quotients, so anything reasoning about the
relaxation must be given the transformed problem, not the original.

## What solvers do

Both mainstream solvers put this in the frontend, not the arithmetic core.

- **Z3.** Bjørner and Nachmanson, *Arithmetic Solving in Z3*: "The front-end to
  the core arithmetic solver axiomatizes integer and real division operations
  using multiplication and addition, so that the solver does not have to reason
  about division."
- **cvc5**, in `src/theory/arith/operator_elim.cpp`, introduces a single Skolem
  `v` for the quotient and constrains it with `den * v <= num` and
  `num < den * (v + 1)` when the divisor is a positive constant, and
  `num < den * (v - 1)` when it is negative. Modulus is derived, not given its
  own variable. `abs` becomes `(ite (< x 0) (- x) x)`. The partial `div`/`mod`
  reduce to total versions wrapped in `(ite (= den 0) <uninterpreted skolem>
  <total>)`, which is how the underspecified-but-total zero case is realised.

## Proposed encoding for Primo

For `(div t k)` and `(mod t k)` where `k` is a nonzero integer **literal**,
introduce one fresh Int constant `q` per distinct pair `(t, k)` and assert

```
0 <= t - k*q  and  t - k*q <= |k| - 1
```

then rewrite `(div t k)` to `q` and `(mod t k)` to the linear term `t - k*q`.

Because `k` is a literal, `k*q` is a linear monomial and `|k| - 1` is a
constant, so both assertions are ordinary Primo atoms. **This is a frontend
change only**: no new atom shape, no tableau change, no theory-solver change.

Uniform in the sign of `k`, this is exactly cvc5's pair of cases:
for `k > 0` it is `0 <= t - kq <= k - 1`, and for `k < 0` it is
`0 <= t - kq <= -k - 1`.

### On uniqueness

The question that motivated this note was whether extra axioms are needed to
pin the quotient down. They are not. Euclidean division is an
existence-*and*-uniqueness theorem: for `t` and nonzero `k` there is exactly
one pair `(q, r)` with `t = k*q + r` and `0 <= r < |k|`. The two inequalities
above therefore already determine `q` as a function of `t`, which is what makes
`div` and `mod` functions rather than relations.

Two consequences follow. Introducing a separate quotient variable for each
syntactic occurrence would still be *sound*, since any two such variables are
provably equal — sharing one per hash-consed `(t, k)` is an efficiency choice,
not a correctness requirement. And no congruence axiom is needed to relate
`(div t k)` and `(mod t k)`: expressing `mod` as `t - k*q` over the same `q`
ties them together by construction.

Note this needs only one Skolem, not the two that `t = k*(t div k) + (t mod k)`
suggests. Giving `mod` its own variable `r` plus the equation is equivalent but
adds a variable and a row per occurrence.

### Scope and deliberate limits

- **Literal divisors only.** `(div x y)` with non-constant `y` makes `y*q`
  nonlinear and is outside the linear fragment, exactly as `(* x y)` already
  is. It must keep reporting unsupported, with a message that says *nonlinear*
  rather than *unknown symbol*.
- **Zero divisor.** With literal divisors, `(div t 0)` can only arise if
  written literally. The cheap conforming option is cvc5's: one uninterpreted
  Int constant per numerator-free equivalence class, i.e. an uninterpreted
  function `div0`/`mod0` applied to `t`, which gets congruence for free from
  the EUF solver. Deferring this and reporting unsupported for a literal zero
  divisor is also defensible; what is not defensible is silently returning a
  value, since that would be a wrong answer rather than a limitation.
- **`abs`.** Rewrite `(abs t)` to `(ite (>= t 0) t (- t))`. Primo already
  handles `ite` over Int, so this needs no new machinery at all.

## What this is and is not worth

Measured, not assumed:

- `div`/`mod`/`abs` appear in **0 of 600** randomly sampled SMT-LIB QF_LIA
  benchmarks, and in **0 of the 20** QF_UFLIA seeds staged for the mutation
  gate.
- They appear in **142 of 200** typefuzz mutants of those same seeds, because
  typefuzz is a type-aware *operator mutation* fuzzer: it substitutes operators
  from the theory signature, so it synthesises them even though no seed
  contains one.

So this will not win a single benchmark. Two other things make it worth doing:

1. **Conformance.** Primo currently rejects conforming QF_LIA/QF_UFLIA input
   that every other solver answers.
2. **It unblocks the mutation gate.** YinYang's `ignore_list` contains the
   string `"unsupported"`, and `Fuzzer.py` counts such a case as
   `invalid_mutants` and skips it. Because Primo says `unsupported` on these
   three operators, roughly three quarters of every real-seed mutation run is
   discarded — the gate logs report `eff: ~25%`. Supporting the operators would
   raise the gate's effective yield roughly fourfold, which improves coverage
   of *everything else* the mutation gate tests.

The second reason is the strong one, and it is why this is worth doing before
rather than after the next round of correctness work.

## Tests this implies

- Euclidean sign convention at all four combinations of operand signs, pinned
  against the table above, especially the negative-divisor rows where floor and
  Euclidean disagree.
- `(mod t k)` and `(div t k)` sharing one quotient: assert `(= (div x 3) 2)`
  and check `(mod x 3)` is constrained accordingly.
- Repeated occurrences of the same `(div t k)` term getting equal values.
- `k` negative, `k = 1`, `k = -1`, and `t` negative.
- Non-constant divisor reported as unsupported-nonlinear, not unknown-symbol.
- `abs` over a term, and `abs` nested inside arithmetic.
- A model-validation case: a sat instance using all three operators whose
  printed model Dolmen accepts.
