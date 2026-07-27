# QF_UF disequality propagation heuristics

## Goal

This branch is for designing and evaluating an additional EUF disequality
propagation heuristic on top of Primo's already-implemented
Nieuwenhuis-Oliveras-style incremental congruence-closure core. The motivating
unary rule is:

```text
f(a) != f(b)  =>  a != b
```

This is a theory propagation rule, not a preprocessing rewrite. It should be
implemented only when Primo can emit a sound SAT-visible reason clause and when
measurement shows the propagated disequalities reduce search enough to justify
their callback cost.

## Literature basis

Primo already implements the relevant Nieuwenhuis-Oliveras Section-3-shaped
incremental congruence-closure machinery: flat application definitions, use
lists, signature lookup, pending congruence merges, disequality conflict checks,
incremental rollback, and explanation clauses. That implementation is documented
in [](euf.md), [](papers/fast-congruence-closure.md), and
[](papers/fast-congruence-closure-implementation.md). The present branch must
not re-document that work as unimplemented or aspirational.

The new heuristic's closest explicit literature connection is Ackermann-style
functional consistency for uninterpreted functions. Ackermann reduction removes
UF applications by replacing them with fresh variables and adding constraints
that force equal results when the arguments are equal. For unary applications,
the functional-consistency implication is:

```text
a = b  =>  f(a) = f(b)
```

The motivating disequality propagation rule is the contrapositive of that
functional-consistency constraint:

```text
f(a) != f(b)  =>  a != b
```

For n-ary functions encoded by Primo's curried flat term representation, the
direct contrapositive is weaker:

```text
f(a1, ..., an) != f(b1, ..., bn)  =>  a1 != b1 or ... or an != bn
```

It does not imply every argument pair is disequal. In the curried internal
representation, a single flat application disequality can propagate a child
disequality only under the appropriate guard:

```text
(apply p x) != (apply q y) and p = q  =>  x != y
(apply p x) != (apply q y) and x = y  =>  p != q
```

This work should stay aligned with:

- Robert Nieuwenhuis and Albert Oliveras, *Fast Congruence Closure and
  Extensions*, Information and Computation 205(4), 2007. This is already the
  basis of Primo's implemented incremental flat congruence closure. Use the
  existing design notes in [](papers/fast-congruence-closure.md) and
  [](papers/fast-congruence-closure-implementation.md) for the implemented
  subset and its limitations.
- Robert Nieuwenhuis, Albert Oliveras, and Cesare Tinelli, *DPLL(T): Fast
  Decision Procedures*, for the DPLL(T) framing of theory propagation and
  theory lemmas.
- Ackermann-style and reduced-functional-consistency work on eliminating
  uninterpreted functions. These papers are the right place to cite when this
  branch talks about app-disequality-to-argument-disequality consequences as
  contraposed functional-consistency constraints, especially for bounded
  SAT-level clause-generation experiments:
  - W. Ackermann, *Solvable Cases of the Decision Problem*, 1954.
  - Ofer Strichman, Sanjit A. Seshia, and Randal E. Bryant, *Deciding
    Separation Formulas with SAT*, CAV 2002, for early SAT-based functional
    consistency encodings in verification settings.
  - Amir Pnueli and Ofer Strichman, *Reduced Functional Consistency of
    Uninterpreted Functions*, PDPAR 2005 / ENTCS 144(2), 2006, for reducing
    Ackermann functional-consistency constraints.
  - Roberto Bruttomesso, Alessandro Cimatti, Anders Franzen, Alberto Griggio,
    Alessandro Santuari, and Roberto Sebastiani, *To Ackermann-ize or not to
    Ackermann-ize? On Efficiently Handling Uninterpreted Function Symbols in
    SMT(EUF ∪ T)*, LPAR 2006, for the tradeoff between eager Ackermannization
    and lazy DPLL(T)-style EUF solving.
  - Randal E. Bryant, Steven German, and Miroslav N. Velev, *Exploiting
    Positive Equality in a Logic of Equality with Uninterpreted Functions*,
    CAV 1999, as related background on polarity-sensitive EUF encodings. This
    is not a direct disequality-propagation algorithm, but it is relevant when
    deciding whether to spend effort on positive equalities, negative equalities,
    or SAT-level reductions.

