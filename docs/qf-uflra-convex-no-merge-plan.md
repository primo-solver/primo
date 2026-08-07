# Convex Nelson-Oppen Merge Plan

This note records how the QF_LRA equality-sharing work from
`origin/qflra-no-equality-sharing` was selectively merged into the Nelson-Oppen
branch without merging the source branch's unrelated QF_LRA experiments and
tableau rewrites. The selective merge, convex fixpoint wiring, and mixed
fixed-elimination decision are now implemented.

The scope is deliberately limited to **convex QF_UFLRA**: rational linear
arithmetic combined with EUF over the purified shared Real terms. Integer
arithmetic, finite domains, Boolean equality sharing, and non-convex
arrangement enumeration are outside this milestone.

## Branch Findings

The branch was forked from an older point. Its full diff is therefore
misleading: many commits are already represented on the current Nelson-Oppen
branch, including the shared equality ledger, lazy equality atoms, Boolean EUF
proxy work, online IPASIR-UP support, and prepared EUF/LRA state reuse. Compare
with `git log --left-right --cherry-pick HEAD...origin/qflra-no-equality-sharing`
before selecting commits.

The genuinely relevant branch-only commits are:

- `a5307b8`: model-equality classes for conjectures used by `M-Propagate`;
- `11cc884`: LRA `FixedEq` and `EqRow` entailed equalities with reasons;
- `640ddc7`: independent entailment-oracle tests and a measured completeness
  gap;
- `b51f20c`: correction that the remaining gap is caused by the LRA basis, not merely by
   missing paper rules.

The branch-only `f3e7022` mixed-dispatch work is also separate from equality
sharing. It should be evaluated independently rather than pulled into the
convex NO merge.

The branch also contains broad QF_LRA preprocessing, tableau, benchmark, and
documentation changes, many of which overlap work already present here. It
must not be merged wholesale into the Nelson-Oppen branch.

The original `11cc884` commit was resolved selectively: its entailed equality
API and rules were retained, while model-equality partition code was excluded.

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

## Selective Merge Status

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
   685.07s to 596.13s. Fixed elimination is now default-on only for mixed
   QF_UFLRA and can be disabled with
   `--no-qf-uflra-fixed-elimination`; pure QF_LRA remains unchanged. Row-bound
   propagation still requires its own mixed-path evaluation before any default
   change.
8. **Deliberately excluded:** do not merge model-equality classes into the
   entailed producer. Any future model partition belongs to the paper's shared
   model/assumption mechanism, not this convex correctness path.

## Ownership and Invariants

- `LraTableau` and EUF produce consequences and reason literals.
- `TheoryCombinationManager` owns shared-term identity and SAT-visible equality
  atoms.
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
