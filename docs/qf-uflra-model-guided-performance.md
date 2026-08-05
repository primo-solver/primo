# QF_UFLRA Model-Guided Performance Plan

Status stamp: 2026-08-05, branch `nelson-oppen-online-lazy-ipasir-up`.

This note records the next performance plan for the mixed QF_UFLRA solver after
the EUF-forbidden final-arrangement pruning change. It is an implementation
checklist, not a claim that Primo already implements full model-based theory
combination.

The clean-room sources for the intended direction are:

- Leonardo de Moura and Nikolaj Bjorner, "Model-based Theory Combination",
  Electronic Notes in Theoretical Computer Science 198(2), 2008, pp. 37-49;
- the local audit in `docs/papers/model-based-theory-combination.md`;
- the current Nelson-Oppen design note in
  `docs/papers/nelson-oppen-closure-methods.md`;
- current benchmark evidence recorded in `docs/run-log.json`.

## Current Evidence

The 2026-08-05 SLURM sample at commit `8b3bb4e`, recorded in
`docs/run-log.json`, shows that EUF-forbidden final-arrangement pruning was the
right first repair:

- default mode improved from 26/45 solved to 33/45 solved;
- opt-in `--qf-uflra-candidate-final-scan` improved from 28/45 solved to 38/45
  solved;
- default model-separation candidates fell from 134245 to 60;
- candidate-mode model-separation candidates fell from 204846 to 62;
- the run had no wrong answers.

That improvement removes the broad arrangement-separation explosion as the main
remaining explanation for the Z3 gap. Z3 still solves all 45 files with PAR2
7.525657, while candidate mode solves 38/45 with PAR2 903.689026.

The follow-up Z3 `-st` inspection at commit `931598f`, recorded as
`current-slurm-derived` evidence in `docs/run-log.json`, points at equality
traffic and LRA extra-atom probing:

- `pb_real_30_60_45_31.smt2`: Z3 solves in 0.023797s with 40 conflicts, 72
  decisions, and one final check. Primo candidate mode times out after 100144
  LRA extra-atom probes, 6807 LRA-to-EUF equalities, 422 equality rechecks, and
  4069 forced SAT equality clauses.
- `pb_real_40_80_60_36.smt2`: Z3 solves in 0.026763s with 60 conflicts, 122
  decisions, two final checks, and one interface equality. Primo candidate mode
  times out after 60718 LRA extra-atom probes, 2801 LRA-to-EUF equalities, 128
  equality rechecks, and 1165 forced SAT equality clauses.
- CPAchecker `email_spec3_product29`: Z3 solves in 0.046272s with one final
  check. Primo candidate mode solves in 11.625156s even though it uses only two
  SAT solve calls; the time is dominated by 17554 LRA extra-atom probes.

The important conclusion is that the next gap is not primarily SAT branching.
The slow path is the mixed-theory equality-mining loop: Primo is still probing
and forcing too many LRA-derived equalities through SAT instead of using a
model-guided relevance/mutation decision.

## Paper Mechanism Still Missing

The de Moura-Bjorner paper is relevant because it explains how a theory model
should guide equality sharing. The current QF_UFLRA implementation has only
pieces of that design.

Missing performance-relevant pieces for QF_UFLRA:

- **Cheap Simplex equality production.** Section 5.1 describes opportunities
  such as fixed variables and row-derived equalities. Primo still finds many
  equalities by calling a temporary strict-bound probe for selected pairs.
- **Model mutation or avoidability in the default path.** Section 5.2 explains
  that a model-equal pair may be avoidable by mutating the arithmetic model.
  Primo has a conservative strict-separation mutation filter only in the
  default-off model-equality branch mode. The default final path still treats
  many LRA-discovered equalities as facts to force through SAT.
- **A shared model state.** Appendix A presents a `DPLL(T^M)` state carrying
  model information. Primo's mixed QF_UFLRA coordinator is still a final-check
  loop in `src/smt/check_sat.cpp`; it repeatedly rebuilds theory views and
  re-enters SAT after guarded equality clauses.
