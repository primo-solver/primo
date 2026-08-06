# QF_UFLRA Model-Guided Performance Plan

Status stamp: 2026-08-06, branch `nelson-oppen-online-lazy-ipasir-up`.

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

**Correctness blocker and repair, 2026-08-05.** The later full 1284-file QF_UFLRA
5s SLURM run at commit `f7c8bd7`, recorded as
`2026-08-05-qfuflra-full-5s-rejected-unsound-filter`, invalidates the
consumer-aware LRA-to-EUF equality-skip experiment as performance evidence. The
candidate-final array produced three wrong `sat` answers on benchmark rows whose
SMT-LIB status and existing Z3 artifact both say `unsat`; local repro showed the
default filter path returning `sat` on the same checked rows. The conservative
repair disables the mergeable-skip optimization: LRA-implied equalities that EUF
can merge are now still forced through the guarded interface-equality clause
path unless EUF already has the pair equal. Performance work on this line is
blocked until the repaired path passes the stronger gate below.

The fuzz evidence that preceded this broad run was insufficient for this
mechanism. The default filter had 1000 generated `mixed` cases from
`scripts/qf_uflra_smoke_fuzz.py` with seed `20260806`, but that generator uses
small synthetic formulas and there is no tracked `benchmarks/seeds/qf_uflra`
real-benchmark seed corpus. The opt-in candidate-final path had only a 100-case
smoke run with seed `20260804`. Future QF_UFLRA theory-combination benchmarks
must run a merge-grade gate for the exact solver options being evaluated, and
that gate must include real benchmark seeds or saved regressions for the
arrangement/equality mechanism.

The older evidence below is historical context only; it is not sufficient to
claim the current filter is sound.

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

The repaired full 1284-file 60s SLURM evaluation at commit `4631aef` provides
new partial evidence while its default array is still running. The
candidate-final array completed with 1013 solved, 271 timeouts, zero wrong
answers among 1282 checked rows, and PAR2 34267.899005. Its aggregate counters
make several non-LRA integration costs explicit:

- 4312141 candidate-final pairs were scanned and 29108941 full pairs were
  skipped, but the coordinator still processed 398936 LRA-to-EUF equalities;
- 228847 interface-equality clauses carried 34241886 reason literals, about 150
  antecedents per clause on average;
- 24364 equality rechecks led to 34974 SAT solve calls;
- the full shared-pair diagnostic still counted 34779350 pairs;
- mixed EUF online propagation and mixed dynamic-Ackermann counters remained
  zero because those existing pure-QF_UF mechanisms are not connected to the
  mixed dispatcher.

These are current diagnosis numbers, not a completed default-versus-candidate
comparison. The default array and final CSV comparison remain pending.

## Non-LRA-First Roadmap

The immediate implementation order must avoid overlapping the concurrent LRA
work. In particular, this phase must not add fixed-variable detection,
row-derived equality rules, one-sided-bound analysis, freedom intervals, or a
second arithmetic model implementation. It can still remove substantial cost
at the SMT/EUF/SAT integration layer.

### 1. Preserve Existing Probe Explanations (Implemented)

The two strict-orientation probes already return LRA conflict clauses. The
coordinator currently reduces those results to booleans while mining implied
equalities, then learns each equality through
`force_implied_interface_equalities`, whose generic reason is every unfiltered
assignment in the current theory view.

The SMT-local result now carries an
explained result containing the equality pair and the union of the two existing
strict-probe conflict reasons, with synthetic literal zero removed. Route each
result through the reason-specific interface forcing path. This changes no
tableau invariant or arithmetic inference: it preserves explanations the LRA
checker already computed instead of discarding them.

The implementation publishes:

- mined equalities with probe-local reasons;
- fallback equalities using a whole-assignment reason;
- `:no.interface.equality-local-reasons` and
  `:no.interface.equality-local-reason-lits`;
- `:no.interface.equality-fallback-reasons` for the defensive whole-assignment
  fallback.

### 2. Use EUF Equality Explanations (Implemented)

`FlatCongruenceClosure` already exposes `explain_equal`. EUF-to-LRA interface
equalities carry that proof-local explanation instead of the complete
EUF assignment. This is an SMT/EUF integration change using an existing theory
API and shares the explained interface-equality result shape from the first
milestone. `:no.interface.euf-explanation-missing` identifies any violation of
the expected `are_equal`/`explain_equal` invariant before the defensive reason
fallback is used.

Tests must cover direct equality, congruence-derived equality, predicate/Boolean
reasons, generated interface atoms, and backtracking. The propagated equality
must remain implied by its recorded antecedents when unrelated assigned atoms
are removed.

### 3. Make The Equality Ledger Sparse (Implemented)

Lazy IPASIR-UP atom creation and the combination manager now both avoid
preallocation. `TheoryCombinationManager::rebuild_from_interface_terms` keeps
the shared term set and arithmetic full-pair count but starts with an empty
pair ledger. The assertion analyzer stores visible pairs only.

