# Entailment checks and fail masks for quantifier instantiation

**Status: design scope. Nothing here is implemented.**

This note scopes item 3 of the proposal in
[](papers/fair-adventurous-enumeration.md). It covers two things:

- an entailment check that skips instances the current ground assignment
  already implies;
- fail masks, which use that check to skip whole classes of tuples.

The quantified UF route it extends is described in [](quantifiers.md).

Sources:

- **Revisiting Enumerative Instantiation.** Reynolds, Barbosa and Fontaine,
  TACAS 2018, Section 4.1. The paper note is
  [](papers/enumerative-instantiation.md).
- **Fair and Adventurous Enumeration.** Janota, Barbosa, Fontaine and Reynolds,
  FMCAD 2021, Section IV. The paper note is
  [](papers/fair-adventurous-enumeration.md).
- **Congruence Closure with Free Variables.** Barbosa, Fontaine and Reynolds,
  TACAS 2017, LNCS 10206, DOI:
  <https://doi.org/10.1007/978-3-662-54580-5_13> (resolves, checked
  2026-09-13). It is cited only as the complete treatment of partially
  instantiated formulas, which this scope does not attempt. It has not been
  read for this note.

## Why

On the SMT-LIB UF division, instance normalization shows that about half of
the enumerated tuples are duplicates. The rest are new instances that may
still be redundant: the current ground assignment `E` may already imply them.

- **Corpus results.** Primo does not check entailment. After the three fixes
  described in [](quantifiers.md), most unsolved files end in timeouts, and
  normalization still loses net solves.
- **Paper results.** In the FMCAD paper, fail masks, which rest on this check,
  gave the largest single effect: 217 problems solved only with them, against
  31 only without.

## What the papers specify

- **Entailment (TACAS 2018, Section 4.1).** First, a cache of instances already
  returned. Second, a fast, incomplete check:
  - compute congruence classes over `T(E)`, writing `[t]` for a class;
  - build a per-function term index `I_f` mapping `([t1], …, [tn])` to
    `[f(t1, …, tn)]`;
  - rewrite a literal `ℓ` to a fixed point, replacing each constant `t` by
    `[t]` and each `f(t1, …, tn)` by `s` when `I_f` maps its arguments to `s`;
  - the literal is entailed when it becomes `⊤`: `t ≈ t` becomes `⊤`, and
    `t ≉ s` becomes `⊤` when `E` contains `t' ≉ s'` with `[t'] = t` and
    `[s'] = s`;
  - the paper extends this to Boolean structure "in a straightforward way".
- **Term order (TACAS 2018, Section 4.1).** A term joins the ordering only if
  `E` does not already entail it equal to a term already in it.
- **Redundancy criteria (FMCAD 2021, Section IV).** Three criteria in
  increasing cost:
  1. duplicate term vector;
  2. entailed;
  3. duplicate modulo rewriting.
- **Fail masks (FMCAD 2021, Section IV).** For a substitution redundant under
  criterion 2 or 3, drop variables greedily while the partial substitution
  stays redundant. The remaining positions form a pattern with wildcards, and
  enumeration skips tuples that match a stored pattern.

## What Primo has

- **Quantifier component.** At a complete SAT assignment, the quantifier
  component enumerates tuples over per-sort pools. The order is set by
  `--quantifier-tuple-order`. Criterion 1 is the per-obligation set of tried
  tuples plus the raw instance cache. Criterion 3 is `InstanceSimplifier` and
  its canonical cache. There is no criterion 2 and no fail masks.
- **Timing.** The dispatcher consults the quantifier component only in
  `cb_check_found_model`, after the EUF component, which precedes it in the
  online order and has processed the same assignment. So when instances are
  chosen, EUF's state is exactly `E` for the ground literals.
