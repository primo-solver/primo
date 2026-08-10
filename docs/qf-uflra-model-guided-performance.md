# QF_UFLRA Model-Guided Performance Plan

Status stamp: 2026-08-10, branch `convex-no-qflra-integration`, through the
exact-fallback profile and literature audit at commit `5702fc0`.

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

**Section 5.2 integration result, 2026-08-09.** The merged LRA prerequisite
provides `freedom_interval()` and
`diversify_shared_model()`. The mixed coordinator now runs one scratch sweep per
feasible round when model reconciliation or model-equality branching is enabled,
projects the post-mutation classes once, and reuses them for reconciliation,
exact convex candidate filtering, and optional branch selection. This removes
the former two-fresh-solves-per-pair mutation proxy. The pre-mutation pair count
is computed from class cardinalities, so only post-mutation pairs are
materialized.

The soundness boundary is explicit: a pair left equal by the greedy sweep is not
called forced. Only the explained `FixedEq`/`EqRow` producers and exact convex
fallback may publish an entailed equality. The scratch assignment is not
installed into the live tableau, so final arrangement/model reconstruction
retains its independent strict-separation validation. Focused tests cover an
accidental equality removed before queue/atom creation, an entailed equality
that cannot move, and model reconstruction after scratch-only mutation.

Fresh SLURM gates at commit `4937d3c` passed 4,000 generated cases with
reconciliation and another 4,000 with model-equality branching. Each option set
covered 3,000 generated mixed cases plus 500 finite-domain QF_UF and 500
finite-domain QF_UFLRA cases, with no wrong answer, timeout, crash, unsupported
result, parser/type error, or reference disagreement. The reconciliation and
branching YinYang runs then each processed all 11 compact QF_UFLRA seeds. They
made 43,982 and 43,872 differential solver calls respectively, with no invalid
seed or bug trigger.

On the same-commit 1,284-file QF_UFLRA reconciliation ablation at five seconds
per file, mutation on and off both solved 1,156 files with 128 timeouts and zero
wrong answers or answer/status differences. The scratch sweep removed
2,280,171 of 11,450,940 model-equality candidate pairs. SAT decisions fell from
1,388,580 to 1,281,729, propagations from 24,996,441 to 24,718,750, and solve
calls from 2,199 to 2,169. PAR2 fell from 1,419.782409 to 1,412.285038; solved-row
time fell from 139.782409 to 132.285038 seconds. The unchanged Z3 result remains
1,234 solved, 50 timeouts, and PAR2 758.708455.

The 45-file, 60-second sample gives the same policy decision. Reconciliation
solved 43 files both with and without mutation; PAR2 changed from 275.807627 to
271.117621. Model-equality branching solved only 37 files in both variants,
although mutation reduced its PAR2 from 1,034.190677 to 991.359031. The reused
Z3 result solved all 45 with PAR2 10.936763. Mutation therefore remains enabled
within the opt-in reconciliation and branching profiles, with
`--no-model-equality-mutation` retained as an ablation. Neither profile is
promoted to the ordinary default by this result.

**Shared-model reconciliation result, 2026-08-09.** The repaired opt-in
`--qf-uflra-model-reconciliation` profile passed 3,000 generated mixed cases,
1,000 finite-domain cases, all 11 tracked-seed preflights, and a YinYang
campaign configured for 2,000 mutations per seed on SLURM. The seed preflight caught an
earlier unsound version: internally merging `x = y` could also make `f(x) =
f(y)` true by congruence without sending that consequence back to LRA. The
implemented repair keeps such congruence-producing merges on the explained
SAT-visible path.

On the full 1,284-file QF_UFLRA corpus at five seconds per file, the repaired
profile solved 1,156 files with PAR2 1,424.154414, compared with 1,123 and PAR2
2,040.942239 for the same-commit default. It solved all 900 MathSAT files and
242/359 CPAchecker induction files; default solved 893 and 216 respectively.
There were no wrong answers, non-timeout failures, or decisive-answer
disagreements with default or the reused identical-corpus Z3 result. The
profile reduced SAT decisions from 2,646,397 to 1,322,558, propagations from
42,195,478 to 24,815,243, solve calls from 8,348 to 2,177, and SAT-visible
interface equality atoms from 17,730 to 1,161. It absorbed 19,600 explained
equalities internally and used 584 guarded congruence fallbacks with zero
validation or exact-fallback failures. Exact commands, corpus hashes,
scheduler state, and artifacts are in `docs/run-log.json`.

