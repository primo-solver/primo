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
8. **Complete as an exact-candidate filter:** retain `model_equal_classes()` as
   a separate LRA model API and use cross-value inequality only to prune exact
   entailment candidates. Equal-value pairs still require both strict probes;
   sharing them directly belongs to the paper's shared model/assumption
   mechanism and remains unimplemented.

## Post-Merge Review Status

These are ordered by correctness first, then performance.

1. **Complete in `c28531b`: repair `--nelson-oppen-prop=off`.**
   The internal mode is now named `exact`, and the old `off` spelling is a
   compatibility alias. Both retain mandatory EUF spanning equalities and the
   exact LRA fallback while disabling only the optional LRA `FixedEq`/`EqRow`
   producer. Plain check-sat and model-request regressions cover `x = y`,
   `p(x)`, `not p(y)` and the paired-bound form of the same disagreement. The
   exact profile passed all 10 tracked seeds and 3,000 generated mixed cases on
   SLURM job 121878.
2. **Complete in `6ffc19b`: replace the exact fallback's repeated LRA
   preparation.** Each unresolved pair now gets one check-local copy of the
   prepared base tableau and one difference row. The current bounds are replayed
   once, then one or two strict orientations use reversible checkpoints. Job
   121882 passed 3,000 generated exact-mode cases, and job 121886 preserved the
   45-file result matrix while reducing default PAR2 from 596.81 to 503.99. This
   change removes normalization and duplicate orientation setup, but
   intentionally does not solve the quadratic pair count or per-pair tableau
   copy/replay.
3. **Complete in `9c7e11f`: filter exact probes by the checked LRA model.**
   Cross-value pairs cannot be entailed and are removed before exact probing;
   model-equal pairs still require both strict conflicts. Job 121888 passed the
   tracked seeds and 3,000 generated exact-mode cases. Job 121892 preserved the
   45-file result matrix and reduced default PAR2 from 503.99 to 416.44 and
   exact fallback pairs from 295,539 to 173,801. Full 1,284-file jobs 121894 and
   121902 then found zero wrong answers or solved-solved Z3 disagreements:
   default solved 1,123 with 160 timeouts, while candidate-final solved 1,153
   with 130 timeouts. Both profiles reported the same one unsupported row.
   Subsequent investigation showed that row is valid QF_UFLRA: finite-domain
   AMO clauses were appended after Real-equality lowering. The full-corpus
   result is therefore marked for revalidation, not accepted correctness
   evidence. Candidate-final's exact current option set subsequently
   passed 10 tracked seeds and 3,000 generated mixed cases against Z3 in job
   121910 with no disagreement, timeout, crash, or unsupported case, but that
   generator had no finite-Real-domain scenario or activation requirement.
4. **Fail loudly on a missing mixed-dispatcher reason.** Returning `0` before
   streaming a reason can turn bookkeeping loss into an empty or malformed
   IPASIR-UP reason. Assert the ownership/reason invariant and add a lazy fresh
   equality backtrack/repropagation test.
5. **Complete on the integration branch: correct stale option and allocation
   claims.** The architecture and paper notes describe `off` as the legacy
   alias for exact mandatory closure and record the exact fallback and final
   arrangement as additional exact-candidate consumers.
6. **Complete for integration commit `3dee8ca`:** lint, `make check`, ASan,
   documentation, whole-tree `clang-tidy`, 10 tracked QF_UFLRA seeds, and a
   3,000-case generated mixed gate all passed. SLURM job 121874 completed four
   750-case shards with zero wrong answers, crashes, or timeouts. That run
   exercised the default convex profile but did not cover the later-discovered
   finite-domain/Real-lowering interaction; it excluded the then-unsound `off`
   ablation, whose repaired exact profile has separate historical evidence in
   item 1.

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

## Return Checkpoint After Frontend DAG Work

Commit `4e19d1b` promotes the convex shared-model reconciliation profile after
the exact-default fuzz and full-corpus gates recorded in `docs/run-log.json`.
The frontend Boolean-DAG memory defect discovered on
`cpachecker-induction.cs_fib_true-unreach-call.i.smt2` is independent of that
combination mechanism. Boolean-DAG work should therefore proceed on a separate
branch and must not be interpreted as completing any item below.

The next QF_UFLRA work, in priority order, is:

1. Retain EUF component state across SAT rounds, synchronized with the SAT
   trail, instead of copying the prepared closure for each round.
2. Generalize the retained LRA epoch from its current one-entry augmented
   tableau reuse to the sequence of model-guided checks, without disturbing
   pure QF_LRA behavior.
3. Reduce the remaining SAT re-entry and equality-recheck churn in split-frontier
   reconciliation. The promoted path avoids most exact probes but still makes
   more SAT decisions and solve calls than the previous exact default.
4. Re-evaluate mixed row-bound propagation. All four Section 5.1 candidate
   rules are wired, but row propagation remains default-off because its prior
   full-corpus A/B traded one solved instance for one loss.
5. Preserve the exact entailment oracle as a fail-safe until the retained
   component states and candidate coverage have fresh correctness evidence.

Generic mutable theory-model ownership, theory-independent `DPLL(T^M)`
transitions, finite shared sorts, and non-convex arrangement enumeration remain
explicitly tabled. They are not prerequisites for finishing the convex
EUF+rational-LRA path above.