`ensure_interface_pair(lhs, rhs)` validates registered interface membership and
same-sort ownership before creating a missing entry. Producers and lazy SAT atom
materialization call it explicitly. The mixed path constructs the old full pair
vector only when full-pair model-equality branching is requested. The statistics
`:no.interface.materialized-pairs` and
`:no.interface.materialized-missing-pairs` distinguish the sparse ledger from
the theoretical `:no.online.shared-real-pairs` count.

### 4. Reuse Incremental EUF In Mixed Search (Partial, Experimental)

The pure QF_UF path already has `OnlineEufPropagator`, explained congruence
closure, and optional dynamic Ackermann/disequality propagation. The mixed
`MixedTheoryPropagationDispatcher` is now a backend-neutral IPASIR-UP
multiplexer: it broadcasts assignment, decision-level, backtrack, and model
callbacks to optional EUF and LRA children and routes each propagated reason or
external clause back to the child that produced it. Lazily generated interface
equality atoms are appended to the EUF child's atom table and registered in its
flat assignment state before the fresh SAT variable is observed.

This EUF child is enabled only by the explicit
`--qf-uflra-online-euf` option together with
`--nelson-oppen-prop=online`. It is default-off because the first 45-file,
3-second local ablation at commit `cbe1cfc` solved the same 43/45 instances with
zero wrong answers and changed PAR2 only from 20.660 to 20.639. On the
representative MathSAT `pb_real_40_80_60_36.smt2`, both paths solve in about one
tenth of a second in a Release build, while the EUF child performs no useful
propagation. This is not evidence for default promotion.

The later 1284-file SLURM run at commit `9bf9fc1` invalidated the online final
scan shortcut before it established any performance result. Ordinary online
mode returned two wrong `sat` answers on expected-`unsat` MathSAT inputs; the
online-EUF profile returned one. In each case, online paired bounds had produced
some equality, but a different LRA-implied equality was still needed for an EUF
congruence conflict. Successful model separation did not prove that omitted
equality was irrelevant. The run is rejected, the broad final LRA equality scan is
again mandatory, and both exact inputs are tracked as regression seeds. Removing
that scan now requires a complete shared-model validation invariant rather than
another activity-based skip condition.

The final coordinator still rebuilds a flat EUF closure after SAT returns. The
EUF component does not yet expose a safe live closure/model view, so care pairs,
EUF-forbidden checks, and final equality explanations do not reuse callback
state. Generated equality registration also does not extend the optional
dynamic Ackermann indexes. Dynamic Ackermann propagation therefore remains a
separate mixed-mode ablation, and live closure reuse is the unfinished part of
this milestone.

### 5. Tighten Candidate Scheduling And Preprocessing

Keep care-pair and EUF-cooccurrence candidates ahead of any full fallback, cache
pair classifications for the current SAT trail, and avoid reconsidering an
already assigned or already explained pair in the same combination round.
Positive-first model equality case splits remain default-off until model
mutation is available.

Preprocessing stays family-targeted. The representative MathSAT
`pb_real_40_80_60_36.smt2` is one large conjunction of roughly 480 ternary
mixed clauses, with no ITEs; its dominant evidence is equality traffic rather
than missed finite-domain or ITE rewriting. Finite-domain, top-level LRA-unit,
ITE, and proxy-definition improvements remain plausible for CPAchecker/BMC or
other mined families, but they should not displace the four integration
milestones above for RandomDecoupled.

### Deferred LRA Work

After the concurrent LRA work lands, re-audit its APIs before implementing the
paper's Simplex-specific mechanisms:

- `FixedEq`, `EqRow`, and `EqOffsetRows` opportunistic equality production;
- linear expected-time bucketing of shared terms by the current `beta` value;
- freedom-interval mutation of non-basic variables;
- short bound/row explanations produced directly by the active tableau.

The combination layer should consume these through a theory-owned explained
candidate API. It must not reproduce bound or row analysis in `smt/`.

### Non-LRA Milestone Gate

Before accepting this phase:

- pure QF_UF and pure QF_LRA answers and selected statistics must remain
  unchanged on seconds-scale samples;
- focused tests must check sparse creation, trail/backtrack behavior, and exact
  EUF/probe-local reason validity;
- aggregate interface reason literals and equality rechecks must decrease on
  the 45-file sample without increasing forced equalities or wrong answers;
- the exact option set must pass the existing 5000-case generated QF_UFLRA gate
  and real-seed YinYang/typefuzz gate;
- the sharded, durable-output full SLURM comparison must report family summaries,
  wrong answers, timeouts, and PAR2 before any default-promotion claim.

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

## Immediate Performance Target

After the shared-equality ownership cleanup, the current conservative source
change keeps LRA-to-EUF equality handling consumer-aware only as a classifier.
The broad 2026-08-05 rejected run shows that treating EUF-mergeable LRA
equalities as internal model equalities is unsound as implemented, so the
repaired path forces mergeable LRA equalities through SAT. `TheoryCombinationManager` owns the
shared equality ledger: interface equality atom identity, SAT literal
assignment snapshots, trailed arrangement values, and per-round classification
flags. The QF_UFLRA coordinator uses that ledger as the single place where an
equality is recorded as already true, assigned false, EUF-implied, LRA-implied,
SAT-forced, EUF-already-true, EUF-mergeable, EUF-forbidden, or model/internal.