This accepts the profile as a measured QF_UFLRA experiment, not as the default
or as a full implementation of the paper. At that historical commit, a second
EUF-to-LRA reconciliation iteration, generic component-model state, and broader
logic coverage were missing. The class/worklist milestone below now addresses
the round-local congruence-consequence step; generic component-model state and
broader logic coverage remain absent. The later Section 5.2 scratch
model-mutation integration described below was not included in this historical
benchmark result.

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
small synthetic formulas. The generator now includes a dedicated
`bool-arguments` shape and the mixed mode samples it alongside the other
QF_UFLRA shapes. The tracked `benchmarks/seeds/qf_uflra` corpus also contains
Boolean-argument regressions. The opt-in candidate-final path had only a 100-case
smoke run with seed `20260804`. Future QF_UFLRA theory-combination benchmarks
must run a merge-grade gate for the exact solver options being evaluated, and
that gate must include real benchmark seeds or saved regressions for the
arrangement/equality mechanism.

The Boolean-argument bridge is deliberately separate from the arithmetic
model-guided scan. It uses canonical EUF `true`/`false` terms and SAT clauses
linking each Boolean UF argument to exactly one of them. This makes Boolean UF
congruence available to mixed EUF through ordinary explained closure, without
promoting model coincidences or adding an all-pairs SAT workload.

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

## Historical Non-LRA-First Roadmap

This ordering governed the work while the LRA branch was concurrent. That branch
has now been merged intact. The current integration consumes its APIs and does
not duplicate fixed-variable, row, one-sided-bound, or freedom-interval logic in
the SMT layer.

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
equality was irrelevant. The run is rejected and the broad final LRA equality
scan is again mandatory. The compact
`benchmarks/seeds/qf_uflra/online_nonpaired_congruence_forced_unsat.smt2`
witness and focused unit tests gate this correctness property. The two full
MathSAT inputs remain under `benchmarks/performance/qf_uflra/` as deferred
performance regressions, not mutation seeds. Removing the scan requires a
complete shared-model validation invariant rather than another activity-based
skip condition.

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
Positive-first model equality case splits remain default-off while the new
scratch mutation integration is fuzzed and evaluated.

#### Exact-probe tableau boundary

The exact fallback's per-pair tableau copies are expensive, but replacing them
with a permanently cached difference row for every candidate is not justified
by the linear-arithmetic literature. Dutertre and de Moura, *Integrating
Simplex with DPLL(T)*, Sections 2.2-2.3, describe the older incremental-Simplex
scheme: asserting an arbitrary linear atom creates a slack variable and row;
retraction removes the row and may first need a pivot to make its slack basic.
They identify frequent row insertion/removal, slack creation/deletion, and the
associated bookkeeping as significant costs. Their replacement in Sections
3.1 and 3.2 fixes the matrix from all formula atoms known in advance and makes
search assertions and backtracking change only bounds. Section 3.2.4 therefore
supports `push`/`pop` of `x < y` only after a variable for `x - y` has already
been internalized; it does not make an unseen linear expression a reversible
bound automatically.

Primo has the same boundary. `LraTableau::push()` trails bounds (and the
SOI-specific state), while `add_variable()` and `add_row()` are structural and
not trailed. The exact oracle consequently copies the feasible tableau, adds
one difference row, checks both strict orientations with bound checkpoints,
and discards the copy. This is an implementation fallback, not a mechanism
recommended by the model-based combination paper.

The size risk is concrete on the profiled CPAchecker `dvb-usb-az6027` case. Its
prepared tableau has 2,437 variables, 1,091 rows, and 2,201 row nonzeros. The
candidate-final set contains 32,817 pairs and each observed difference row has
at most five terms. Permanently internalizing that set could add 32,817
variables/rows and up to about 164,000 coefficients; the full 759-term shared
cross product contains 287,661 pairs. A lazy cache avoids up-front allocation
but remains quadratic if search eventually requests most pairs, and inactive
rows can still enlarge violation scans, occurrence lists, and pivot updates.