- **EUF state.** `UfliaEufComponent` owns an `OnlineEufPropagator`, which owns
  an `IncrementalFlatAssignmentState` holding:
  - a `FlatTermIndex`: curried applications, where Boolean formula subterms are
    opaque constants;
  - a `FlatCongruenceClosure`;
  - the flat nodes for `true` and `false`.

  Assigned equality atoms are asserted equal or distinct; other Boolean atoms
  are asserted equal to the true or false node. `FlatEufModelView` already
  packages the index and the closure. All of this is private. The propagator
  is rebuilt whenever atoms grow.
- **Closure interface.**
  - *Public:* `are_equal` (non-const, because it flushes pending merges),
    `explain_equal` and `equivalence_classes`.
  - *Private:* `representative`, the signature table `lookup_` over (function,
    argument) pairs, and the disequalities with per-node index lists.
  - *Missing:* a disequality query, and a lookup of a congruent application
    for a term that was never flattened. Instance terms such as `f(a)` often
    are not in `T(E)`.
- **Component lifetime.** Components are created once per solve by the recipe
  factories, so a pointer to the EUF component stays valid for the solve.
  Pointers into its propagator do not.

## Design

### 1. A read-only ground equality view, owned by EUF

Add a small interface in `src/theory/`, for example
`theory::GroundEqualityView`, that knows nothing about quantifiers:

- `class_of(TermId) -> optional<ClassId>`: the class of a term.
  - A term that is not flattened is looked up by congruence: take the classes
    of the function symbol and each argument, and follow the curried
    signature table.
  - If any step is missing, return nothing, meaning the term is not in `T(E)`.
- `equal(ClassId, ClassId)`.
- `distinct(ClassId, ClassId)`: some asserted disequality links the two
  classes.
- `truth(ClassId) -> optional<bool>`: the class is the true or the false node.

`UfliaEufComponent` implements it through its current propagator state. This
needs three additions to `FlatCongruenceClosure`, each with unit tests:

- a public const representative query, after a flush;
- a const signature lookup on (function representative, argument
  representative);
- a disequality query over the per-node lists, using current representatives.

`FlatTermIndex::existing_flat_term` already maps terms that were flattened.

**Validity.** The view is only consulted inside the quantifier component's
check at a complete assignment, and its answers are discarded after that check.
Any backtrack or growth invalidates them. An assignment-epoch guard makes a
stale query fail loudly.

### 2. Wiring

`solve_quantified_uf` already builds both components. The EUF factory records
the component pointer, and the quantifier factory receives it as a
`GroundEqualityView*`. Other recipes pass nothing, and the quantifier component
behaves exactly as today without a view.

### 3. Three-valued evaluation of an instance

Evaluate the canonical instance bottom-up, iteratively and memoized per check,
to true, false or unknown:

- an equality is true when its sides are in one class, false when the classes
  are distinct, and unknown otherwise;
- a Boolean application is its class's truth value, or unknown;
- `not`, `and`, `or` and `ite` use Kleene logic;
- an opaque Boolean proxy, a nested quantifier atom, or a term not in `T(E)` is
  unknown.

This is the Section 4.1 rewrite with the Boolean extension made explicit. It
is sound, because it answers true or false only when `E` entails it. It is
incomplete by design.

An instance is redundant when it evaluates to the polarity its lemma asserts:
true for a true universal, false for a false existential.

### 4. What skipping means

Entailment depends on the assignment, and a later branch may need an instance
that is entailed now. A skipped instance must therefore not become permanent
state:

- **Caches and budget.** It is recorded in neither the raw nor the canonical
  cache, and does not count against the budget.
- **Tried set.** It is removed from the obligation's tried set.
- **Retry.** It goes on a per-obligation retry list, which is replayed before
  new tuples at the next round. This keeps the staged orders' resume points
  and the walks' states unchanged.

These rules are what make it safe to insert the check after the duplicate
caches and before `enqueue`, in the paper's order of increasing cost. The
retry list keeps the enumeration fair across rounds, which Corollary 2 of
TACAS 2018 requires.