- **Theory-consumer relevance.** The paper's model-guided direction is not
  "enumerate all equal shared Real pairs". The producing theory should expose
  candidates, and the combination layer should propagate only equalities that a
  consuming theory cannot absorb, mutate away, or ignore.

The current code therefore must not be described as implementing the full
model-based theory-combination algorithm. The intended implementation is a
QF_UFLRA subset that moves the expensive final equality scan toward the paper's
candidate-first discipline.

## Immediate Implementation Target

The next accepted source change should make LRA-to-EUF equality handling
consumer-aware before it creates or forces SAT equality literals.

Concrete rule:

1. When LRA reports or proves a shared Real equality `a = b`, first query the
   current EUF closure under the current SAT assignment.
2. If EUF already has `a` and `b` equal, do not create a new equality atom and
   do not add a guarded SAT equality clause.
3. If EUF can merge `a` and `b` without conflicting with asserted disequalities
   or an assigned-false generated interface equality, treat the equality as
   internally absorbed for the current final/model validation step. Do not force
   it through SAT.
4. If EUF forbids `a = b`, keep the existing guarded equality-clause path. This
   is the case where the equality is genuinely useful: it rejects the current
   SAT arrangement.
5. If the equality atom is already assigned false, EUF must see that as a
   disequality. The pair is then EUF-forbidden and must not be skipped.

Soundness obligation:

- Skipping a mergeable LRA equality is allowed only when final validation and
  model reconstruction still account for the equality internally.
- A skipped equality must not allow a model where LRA gives the same Real value
  to `a` and `b` but EUF prints incompatible interpretations for UF applications
  that depend on separating those Real arguments.
- Any explicit or generated disequality already present in the SAT assignment
  must turn the pair into the forced/conflict path, not the skipped path.

Likely C++ ownership:

- `src/smt/check_sat.cpp` remains the first owner because the current
  QF_UFLRA coordinator already owns the SAT assignment, EUF flat closure, LRA
  final checks, and model reconstruction.
- `TheoryCombinationManager` in `src/smt/theory_combination.hpp` remains the
  registry for interface equality atoms and generated shared-Real equality
  variables. It should not be taught theory semantics.
- EUF mergeability should be queried through a helper like the existing
  final-arrangement EUF-forbidden check, using a temporary push/pop closure
  state. The helper must include current true equalities and false generated
  interface equalities.
- LRA remains the producer of entailed equalities; the consumer-aware filter
  decides whether the SAT layer needs to see them.

Counters to add:

- `:no.interface.lra-to-euf-equalities-euf-already-true`;
- `:no.interface.lra-to-euf-equalities-euf-mergeable-skipped`;
- `:no.interface.lra-to-euf-equalities-euf-forbidden-forced`;
- `:no.interface.lra-to-euf-internal-model-equalities`, if skipped equalities
  are carried into model reconstruction;
- `:no.interface.lra-to-euf-skip-model-reconstruction-fallbacks`, if a skipped
  equality later forces the broad fallback.

Expected benchmark movement:

- lower `:no.interface.sat-equalities-forced`;
- lower `:no.interface.lra-to-euf-rechecks`;
- lower `:sat.solve-calls` on MathSAT RandomDecoupled timeouts;
- lower wall time on CPAchecker solved-but-slow cases whose SAT search is
  already small but equality probing is large.

## Next Simplex-Side Target

The consumer-aware skip only reduces SAT pollution after an equality has already
been found. The next layer is to reduce how many temporary strict-bound probes
are needed to find LRA equalities in the first place.

The first LRA-side prototype should expose a narrow equality source with clear
limitations:

- paired non-strict bounds already used by online propagation;
- variables fixed by current lower and upper bounds to the same rational value;
- direct row-derived equalities only when the row and active bounds make the
  proof obligation obvious and explainable.

This must not be documented as the full Section 5.1 Simplex algorithm unless
the implementation really maps the paper's operations, invariants, and
complexity expectations. A baseline fixed-bound detector is acceptable, but it
should be called a baseline.