The paper-grounded primary direction is therefore to request far fewer fresh
equalities: de Moura and Bjorner Section 4 uses postponed `M-Propagate`, model
mutation, and positive-first case splits, while Section 5.1 uses `FixedEq`,
`EqRow`, `EqOffsetRows`, and `EqRows` for equalities available without an
additional solve. Their reported model-based runs propagated 2.46 shared
equalities on average and at most 57, which is qualitatively different from
internalizing every exact-probe candidate. A bounded one-scratch-tableau or
scoped-temporary-row experiment remains possible, but it must expose maximum
added rows/nonzeros and probe-check cost, and it must be rejected if structural
growth approaches the candidate set. It is not the next default mechanism.

#### Complete convex model-reconciliation checklist

The next opt-in implementation follows de Moura and Bjorner Sections 4, 5.1,
and 5.2 without claiming the generic Appendix A transition system. Its state is
the current SAT assignment, an EUF closure, and one LRA candidate assignment.
`TheoryCombinationManager` owns only their shared-term projection and the lazy
equality-decision lifecycle; EUF and LRA retain their theory-local objects.

The LRA candidate model is a full scratch `beta`, not only an equality
partition. Every accepted freedom-interval update must preserve every tableau
row and bound. SAT-false shared equalities are mandatory disequalities for this
model: mutation first minimizes their violations and only then maximizes the
number of value classes. If the greedy sweep leaves a mandatory pair equal,
the coordinator searches the two strict orientations for those unresolved
pairs only. Failure of this heuristic is never an entailment result; an
equality is forced only from two explained strict conflicts.

Before model propagation, the remaining Section 5.1 rules are indexed rather
than compared pairwise. `EqOffsetRows` groups a shared basic variable by its
single non-fixed coefficient-one variable and fixed offset. `EqRows` groups by
the canonical non-fixed row polynomial and fixed offset. Each bucket emits a
spanning set and combines the fixing-bound reasons from the two rows. The
expected work is linear in scanned row nonzeros plus emitted reasons; the
broader rule runs once immediately before model propagation, not on every
pivot.

Model agreement is tested by checkpointing the EUF closure and merging the
deterministic spanning edges of each LRA value class. Successful merges are
candidate-model choices, not theory consequences. If every edge is absorbed,
the component models agree on the shared Real interface. If an edge conflicts,
that pair becomes one lazy positive-first `M-Propagate` SAT assumption. Existing
true and false equality atoms participate in the same arrangement; formula
occurrence is not a reason to skip them. The finite set of shared pairs gives
termination, so the complete reconciliation profile has no candidate budget or
adaptive early stop.

The old exact scan remains a shadow oracle and conservative fallback while the
profile is opt-in. It is required for an unmapped shared term, a failed
mandatory-separation invariant, or a disagreement between candidate-model and
exact validation. No such fallback may be reported as successful model-based
convergence. Pure QF_LRA and QF_UF never construct this state and must retain
identical SAT/theory statistics.

Preprocessing stays family-targeted. The representative MathSAT
`pb_real_40_80_60_36.smt2` is one large conjunction of roughly 480 ternary
mixed clauses, with no ITEs; its dominant evidence is equality traffic rather
than missed finite-domain or ITE rewriting. Finite-domain, top-level LRA-unit,
ITE, and proxy-definition improvements remain plausible for CPAchecker/BMC or
other mined families, but they should not displace the four integration
milestones above for RandomDecoupled.

### Integrated LRA Work

The merged LRA branch supplies the paper-grounded mechanisms below, which the
mixed coordinator consumes without reimplementing their arithmetic logic:

- `FixedEq` and `EqRow` opportunistic equality production;
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

- **Broader cheap Simplex equality production.** Section 5.1 `FixedEq` and
  `EqRow` producers are implemented, but the exact fallback still probes
  surviving pairs not covered by those rules.
