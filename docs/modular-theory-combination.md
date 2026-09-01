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
remain in its specialized coordinator.

## Recorded model-guided direction

The intended optimization path is de Moura and Bjorner's model-based theory
combination, not convex propagation applied to integers. A component may expose
a candidate partition of its shared terms. The coordinator may use that
partition to choose equalities, test compatibility with other component models,
or ask a component to mutate its model before committing to a split. A
candidate-model equality remains a search choice; only an explained theory
consequence may be learned as an entailed clause.

The generic protocol already records whether a component produces a model
projection, and `TheoryCheckResult` has an equality-class projection. It does
not yet define generic `M-Propagate`, `M-Mutate`, model generations, or
trail-synchronized model invalidation. Those operations and their ownership
checklist are recorded in [](papers/model-based-theory-combination.md).

For QF_UFLIA, model guidance may eventually order or reduce the practical cost
of arrangement search, but it cannot rely on the convex rule that an implied
disjunction contains an individually implied equality. The complete
SAT-visible arrangement therefore remains the current correctness backstop.

## Implemented subset and limitations

`QF_UFLIA` is runnable through a sequential two-component coordinator. Its EUF
adapter declares a convex, stably-infinite Int interface; its LIA adapter
declares the same interface stably infinite and non-convex. Both receive the
same complete `InterfaceRelation` vector, but enforce it only through their
owned SAT atoms. The LIA adapter reuses Primo's exact-rational tableau, integer
patching, GCD check, Gomory cuts, and bounded branch-and-bound. Reaching the
existing integer node limit returns a typed resource-limit `unknown`.

The frontend accepts Int-valued uninterpreted functions and predicates, rejects
Real-valued symbols and Real arguments, and retains the existing QF_LIA
operator subset: integer `div` and `mod` remain unsupported. Mixed purification
uses memoized Int proxies in both directions. Models combine the final integer
tableau values with the ground EUF function and predicate tables, then replay
the ordered preprocessing reconstruction chain.

The coordinator is sequential and currently instantiates EUF plus LIA only.
`QF_UFLRA` retains its established specialized convex coordinator, options,
counters, and model behavior. Its EUF and arithmetic implementations supply the
same operations represented by the component protocol, but the specialized
scheduler is deliberately not routed through the new adapter in this baseline;
doing so later requires a separate equivalence evaluation. Arrays, bit-vectors,
nonlinear arithmetic, finite shared sorts, quantifiers, and the full
`DPLL(T^M)` transition system remain unsupported.

Protocol tests use mock convex and non-convex components. QF_UFLIA solver tests
cover purification, complete arrangements, the non-convex disjunction case,
combined models, empty formulas, and frontend sort rejection. Smoke models are
validated directly by Dolmen. On implementation commit `4b0ad55`, a local
3,000-case generated QF_UFLIA campaign agreed with both Z3 and cvc5, with no
timeouts, unsupported results, crashes, or frontend errors; Dolmen directly
accepted all 1,686 satisfiable models. The tactic-composition matrix passed all
174 runs across its five logics and eleven applicable profiles, including 28
QF_UFLIA runs and the sequential Boolean-splitting profiles, without
non-timing-statistic drift. A separate 1,000-case QF_UFLRA preservation run
agreed with Z3 and Dolmen accepted all 597 satisfiable models. These are local
correctness campaigns, not performance or SLURM evidence; their exact commands
and temporary artifact paths are recorded in [](run-log.md).

## Statistics

`:solver.qf-uflia` selects the new leaf. `:combination.components`,
`:combination.shared-terms`, `:combination.arrangement-pairs`, and
`:combination.arrangement-bridge-clauses` describe the static combination.
`:combination.rounds`, `:combination.component-checks`,
`:combination.learned-clauses`, and `:combination.resource-limits` describe the
sequential search. Existing QF_UFLRA statistic names and meanings are unchanged.