### 5. Fail masks

For a redundant substitution, apply FMCAD Section IV's greedy procedure with
the evaluator from step 3. A dropped variable stays an open bound variable,
which evaluates to unknown. That is already enough for the paper's example:
`P(a,b) ∨ Q(b,x3)` evaluates to true for any `x3` when `P(a,b)` is entailed.

- **Storage.** The result is a wildcard pattern over pool indices, stored per
  obligation.
- **Enumeration.** A tuple matching a pattern is skipped exactly like an
  entailed instance: retried later, never marked tried.
- **Lifetime.** Masks derive from `E`, so they are cleared at the next
  assignment. FMCAD does not state the lifetime; this is the conservative
  choice.
- **Normalization.** Normalization (criterion 3) could also produce masks,
  but an open instance rarely matches a closed canonical one. Masks start from
  entailment only.

### Deferred

- **Pool deduplication.** Deduplicating pools modulo `E`, as in the TACAS 2018
  term order. Pools are append-only across the solve while `E` changes, so
  this could only reorder terms, never drop them.
- **Satisfiable answers.** Deriving `sat` from "every instance over `T(E)` is
  entailed", Corollary 1 of TACAS 2018. Primo's pool is a superset of `T(E)`,
  and the check is incomplete, so this needs its own soundness argument and
  tests.
- **Free variables.** Complete handling of partially instantiated formulas,
  as in the TACAS 2017 paper.

## Plan

Each step is a separate commit with unit tests. Solver steps also go through
the usual gates: CTest, ASan and lint, and quantifier-free preservation where
shared EUF code changes.

1. Closure additions and `GroundEqualityView` in the EUF component, with unit
   tests on hand-built assignments: congruence, disequality, truth, and terms
   outside `T(E)`. Quantifier-free preservation: the added queries are
   read-only.
2. Wiring and the three-valued evaluator, with a truth-table test and the
   paper's `P(a,b) ∨ Q(b,x3)` example.
3. Entailment skipping with retry lists, behind an option that is off by
   default. Tests:
   - a solver test where entailed instances are counted and the answer is
     unchanged;
   - a retry test where an instance skipped under one assignment is produced
     under a later one.

   Counters: `:quant.instances.entailed` and `:quant.retries`.
4. Fail masks, behind their own option. Tests: the paper example, and on small
   formulas, that masked enumeration never skips a non-redundant tuple
   relative to enumeration without masks. Counters `:quant.fail-masks` and
   `:quant.tuples.masked`.
5. Evaluation on the UF division under one tuple order: off, entailment, and
   entailment with masks. Report solved counts, PAR2, counters, and
   correctness against `:status`, Z3 and cvc5. Also run a mutation-fuzz gate.
   Record everything in the run-log.

## Risks

- **Cost.** The evaluator runs on every instance that survives the duplicate
  caches. A fast path first rejects instances containing a term that is not
  in `T(E)`, which are unknown anyway; that path is measured by counters.
- **Coupling.** The view is a generic equality interface. EUF learns nothing
  about quantifiers, but the quantified route depends on EUF's internal
  representation holding at a complete assignment. The epoch guard and the
  growth rebuild are where this can break.
- **Completeness.** Skipped instances stay retryable, so the enumeration stays
  fair. A bug in retry handling would lose refutations silently. The retry
  test and the corpus comparison against the entailment-off arm are the
  guard.

## Open questions

- Should retry lists be bounded? A tuple entailed in every assignment would
  be retried forever. Candidates are a per-tuple retry cap, or retrying only
  when the obligation's pools or `E` changed.
- Should masks survive backtracking when their literals' explanations are
  still implied at a lower level? That needs explanations for the classes,
  which `explain_equal` provides; it is deferred.
- How do masks interact with the walk orders? Deepening and random walk
  revisit and skip tuples differently, so masked tuples may need
  order-specific handling.