- **Model mutation in the default profile.** Section 5.2 freedom intervals and
  greedy diversification are implemented and wired to the opt-in shared-model
  and branch profiles. The ordinary default final path does not run the sweep.
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

## Shared-Model Reconciliation Prototype

The opt-in `--qf-uflra-model-reconciliation` profile implements the first
round-scoped ownership slice. `TheoryCombinationManager::shared_model()` stores
the current generation, LRA value classes over shared Real terms, deterministic
spanning equalities, internally absorbed equalities, and strict separation
atoms selected during final validation. SAT re-entry and backtracking
invalidate this state. EUF closures and LRA tableaux remain theory-owned.

The first producer admitted to the internal path is deliberately conservative:
only an explained LRA entailment enters the manager's worklist. A batch is
asserted into the live EUF closure with its LRA reasons, then the shared EUF
classes are projected once. Consequences whose endpoints are mapped into the
same diversified LRA model class remain internal; disagreement or an unmapped
endpoint lazily creates an equality atom with the EUF explanation. An immediate
EUF contradiction returns the composed conflict directly. Final validation and
model reconstruction reuse internal spanning equalities. The regression is
`benchmarks/seeds/qf_uflra/real_congruence_forced_unsat.smt2`: internalizing
`x = y` without reconciling `f(x) = f(y)` would incorrectly accept incompatible
arithmetic values. Enabling the profile also enables
candidate-first scanning, with the broad exact scan retained as a fallback when
arrangement validation detects a missed equality.

This repairs the invariant missing from the rejected mergeable-skip experiment:
an equality is not called internal unless the receiving model actually absorbs
it. It is still not the paper's full `M-Propagate`/`M-Mutate` system. In
particular, arbitrary model coincidences are recorded as classes but are not
propagated as entailments. The integrated Section 5.2 sweep mutates only a
scratch LRA assignment; it does not change live tableau state. The safety check
now counts distinct eager EUF representatives among shared terms before and
after each tentative merge. The intended direct join
reduces the class count by exactly one; any other delta keeps the guarded SAT
fallback. This is expected linear in the shared-interface size per attempt and
replaces the earlier quadratic shared-pair comparison. Counters
`:no.shared-model.congruence-summary-checks` and
`:no.shared-model.congruence-summary-terms` expose its invocation count and
total representative visits. That older guard is superseded by the manager's
explained-equality worklist and one-shot shared EUF class projection. The
profile remains opt-in while component models and the generic paper transition
system remain absent.

The 2026-08-09 validation at commit `2f9113b` accepts this linear summary as an
implementation improvement, not as a profile promotion. A fresh SLURM gate
passed 3,000 generated mixed cases, 1,000 finite-domain cases, all 11 tracked
seeds, and 2,000 YinYang iterations per seed without a discrepancy or bug
trigger. On the fixed 45-file sample, the old and new implementations both
solved 43 files with two timeouts and identical SAT search counts; PAR2 changed
from 277.404 to 274.483 seconds. On the full 1,284-file, five-second corpus,
both solved 1,156 files with 128 timeouts and no answer or status change; PAR2
changed from 1424.154 to 1421.232 seconds. The exact class-delta criterion used
588 guarded fallbacks rather than the old classifier's 584, so SAT decisions
increased from 1,322,558 to 1,390,745 even though aggregate solved-row time fell
from 144.154 to 141.232 seconds. The new scan performed 21,036 checks and
13,653,597 representative visits. If this scan becomes dominant, reuse of the
representative-set scratch storage or an incremental shared projection is the
next optimization. The subsequently integrated scratch freedom-interval sweep
was validated separately and was not modified by the class/worklist milestone.

The next milestone at commit `4a93256` replaced the guarded per-merge summary
with class-based, same-round reconciliation. Its fresh gate passed 3,000 mixed
generated cases, 1,000 finite-domain cases, 11 tracked seeds, and 2,000
configured YinYang iterations per seed without a discrepancy. The full
1,284-file run preserved 1,156 solved files, 128 timeouts, and every decisive
answer; PAR2 improved from 1412.285 to 1407.514 seconds. It enqueued and drained
20,022 explained LRA equalities, projected 1,605 EUF class snapshots, retained
20,400 model-agree consequences internally, exposed 663 actual model
disagreements to SAT, and returned 15 direct combined conflicts. On the 241
rows that previously used congruence fallback, SAT equality atoms fell 36%,
solve calls 31%, decisions 13%, and propagations 13%. The profile remains
opt-in: solved count did not improve and reused Z3 still solves 1,234 files.

