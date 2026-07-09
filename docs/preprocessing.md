# Preprocessing

Primo currently has one QF_UF preprocessing pass: default-off symmetry breaking
over finite-domain choice disjunctions. The pass is implemented in
`src/smt/preprocessing.*` and is enabled from the CLI with:

```bash
./build/primo --uf-symmetry-breaking input.smt2
```

The pass is deliberately separate from congruence closure. It creates additional
Boolean assertions over ordinary equality terms, then the existing Boolean
clausifier and lazy EUF loop solve the strengthened formula.

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

The pass operates on hash-consed `core::TermId` values after SMT-LIB parsing.
It traverses top-level conjunctions to collect explicit disequalities, then scans
`or` nodes for choice disjunctions. The invariance check builds canonical string
keys for formulas after applying a value permutation. Equality, `and`, and `or`
children are sorted in the key because those operators are commutative for this
syntactic check.

Generated assertions are ordinary `TermId` formulas using `mk_not`,
`mk_implies`, `mk_or`, and `mk_eq`. They are appended before QF_UF solving, so
they reuse the same SAT abstraction and congruence-closure code as user
assertions.

## Current Status

Implemented:

- finite-domain choice recognition from explicit disjunctions;
- top-level pairwise disequality requirement;
- syntactic constant-permutation invariance check;
- moving-term rejection;
- value-precedence assertion generation;
- CLI flag `--uf-symmetry-breaking`;
- unit tests for accepted, asymmetric, and moving-term cases.

Deferred:

- finite-domain at-most-one clauses and equality definitions from llm2smt;
- proof support for generated preprocessing lemmas;
- stats printing for accepted/rejected groups;
- SyMT-style graph automorphism detection;
- benchmark-driven default-on decision.

## Paper Traceability

This pass is a clean Primo implementation of the idea that was successful in
LLM2SMT `lra`; no LLM2SMT code was copied.

- Déharbe, Fontaine, Merz, and Woltzenlogel Paleo, *Exploiting symmetry in SMT
  problems*, is the source for SMT symmetry breaking by invariance under
  permutations of uninterpreted constants.
- Reger and Suda, *Using SAT and SMT Solvers for Finite Model Finding with
  Sorts*, is the source for value-ordering/value-precedence constraints in
  finite-model encodings.

See [](papers/smt-symmetry-breaking.md) and
[](papers/finite-model-finding-sorts.md).
