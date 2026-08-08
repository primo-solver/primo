# Convex Nelson-Oppen Merge Plan

This note is the live integration checklist for merging the convex
Nelson-Oppen work with the QF_LRA equality-sharing mainline. Local `main` was
fast-forwarded to `dd58707` and merged into `convex-no-qflra-integration` with
the mainline LRA implementation, API, tests, and pure-QF_LRA evidence treated
as authoritative. The NO branch contributes the mixed fixpoint, lazy equality
registry, producer-rule statistics, and QF_UFLRA evidence.

The scope is deliberately limited to **convex QF_UFLRA**: rational linear
arithmetic combined with EUF over the purified shared Real terms. Integer
arithmetic, finite domains, Boolean equality sharing, and non-convex
arrangement enumeration are outside this milestone.

## Integrated Mainline LRA Surface

- `LraTableau::model_equal_classes()` returns model-coincident shared-variable
  classes. They are conjectures for `M-Propagate`, never entailments.
- `LraTableau::entailed_equalities()` returns `FixedEq` and `EqRow`
  consequences with blocking reasons. The merged NO branch retains a narrow
  rule tag so statistics can distinguish the two producers.
- The entailment oracle is bounds-equivalent after push/pop but may change the
  basis. Tests do not assume basis restoration.
- Mixed dispatch remains a separate routing decision. Its mainline default and
  pure-QF_LRA preservation evidence are retained unchanged.
- Mainline documentation and run-log evidence are retained. NO-only run-log
  entries are merged by stable entry ID rather than by textual conflict
  resolution.

## Convexity Contract

EUF and rational LRA are convex and stably infinite for this fragment. Thus,
once each theory reports all equalities it entails over the shared terms, the
combination layer can propagate equalities to a fixpoint. It does not need to
enumerate equality arrangements or guess every shared disequality.

This requires a strict distinction:

- model-equal values are **conjectures** and may guide a future mutation or
  assumption;
- entailed equalities are **consequences** and may be propagated with a
  theory explanation.

`model_equal_classes()` must therefore remain separate from
`entailed_equalities()` and must not be used as an unconditional propagation
source.

## Convex Integration Status

1. **Complete:** port `EntailedEquality` to the current `LraTableau` row representation.
2. **Complete:** implement and test `FixedEq` and `EqRow`, preserving shared-term filtering,
   complete `DeltaRational` values including infinitesimals, deterministic
   spanning output, and SAT reason literals.
3. **Complete:** keep the existing exact pairwise strict-bound check as a correctness oracle
   and unresolved-pair fallback while the optimized rules remain incomplete.
4. **Complete:** add an adapter from LRA/EUF producer results to the lazy equality-atom
   creation owned by `TheoryCombinationManager`.
5. **Complete:** change the mixed coordinator into an equality-sharing fixpoint:
   - query LRA entailed equalities;
   - query EUF entailed equalities;
   - lazily register the shared equality atom;
   - assert true equalities into both theories;
   - learn a guarded clause from the producer explanation;
   - repeat until no new equality is produced.
6. **Complete:** keep false generated equalities out of the LRA assignment. Native LRA
   disequality arrangements are a later, non-convex extension.
7. **Complete for fixed elimination:** a 45-file, 60-second QF_UFLRA ablation
   preserved 42/45 solved with zero wrong answers and improved PAR2 from
   687.11s to 596.81s in the exact promotion run (SLURM job 121509). Fixed
   elimination is now default-on only for mixed
   QF_UFLRA and can be disabled with
   `--no-qf-uflra-fixed-elimination`; pure QF_LRA remains unchanged. Row-bound
   propagation still requires its own mixed-path evaluation before any default
   change.
8. **Complete at the API boundary:** retain `model_equal_classes()` as a
   separate, currently unconsumed LRA API. Any future use belongs to the paper's
   shared model/assumption mechanism, not this convex entailment path.

## Post-Merge Review Status

These are ordered by correctness first, then performance.

1. **Blocker: repair or remove `--nelson-oppen-prop=off` for mixed QF_UFLRA.**
   The option currently skips mandatory equality sharing and returns `sat` for
   `x = y`, `p(x)`, `not p(y)`. An ablation may disable opportunistic or online
   propagation, but it must retain the final convex combination check. Add
   plain check-sat and model-validation regressions before keeping the option.
2. **Replace the exact fallback's per-pair LRA reconstruction.** The current
   default constructs every unresolved shared pair and builds a fresh prepared
   LRA theory for one or two strict-orientation probes per pair. Streaming pairs
   removes only the vector allocation; the substantive target is a reversible
   or incremental probe context. Job 121509 still recorded 295,539 fallback
   pairs and 274,252 extra-atom probes on 45 files.
3. **Fail loudly on a missing mixed-dispatcher reason.** Returning `0` before
   streaming a reason can turn bookkeeping loss into an empty or malformed
   IPASIR-UP reason. Assert the ownership/reason invariant and add a lazy fresh
   equality backtrack/repropagation test.
4. **Correct stale option and allocation claims.** Documentation must not claim
   that `off` preserves model reconstruction or that full pair vectors are used
   only by explicit model-equality branching.
5. **Complete for integration commit `3dee8ca`:** lint, `make check`, ASan,
   documentation, whole-tree `clang-tidy`, 10 tracked QF_UFLRA seeds, and a
   3,000-case generated mixed gate all passed. SLURM job 121874 completed four
   750-case shards with zero wrong answers, crashes, or timeouts. This validates
   the default convex profile; it does not validate the known-unsound `off`
   ablation.

## Ownership and Invariants

- `LraTableau` and EUF produce consequences and reason literals.
- `TheoryCombinationManager` owns shared-term identity and the canonical
  equality registry; the clausifier owns term-to-SAT atom materialization.
- The SMT coordinator owns the propagation fixpoint and installs guarded SAT
  clauses.
- SAT sees only literals and clauses, never terms or tableau objects.
- A reported equality must be valid in every model of the current theory
  assignment, and its reason must independently justify the equality.
- The fixpoint must terminate because the finite set of shared equality atoms
  only grows and each atom is registered once.

## Required Tests and Evidence

- `FixedEq` and `EqRow` positive and negative tableau tests;
- one-sided-bound tests proving no unsound equality is emitted;
- infinitesimal (`DeltaRational`) distinction tests;
- optimized-producer results checked against the exact entailment oracle;
- explanation tests that replay the reason literals;
- transitive EUF/LRA propagation requiring more than one fixpoint round;
- pure QF_LRA and pure QF_UF tests proving the mixed coordinator is untouched;
- deterministic QF_UFLRA seed checks, then a fresh SLURM mutation/campaign gate;
- benchmark comparison only after zero wrong answers, crashes, and incomplete
  deterministic seed rows.

The paper basis is de Moura and Bjørner, *Model-based Theory Combination*,
Section 5. The implementation is intentionally a convex QF_UFLRA subset, not
the paper's generic `DPLL(T^M)` transition system.
