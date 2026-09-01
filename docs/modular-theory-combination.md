# Modular theory combination

Primo's modular combination layer is based on Nelson and Oppen's arrangement
method and on SAT-mediated delayed theory combination. Its first non-convex
target is `QF_UFLIA`; the established `QF_UFLRA` solver retains its specialized
convex model-reconciliation policy.

The primary algorithmic sources are Nelson and Oppen, *Simplification by
Cooperating Decision Procedures* (TOPLAS 1979); Oppen, *Complexity, Convexity
and Combinations of Theories* (TCS 1980); Krstic and Goel, *Architecting
Solvers for SAT Modulo Theories: Nelson-Oppen with DPLL* (FroCoS 2007);
Bruttomesso et al., *Delayed Theory Combination vs. Nelson-Oppen for
Satisfiability Modulo Theories* (LPAR 2006); and de Moura and Bjorner,
*Model-based Theory Combination* (ENTCS 2008). The detailed readings and the
status of the existing convex implementation are recorded in
[](papers/nelson-oppen-closure-methods.md) and
[](papers/model-based-theory-combination.md).

The implementation checklist below uses Bruttomesso et al. Section 3 and
Theorem 2: augment the Boolean abstraction with every interface equality, give
both components the same total interface assignment, and learn a component
conflict set as a SAT clause. Theorem 2 explicitly permits stably-infinite,
non-convex components without complete equality deduction. The retained
QF_UFLRA policy uses de Moura and Bjorner Sections 4, 5.1, and 5.2 for
model-based propagation, opportunistic equality production, and arithmetic
model mutation. It does not claim the paper's complete Appendix A transition
system.

## Component contract

`theory::TheorySolver` is a solve-scoped protocol. Its immutable
`TheoryProperties` declares the component identifier, shared sorts, stable
infiniteness, convexity, and optional model/equality/care capabilities. A check
receives only literals owned by that component and an SMT-owned interface
arrangement. It returns consistency, an explained SAT clause, or a typed stop.
The component has no SAT-backend handle and cannot inspect another component's
private closure, tableau, or model.

The SMT coordinator owns purification, atom ownership, interface discovery,
equality materialization, SAT scheduling, and combined-model construction.
`sat/` continues to see clauses and literals only. `core/` continues to own all
terms and sorts.

A clause is deliberately the general inference type. For a convex theory it
can encode an explained equality propagation. For a non-convex theory it can
encode a disjunction of interface equalities without falsely claiming that one
member is individually entailed.

## Invariants and obligations

1. Purification gives every non-equality atom exactly one theory owner.
2. Interface relations join terms of one sort that occur in at least two
   components.
3. Every learned clause is entailed by the reporting component under its
   supplied assignment; candidate-model equality is not entailment.
4. A complete-arrangement policy assigns every same-sort shared pair. A convex
   policy may remain sparse only when its equality-propagation completeness
   argument applies.
5. EUF, rational LRA, and integer LIA are stably infinite on their supported
   interfaces. EUF and LRA are convex; LIA is explicitly non-convex.
6. Backtracking is owned by SAT and the coordinator. Theory-local retained
   state must be trailed or rebuilt before a different assignment is checked.
7. A combined `sat` result requires component models that agree on the complete
   shared equality arrangement.

## Initial operations and complexity

The first `QF_UFLIA` policy uses delayed combination. For `n` shared Int terms
it materializes all `n(n-1)/2` equality choices. Each equality is connected to
the arithmetic component by two non-strict inequalities and Boolean clauses
expressing equality iff both inequalities hold. A false equality therefore
lets SAT select one strict integer orientation. This quadratic baseline is
intentional: it supplies a simple completeness argument for a non-convex
component before sparse care-set or model-guided optimizations are attempted.

`QF_UFLRA` does not use this eager policy. Its existing lazy convex equality
exchange, exact fallback, model mutation, reconciliation, and model builder
remain the active policy behind the same component boundary.

## Implemented subset and limitations

The protocol and capability declarations are the first scaffold. Until the
coordinator and adapters are connected, runnable behavior remains the existing
specialized `QF_UFLRA` path and the direct single-theory solvers. Arrays,
bit-vectors, nonlinear arithmetic, finite shared sorts, quantifiers, and the
full `DPLL(T^M)` transition system are not implemented by this interface.

Protocol tests use mock convex and non-convex components to check ownership,
capability declaration, assignment delivery, and general learned clauses.
Solver-level arrangement, explanation, model, and fuzz tests are required as
the adapters become runnable.