## Historical Consumer-Aware Target

After the shared-equality ownership cleanup, the first conservative source
change kept LRA-to-EUF equality handling consumer-aware and added one scratch
model-diversification pass before candidate expansion. The rules below describe
that historical pre-worklist milestone.
The broad 2026-08-05 rejected run shows that treating EUF-mergeable LRA
equalities as internal model equalities is unsound as implemented, so the
repair at that milestone forced mergeable LRA equalities through SAT.
`TheoryCombinationManager` owns the
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

## Integrated Simplex-Side Subset

The consumer-aware skip only reduces SAT pollution after an equality has already
been found. The merged LRA branch reduces temporary strict-bound probes through
the following narrow sources:

- paired non-strict bounds used by online propagation;
- `FixedEq` for variables fixed by current lower and upper bounds;
- `EqRow` for direct explained row equalities;
- model-value bucketing and Section 5.2 scratch diversification before exact
  candidate expansion.

These mechanisms are documented as Section 5.1/5.2 subsets, not as the full
Simplex algorithm from the paper.

Required data structures:

- a theory-layer result object over `core::TermId` pairs, not SAT literals;
- a reason object or callback capable of producing the bound literals used to
  prove the equality;
- counters separating candidate production from SAT materialization and from
  successful propagation.

Required tests:

- fixed lower/upper bounds produce an explained shared Real equality;
- a one-sided bound does not produce an equality;
- model-equal values removed by scratch mutation do not become entailed
  propagations;
- paired-bound online propagation remains lazy and keeps
  `:no.online.precreated-equality-atoms == 0`;
- pure QF_LRA and pure QF_UF runs keep Nelson-Oppen online stats disabled.

## Model-State Direction

After the two narrow optimizations above, the larger architectural step was to
move selected final-check data into a QF_UFLRA shared-model projection.

Primo already has shared equality ownership in `TheoryCombinationManager`.
That class owns the interface term list, visible or generated interface equality
registry, SAT literals for equality atoms, SAT assignment snapshots, trailed
`InterfaceEqualityValue` entries, and per-round classification flags. This is
necessary shared interface state. With
`--qf-uflra-model-reconciliation`, it now also owns `SharedModelState`: a
round-scoped projection containing LRA value classes, deterministic spanning
equalities, internally absorbed equalities, and selected strict separations.
It still does not own the component EUF closure, the LRA tableau, or mutable
model values. Those theory objects remain local to their solvers and are
rebuilt or copied by `solve_qf_uf_lra`.

The current implementation now reuses the structural EUF preparation across
mixed SAT rounds. `PreparedFlatTheory` owns the flattened terms, definition
closure, and deduplicated theory-atom mapping; newly materialized interface
equalities are appended before the next check. The deduplication map also gives
each cached atom a stable `(signed SAT literal, TermId)` identity. Every round
uses that identity to realign assignment values because generated equalities
can change polarity and therefore move between the true and false portions of
the round-local input. The closure itself remains a per-round copy, so this is
a preparation optimization, not the general model state or a persistent
DPLL(T^M) transition system.

The current projection records:

- the round generation and current shared Real terms;
- LRA model-value classes and their deterministic spanning pairs;
- internally absorbed explained equalities and selected strict separations;
- SAT-visible equality atoms already created, assigned true, or assigned false
  through the manager's existing ledger.

The next extension should record or expose:

- persistent, incrementally updated EUF class state across SAT rounds rather
  than the current round-local projection;
- component-model ownership rather than only an LRA value projection;
- LRA equality candidates with an explicit distinction between independently
  explained entailments and unexplained post-mutation model equalities;
- a persistent component-model transition rather than the current round-local
  reconciliation batch.

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