Required data structures:

- a theory-layer result object over `core::TermId` pairs, not SAT literals;
- a reason object or callback capable of producing the bound literals used to
  prove the equality;
- counters separating candidate production from SAT materialization and from
  successful propagation.

Required tests:

- fixed lower/upper bounds produce an explained shared Real equality;
- a one-sided bound does not produce an equality;
- model-equal but mutation-avoidable values do not become entailed
  propagations;
- paired-bound online propagation remains lazy and keeps
  `:no.online.precreated-equality-atoms == 0`;
- pure QF_LRA and pure QF_UF runs keep Nelson-Oppen online stats disabled.

## Model-State Direction

After the two narrow optimizations above, the larger architectural step is to
move from final-check loops toward a QF_UFLRA model state.

The first useful state object should record:

- current shared Real terms;
- current EUF closure classes and forbidden equalities under the SAT assignment;
- current LRA model values for shared Real terms;
- LRA equality candidates with one of three classifications:
  `entailed`, `model_equal_mutation_avoidable`, or
  `model_equal_mutation_forced`;
- equalities absorbed internally by the consumer theory for final/model
  validation;
- SAT-visible equality atoms already created, assigned true, or assigned false.

This object should still be QF_UFLRA-specific until the invariants are stable.
General `DPLL(T^M)` can come later; the current evidence does not justify a
generic rewrite before the equality-probe cost is reduced.

## Preprocessing Tie-In

Preprocessing remains a separate opportunity. The Z3 stats show that some
MathSAT RandomDecoupled instances have small Boolean search in Z3 and many
disjunctions/disequalities in the SMT-LIB input. The existing preprocessing
opportunity list in `docs/preprocessing.md` remains relevant:

- pseudo-Boolean-over-Real finite-domain recognition;
- guarded at-most-one/exactly-one structure for repeated Real domain choices;
- simplification from top-level LRA unit facts;
- reuse of equality and bound definitions for repeated linear terms and Real UF
  proxies.

However, the current post-pruning evidence gives the mixed equality path a more
direct first target than broad preprocessing: solved-but-slow CPAchecker cases
have tiny SAT search but thousands of LRA extra-atom probes. Those should improve
from equality-probe reduction even before new preprocessing is added.

## Verification Gate

For any accepted implementation of the targets above:

- add focused unit regressions for mergeable-skipped and forbidden-forced
  LRA-to-EUF equalities;
- run the focused QF_UFLRA unit tests;
- run `make lint`;
- run `make check` when feasible;
- run `make docs`;
- run `git diff --check`;
- run a fresh QF_UFLRA fuzz gate against Z3, preferably at least 1000 generated
  mixed cases for a merge-ready change;
- rerun the 45-file SLURM sample from
  `benchmarks/qf_uflra-preprocessing-opportunity-sample.txt` with default,
  candidate final scan, and Z3;
- record all nontrivial local, fuzz, and SLURM evidence in `docs/run-log.json`;
- compare solved counts, wrong answers, PAR2, and the new equality counters
  against commit `8b3bb4e`/artifact
  `remote:primo-candidate-final-7a585fc/results/qf-uflra-candidate-final-119470`.

Performance claims are blocked by any wrong answer, crash, invalid model, or
unclassified reference disagreement. Timeout-only results are acceptable for
triage but not for claiming the feature is headline-ready.

## Decision Criteria

Accept the consumer-aware equality skip if it:

- preserves all focused tests and fuzz correctness;
- does not regress pure QF_UF or pure QF_LRA statistics materially;
- reduces `sat_equalities_forced` and `lra_to_euf_rechecks` on the 45-file
  sample;
- improves PAR2 or solved count on the current QF_UFLRA sample, or gives a clear
  per-family improvement without wrong answers.

Reject or revise it if:

- skipped equalities break model reconstruction;
- a false generated equality can be skipped as mergeable;
- the implementation only moves cost from SAT rechecks into more EUF push/pop
  probes;
- the counters show the skipped population is negligible on the target sample.