Do not copy implementation code from other solvers. If a future heuristic uses
an algorithm beyond these basic congruence consequences, first add a local
paper checklist with data structures, invariants, operations, limitations, and
tests.

## Existing Primo implementation

The branch starts from the current implementation, not from a blank EUF design:

- `FlatTermIndex` already converts nested UF applications into deterministic
  binary flat definitions.
- `FlatCongruenceClosure` already maintains representatives, class members, use
  lists, signature lookup, pending congruence merges, disequality indexes,
  explanation edges, rollback trails, and changed-term drains.
- `PreparedFlatTheory` and `OnlineEufPropagator` already connect that closure to
  final conflict checks, conflict callbacks, model-time propagation, and online
  propagation.
- Existing docs already describe the implemented subset and evidence; this page
  should only describe the new disequality-propagation design space and its
  evaluation criteria.

## Current Primo hooks

The relevant implementation pieces are:

- `src/theory/equality/flat_terms.*`: flattens applications into binary
  `FlatDefinition{function, argument, result}` nodes.
- `src/theory/equality/flat_congruence_closure.*`: owns representatives,
  definitions, use lists, disequality lists, explanations, changed-term drains,
  and rollback.
- `src/smt/check_sat.cpp`: owns `OnlineEufPropagator`, assignment
  notifications, candidate queues, theory propagation delivery, and reason
  validation.
- `docs/euf.md`: describes the public `--euf-prop` modes, existing online
  propagation budgets, candidate indexing, and known limitations.

Existing online propagation tries to prove or refute existing theory atoms after
representatives change. The disequality-propagation experiment should avoid a
new full scan over all disequalities on every callback. Prefer an index keyed by
the flat child terms/classes that changed, and rely on reason construction as
the final soundness gate.

## Candidate heuristic shapes

### H0: measurement-only baseline

Before implementation, measure current `main` on a focused NEQ-heavy manifest
with:

```text
default
--euf-prop=off
--euf-prop=conflict
--euf-prop=online
```

Record solved counts, wrong answers, timeouts, PAR2, SAT conflicts, EUF
assignments, EUF propagation candidates, delivered propagations, and reason
literal counts. This decides whether the new work should target online
propagation, model-time propagation, or a SAT-level preprocessing clause
experiment.

This branch first implements an opt-in diagnostic-only upper-bound pass enabled
by `--euf-diseq-prop-diagnostics`. It does not learn clauses or propagate
literals. At each completed SAT assignment, the pass counts assigned app-result
disequalities and checks whether the equality atoms needed by the proposed H1/H2
rules already exist in the SAT abstraction. Because final theory checks see a
complete SAT assignment, the diagnostic splits existing argument-equality atoms
by their current assigned value instead of claiming to observe "unassigned"
targets directly. These counters are still the right upper bound for whether
online propagation could have had a SAT-visible target earlier in the search.

The diagnostic counters are:

- `:euf.diseq-prop.assigned-app-disequalities`;
- `:euf.diseq-prop.source-unary-candidates`;
- `:euf.diseq-prop.source-unary-existing-arg-atom`;
- `:euf.diseq-prop.source-unary-arg-atom-true`;
- `:euf.diseq-prop.source-unary-arg-atom-false`;
- `:euf.diseq-prop.flat-same-function-candidates`;
- `:euf.diseq-prop.flat-same-function-existing-arg-atom`;
- `:euf.diseq-prop.flat-same-function-arg-atom-true`;
- `:euf.diseq-prop.flat-same-function-arg-atom-false`;
- `:euf.diseq-prop.guarded-function-guard-candidates`;
- `:euf.diseq-prop.guarded-argument-guard-candidates`.

The source-unary counters answer the narrow H1 question for syntactically unary
SMT-LIB applications. The flat-same-function counters answer the same question
after Primo's curried flattening, where a binary or larger application can expose
a same-prefix child disequality opportunity. The guarded counters require a
currently true sibling equality and a SAT-visible target equality atom; they are
diagnostic only and do not yet construct guard reasons.

Use `scripts/summarize_qfuf_diseq_diagnostics.py` on CSV files produced by
`scripts/bench_qf_uf_full.py --solver-arg=--stats
--solver-arg=--euf-diseq-prop-diagnostics` to aggregate these counters by total,
active file count, family, and top contributing files. A local smoke run at
commit `2960394` checked the diagnostic pipeline on the in-repo QF_UF seeds,
smoke tests, and regression case. It found opportunity counters only in
`benchmarks/regression/qf_uf/online_reason_cache.smt2`, so it is harness evidence
rather than evidence for accepting or rejecting H1 on the weak benchmark
manifests.

