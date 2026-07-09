# Preprocessing

Primo currently has two default-off QF_UF preprocessing passes and one
default-enabled unit propagation cleanup in `src/smt/preprocessing.*`:

- equality bridging under disjunctions, enabled with `--eq-bridge`;
- unit equality propagation, enabled by the default preprocessing loop;
- symmetry breaking over finite-domain choice disjunctions, enabled with
  `--uf-symmetry-breaking`.

```bash
./build/primo --eq-bridge input.smt2
./build/primo --uf-symmetry-breaking input.smt2
./build/primo --eq-bridge --preprocessing-passes 3 input.smt2
```

Both passes are deliberately separate from congruence closure. They create
additional Boolean assertions over ordinary equality terms, then the existing
Boolean clausifier and lazy EUF loop solve the strengthened formula.

Unit equality propagation runs after optional generated preprocessing facts are
appended. It collects equalities asserted at top level, including equalities
under top-level `and`, and simplifies equality atoms decided by that unit
closure. This is what turns bridge facts from diamond formulas into an immediate
pre-SAT contradiction with the final disequality.

`--preprocessing-passes N` controls the repeated preprocessing loop. Each pass
can add bridge facts and then simplify with unit equalities. The default is one
pass; `0` disables the loop.

## Equality Bridge

The equality bridge targets equality-diamond formulas from the SMT-LIB QF_UF
benchmarks, generated from the Strichman-Rozanov minimum-transitivity-constraints
workshop benchmark family.

The pass scans `or` subformulas. For every branch, it computes the equality
closure of equality atoms found directly in that branch or under nested `and`.
If every branch implies the same equality between shared terms, the pass appends
that equality as a new unit assertion.

Example:

```text
(or (and (= x y) (= y z))
    (and (= x w) (= w z)))
```

Both branches imply `x = z`, so `--eq-bridge` appends `(= x z)`.

For the diamond chain:

```text
(or (and (= x_i y_i) (= y_i x_{i+1}))
    (and (= x_i z_i) (= z_i x_{i+1})))
```

each disjunction implies `x_i = x_{i+1}`. Adding these bridge equalities lets the
ordinary EUF checker derive the final contradiction without exploring all branch
combinations.

The implementation is intentionally conservative:

- it looks through `or` and `and`, but not through negation, implication, or
  predicates;
- it only adds equalities implied by every branch;
- it caps branch count and pair checks per `or` node;
- generated bridge equalities are ordinary `TermId` equality assertions.

## UF Symmetry Breaking

The recognizer looks for finite-domain choice formulas:

```text
(or (= t c0) (= t c1) ... (= t cn))
```

where `c0 ... cn` are constants and every pair of values is asserted distinct at
top level. In SMT-LIB this usually appears as assertions such as:

```text
(assert (not (= c0 c1)))
(assert (not (= c0 c2)))
(assert (not (= c1 c2)))
(assert (or (= x c0) (= x c1) (= x c2)))
```

Terms with the same value set form a candidate symmetry group. A group is
accepted only if:

- all values are constants of an uninterpreted sort, not Bool-valued
  propositions;
- the accumulated assertion multiset is syntactically invariant under the value
  cycle and under swapping the first two values;
- candidate ordered terms do not themselves contain the permuted values.

The moving-term guard matters. A table such as `(f c0), (f c1), ...` moves when
`c0, c1, ...` are permuted, so it is not a fixed term sequence and cannot receive
these ordering clauses safely. If a mixed group contains both moving and fixed
terms, only the fixed terms remain.

For an accepted group, terms and values are ordered by stable `TermId`. The pass
adds value-precedence assertions:

- term `t_i` may use only values `c_0 ... c_i`;
- if `t_i = c_j` for `j > 0`, some earlier term `t_k` must use `c_{j-1}`.

These assertions remove value-renaming representatives from the search space.
They do not replace EUF reasoning and they do not create a finite model finder by
themselves.

## Implementation Notes

The preprocessing passes operate on hash-consed `core::TermId` values after
SMT-LIB parsing.

Equality bridging builds a small local union-find per disjunction branch. It
intersects the terms that occur in equality atoms in every branch, then checks
pairs of shared terms for equivalence in every branch closure.

Unit equality propagation builds one local union-find from top-level equality
facts and rewrites equality atoms whose endpoints are already in the same class
to `true`. Negated equalities then simplify through `mk_not`, and enclosing
Boolean structure simplifies through the existing `TermManager` constructors.
It does not inspect equalities hidden under `or`, implication, negation, or
predicate applications.

UF symmetry breaking traverses top-level conjunctions to collect explicit
disequalities, then scans `or` nodes for choice disjunctions. The invariance
check builds canonical string keys for formulas after applying a value
permutation. Equality, `and`, and `or` children are sorted in the key because
those operators are commutative for this syntactic check.

Generated assertions are ordinary `TermId` formulas using `mk_not`,
`mk_implies`, `mk_or`, and `mk_eq`. They are appended before QF_UF solving, so
they reuse the same SAT abstraction and congruence-closure code as user
assertions.

## Current Status

Implemented:

- equality bridging under disjunctions;
- unit equality propagation from top-level equalities;
- configurable repeated preprocessing loop with `--preprocessing-passes`;
- finite-domain choice recognition from explicit disjunctions;
- top-level pairwise disequality requirement;
- syntactic constant-permutation invariance check;
- moving-term rejection;
- value-precedence assertion generation;
- CLI flag `--eq-bridge`;
- CLI flag `--uf-symmetry-breaking`;
- unit tests for equality bridging and for accepted, asymmetric, and moving-term
  symmetry-breaking cases.

Deferred:

- finite-domain at-most-one clauses and equality definitions from llm2smt;
- proof support for generated preprocessing lemmas;
- stats printing for accepted/rejected groups;
- SyMT-style graph automorphism detection;
- benchmark-driven default-on decision.

## Paper Traceability

This pass is a clean Primo implementation of the idea that was successful in
LLM2SMT `lra`; no LLM2SMT code was copied.

- Ofer Strichman and Mirron Rozanov, *Generating minimum transitivity
  constraints in P-time for deciding Equality Logic*, SMT Workshop 2005, is the
  source named by the SMT-LIB equality-diamond benchmarks. Primo does not
  implement a full minimum-transitivity-constraint generator; `--eq-bridge` is a
  narrow disjunction-consequence pass for that benchmark shape.
- Déharbe, Fontaine, Merz, and Woltzenlogel Paleo, *Exploiting symmetry in SMT
  problems*, is the source for SMT symmetry breaking by invariance under
  permutations of uninterpreted constants.
- Reger and Suda, *Using SAT and SMT Solvers for Finite Model Finding with
  Sorts*, is the source for value-ordering/value-precedence constraints in
  finite-model encodings.

See [](papers/smt-symmetry-breaking.md) and
[](papers/finite-model-finding-sorts.md).