Implemented conservative rule:

1. When LRA reports or proves a shared Real equality `a = b`, first query the
   current EUF closure under the current SAT assignment.
2. If EUF already has `a` and `b` equal, do not create a new equality atom and
   do not add a guarded SAT equality clause.
3. If EUF can merge `a` and `b` without an immediate conflict, record
   `:no.interface.lra-to-euf-equalities-euf-mergeable-forced` and force the
   equality through the guarded equality-clause path. The previous internal-skip
   behavior is rejected until there is a sound model-construction proof and a
   stronger fuzz/seed gate.
4. If EUF forbids `a = b`, keep the existing guarded equality-clause path. This
   is the case where the equality is genuinely useful: it rejects the current
   SAT arrangement.
5. If the equality atom is already assigned false, EUF must see that as a
   disequality. The pair is then EUF-forbidden and must not be skipped.

Soundness obligation:

- Skipping a mergeable LRA equality is currently disabled. It is allowed only
  after final validation and model reconstruction account for the equality
  internally and the real benchmark regressions stay clean.
- A skipped equality must not allow a model where LRA gives the same Real value
  to `a` and `b` but EUF prints incompatible interpretations for UF applications
  that depend on separating those Real arguments.
- Any explicit or generated disequality already present in the SAT assignment
  must turn the pair into the forced/conflict path, not the skipped path.

Implemented C++ ownership:

- `TheoryCombinationManager` in `src/smt/theory_combination.hpp` owns the
  shared equality ledger and should be the only component that records
  per-pair combination-layer classification.
- `src/smt/check_sat.cpp` remains the orchestrator that asks SAT, EUF, and LRA
  for their current facts, but it should not keep parallel ad hoc equality
  state.
- EUF mergeability is queried with a temporary push/pop closure state. The
  closure includes current true equalities and false generated interface
  equalities. Mergeable LRA equalities are classified but then routed through
  guarded clauses for soundness.
- LRA remains the producer of entailed equalities; the current consumer-aware
  classifier decides whether EUF already has the equality, or whether the SAT
  layer must still see it through a guarded clause.

Implemented counters:

- `:no.interface.lra-to-euf-equalities-euf-already-true`;
- `:no.interface.lra-to-euf-equalities-euf-mergeable-skipped` remains a
  diagnostic counter from the rejected skip experiment and should stay zero in
  the repaired conservative path;
- `:no.interface.lra-to-euf-equalities-euf-mergeable-forced`;
- `:no.interface.lra-to-euf-equalities-euf-forbidden-forced`;
- `:no.interface.lra-to-euf-internal-model-equalities`;
- `:no.interface.lra-to-euf-skip-model-reconstruction-fallbacks`.

Expected benchmark movement after any future sound skip repair:

- lower `:no.interface.sat-equalities-forced` without increasing wrong answers;
- lower `:no.interface.lra-to-euf-rechecks` without increasing wrong answers;
- lower `:sat.solve-calls` on MathSAT RandomDecoupled timeouts;
- lower wall time on CPAchecker solved-but-slow cases whose SAT search is
  already small but equality probing is large.

## Deferred Simplex-Side Target

The consumer-aware skip only reduces SAT pollution after an equality has already
been found. After the non-LRA-first roadmap and the concurrent LRA work are
integrated, the next arithmetic layer is to reduce how many temporary
strict-bound probes are needed to find LRA equalities in the first place.

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

Primo already has shared equality ownership in `TheoryCombinationManager`.
That class owns the interface term list, visible or generated interface equality
registry, SAT literals for equality atoms, SAT assignment snapshots, trailed
`InterfaceEqualityValue` entries, and per-round classification flags. This is
necessary shared interface state, but it is not yet the model state meant here.
It does not own an EUF model, an LRA model, a reconciled arrangement/model view,
or mutable model values. Those objects are still rebuilt in `solve_qf_uf_lra` as
local final-check data.

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

- add focused unit regressions for EUF-already-true, mergeable-forced, and
  forbidden-forced LRA-to-EUF equalities; any future reintroduction of
  mergeable-skipped behavior needs separate regression coverage for model
  reconstruction and false generated equality assignments;
- add the three rejected full-run wrong rows, or minimized forms derived from
  them, to the QF_UFLRA regression/seed corpus before rerunning performance
  evaluation;
- run the focused QF_UFLRA unit tests;
- run `make lint`;
- run `make check` when feasible;
- run `make docs`;
- run `git diff --check`;
- run a fresh QF_UFLRA fuzz gate against Z3 for the exact option set under
  evaluation, including at least 1000 generated mixed cases and real benchmark
  seed or saved-regression mutation coverage for this equality/arrangement path;
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