### H1: existing-atom unary propagation

Only propagate when all of these hold:

- there is an assigned disequality literal for two flat application results;
- both applications have the same current function representative, or the same
  original unary function symbol in the uncurried frontend term;
- the candidate argument equality atom already exists as a SAT theory atom;
- the candidate argument equality is unassigned;
- a reason clause can be produced and passes
  `reason_is_valid_for_current_assignment`.

For a theory atom `E_fx = (f(a) = f(b))` assigned false and candidate
`E_ab = (a = b)`, the propagated literal is `not E_ab`, with the theory lemma:

```text
E_fx or not E_ab
```

This is exactly the binary clause form of the contraposed unary functional
consistency constraint, restricted to equality atoms that already exist in the
SAT abstraction.

In CaDiCaL reason-clause form, the propagated literal appears first, followed by
the negated antecedent literals as required by the existing external propagation
adapter.

### H2: guarded curried-child propagation

For flat applications `(apply p x)` and `(apply q y)`, allow child propagation
only when the sibling side is known equal:

```text
(apply p x) != (apply q y), p = q  =>  x != y
(apply p x) != (apply q y), x = y  =>  p != q
```

This can be useful for curried multi-argument UF terms, but it is also the first
place reason size can grow. Keep it default-off until it has pure-class and
mixed-suite evidence.

### H3: clause-generation experiment

As a separate, default-off experiment, emit bounded binary or ternary clauses
for small sets of existing equality atoms:

```text
E_fx or not E_ab
E_app or not E_func or not E_arg
```

This is the Ackermannization-facing variant of the experiment: it moves selected
functional-consistency consequences from the online propagator to SAT
preprocessing. It must be bounded by predicted clause count and compared
against online delivery because QF_UF instances with many repeated applications
may produce too many clauses.

## Required tests

Add focused tests before accepting any implementation:

- Unary soundness: `f(a) != f(b)` propagates `a != b` only when the argument
  equality atom exists.
- Guarded curried soundness: `(apply p x) != (apply q y)` does not propagate
  `x != y` unless `p = q` is assigned, and does not propagate `p != q` unless
  `x = y` is assigned.
- Negative test: `f(a, b) != f(c, d)` must not imply both `a != c` and
  `b != d`; only the disjunction is valid.
- Backtracking: a propagated argument disequality disappears after the
  application disequality or sibling equality assignment is popped.
- Reason validation: every delivered propagation clause contains exactly the
  propagated literal and only currently false antecedent literals.

## Evaluation plan

1. Establish a current-branch baseline on the focused NEQ manifests and the
   mixed QF_UF manifests using existing SLURM option-comparison scripts.
2. Run the diagnostic-only upper-bound pass on the focused weak manifests and
   full-suite fast-reference/slow-Primo gaps. Reject or narrow H1 if the
   existing-target counts are too small.
3. Implement H1 behind an explicit option. Add stats for candidates inspected,
   candidates rejected by missing existing atom, candidates rejected by missing
   guard, delivered propagations, and reason literal totals.
4. Run unit tests, `make lint`, `make check`, and a local smoke fuzz gate.
5. Compare H1 against the baseline on the focused NEQ manifest. If wrong
   answers or crashes appear, stop and fix correctness before performance work.
6. If focused NEQ improves, evaluate on mixed QF_UF manifests. Keep the option
   default-off unless mixed-suite PAR2 is neutral or better and the fuzz gate is
   clean.
7. Only then evaluate H2/H3. Record rejected experiments in `docs/run-log.json`
   and revert source changes that are not accepted.

## Initial acceptance bar

A heuristic is not merge-ready unless:

- it has unit tests for the soundness cases above;
- it exposes enough stats to explain hit rate and reason cost;
- focused NEQ evidence shows a real win;
- mixed QF_UF evidence is neutral or better, or the feature remains explicit
  default-off;
- a fresh QF_UF fuzz gate is recorded in `docs/run-log.json`;
- docs describe implemented subset, option status, limitations, and evidence.
