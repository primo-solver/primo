# Assumption-based incremental QF_UF

Status: opt-in QF_UF implementation qualified at `fa2fe5c` on the scoped gates
and completed native sessions described below. Timed-out and unknown sessions
remain unqualified. Full artifacts have been collected and verified.
The design checklist below was recorded before implementation.
The default remains `rebuild`. The input archive is `sandbox/QF_UF.tar.zst`,
SHA-256 `52c55a17ee15e5c4a0fc723b9605ae42615da34499efc7e5a5e8b2c0ffc26a27`.
Its command-boundary inventory contains 1,778 sessions (766 Goel and 1,012
CLEARSY), 29,990 queries and 33,716 pushes/pops. Only 1,532 queries have a fresh
status annotation; remaining answers require reference comparison. Production
parser job **142323** verified all 1,778 command streams and 29,990 queries.

## Algorithmic basis and implemented subset

Cimatti, Griggio, Schaafsma and Sebastiani, *The MathSAT5 SMT Solver*, TACAS
2013, [sections 4.2–4.3](https://es-static.fbk.eu/people/griggio/papers/tacas13.pdf),
was read for labels, proxy protection and preprocessing hazards. Reuse Primo's
existing [](assumption-incremental-lra) guarded-root construction: unconditional
full Tseitin definitions, active selector assumptions, permanent negative units
for retired selectors, retained storage until reset. This deliberately differs
from MathSAT's label-dependent deletion and restoration policies.

Nieuwenhuis and Oliveras, *Fast Congruence Closure and Extensions*, Information
and Computation 205 (2007), pp. 557–580,
[sections 3.1–3.4](https://www.cs.upc.edu/~oliveras/IC.pdf), was read before
integration. Section 3.1 separates Curry/flat structural definitions from the
changing equality assignment. Reuse Primo's existing `FlatTermIndex` and
`FlatCongruenceClosure`; this task does not introduce a new closure algorithm.
The existing implementation maps the section 3 worklist, eager representatives,
class lists, affected use lists and signature lookup as documented in
[](papers/fast-congruence-closure-implementation). It still uses ordinary sparse
rollback and BFS explanations, not the paper's timestamped lookup rollback or
optimized proof production. Callback lifetime follows [](papers/ipasir-up)
and the already tested adapter reconnection contract.

## Ownership and operations

- `SmtSession` owns its frontend, scoped original assertions, declaration and
  definition visibility, and an `IncrementalEngine`; destroy the engine first.
- `IncrementalUfEngine` owns a permanent `CadicalBackend`, `BooleanClausifier`,
  selector stack and `PreparedFlatTheory`. It borrows the frontend term manager.
- Append clausifies only newly translated safe goals. Before a query, register
  Boolean UF arguments and extend structural preparation only for new atoms.
  Perform these registration scans once per query, not once per assertion.
- `PreparedFlatTheory` owns immutable-in-meaning flat definitions and the base
  closure with only definitional equations and `true != false`. It must never
  receive a query's equality/disequality assignments or SAT reasons.
- A fresh `OnlineEufPropagator` owns query-local assignment maps, queues,
  explanations and working congruence closure. Initialize it by copying the
  prepared structure, preserving flat IDs. Reconnect and reobserve every theory
  proxy before solving; root-level assignments must be delivered again.
- User pop retires selectors and visibility, while SAT search backtracking uses
  query-local closure checkpoints. No callback or reason stream outlives its
  owner. Only equality/congruence consequences with complete literal reasons
  may survive as SAT learning.
- Model construction consumes active lowered roots, the complete theory assignment and explicit
  visible symbol IDs. A popped sort/name may be redeclared with a fresh ID;
  retained historical symbols must not appear in current model output.

## Safe configuration and proof obligations

Auto/Off share mandatory conservative lowering; Auto additionally uses audited
context-independent rewriting. Equation elimination, finite-domain symmetry
constraints and other whole-goal preprocessing remain unavailable in this mode.
SAT preprocessing/inprocessing stay disabled and selectors/proxies protected.

The initial QF_UF route uses the incremental flat checker, Boolean argument
registration and Auto/Online/Conflict callback schedules. Reject unsupported
checker/schedule/domain combinations explicitly. Auto pure-theory filtering
resolves to Off here; explicit On is rejected. Syntactic atom polarity changes
across assertions and retained definitions, so the existing one-shot filtering
argument is not an incremental proof. See [](euf-boolean-arguments) for the
separate two-valued Boolean argument invariant. No existing rebuild defaults or
combined-theory routes change.

Proof obligations and tests:

1. Active selectors are equisatisfiable with precisely the active original
   assertions, including ITE side constraints and shared Boolean encodings.
2. New atoms/functions/Boolean arguments after SAT, UNSAT, unknown and pop
   preserve complete registration and fixed atom meanings.
3. Neither structural base nor a retired query leaks asserted facts into the
   next working closure. Test nested unary/binary congruence and disequalities.
4. Register every Boolean UF argument at both truth values; test predicate
   arguments, compound arguments, collapsed Bool domains and polarity changes.
5. Scoped sorts, constants, functions and definitions disappear from lookup;
   redeclarations and immutable model snapshots preserve identity and lifetime.
6. Test allocation growth, cancellation, query conflict limits and repeated
   checks. Compare generated sessions with Z3 and validate actual SAT models
   independently with Dolmen.

Structural extension flattens each new atom once. Registry scans, observation
setup and working-state copies cost retained term/atom size per query. Existing
rewriters may allocate scratch arrays proportional to retained terms per append.
Retired storage is unbounded until reset; no compaction is claimed. Counters
must expose engine/frontend creations, translated assertions, prepared atoms,
flat terms/definitions, active/retired selectors, clauses, working resets and
nested phase times.

## Evaluation and acceptance

Record baseline provenance at `5d930be` (solver-source identical to `04d2e23`).
Before full timing, run at least 1,000 fresh generated scoped UF sessions and
1,000 native-seed mutations against Z3 with separate actual-model validation.
Include the tracked Boolean-argument regression seeds in deterministic checks.
Use SLURM for substantial runs and preserve all candidates, inputs and results.

`scripts/slurm_incremental_uf_gate.sbatch` defaults to the measured Auto pipeline;
`PRIMO_UF_GATE_PROFILE=matrix` separately covers Auto/Off preprocessing and
Online/Conflict propagation. Each invocation runs 1,000 generated and 1,000
native mutations. `slurm_incremental_uf_regressions.sbatch` requires an explicit
`PRIMO_CTEST` path, since compute nodes need not expose CTest on PATH. The archive
driver uses streaming SHA-256 compatible with the cluster's Python version.

Evaluate the complete 1,778-file archive with original stdin, initially at
30 seconds per session, comparing rebuild, assumptions, Z3 and cvc5. Record
complete sessions separately from partial query progress and compute whole-session
PAR2. Check every successful candidate session's SAT query models in a separate
replay with identical options; timed-out/incomplete runs remain unqualified.
Replay answers must match both the timed query sequence and the independent
reference; a changed answer blocks qualification even if the replay model passes.
The Linux timing driver reaps the direct solver with `wait4`, collecting peak
RSS even after a timeout. It kills the solver's process group on timeout and
polls at 1 ms intervals; sub-millisecond timing differences are not meaningful.
Opening artifact files and writing resource metadata are outside the timed span.
Investigate lost solves and significant regressions with phase counters and
profiling before deciding acceptance. Preserve the existing QF_LRA assumptions
and default cross-logic tests. Run check, ASan, lint, docs and required reviews/
static scans, record experiments in `run-log.json`, commit coherent milestones,
collect artifacts and audit all jobs/processes and the worktree before handoff.

## Qualified native evaluation at `fa2fe5c`

SLURM timing job **142665** replayed all 1,778 unchanged files through real stdin,
with 30 seconds per whole session and a 10,000 theory-conflict budget for both
Primo modes. The Release binary uses CaDiCaL `3.0.0-7b99c07`. Z3 5.1.0 and cvc5
1.3.4 timings are the unchanged records from job **142464**; reference performance
was not rerun. Timing ran serially per task on `node-02.ciircluster`, with 8 GiB
requested and two logical CPUs allocated for each requested single-CPU task.
The Linux driver retains direct-child peak RSS, stdout, stderr and exact commands.

| Profile | Complete sessions | Timeout | Unknown | Mean whole-session PAR2 (s) | Complete-session queries |
| --- | ---: | ---: | ---: | ---: | ---: |
| Rebuild | 1,775 | 3 | 0 | 0.8294 | 29,573 |
| Assumptions | 1,772 | 0 | 6 | 0.4828 | 29,978 |
| Z3 | 1,778 | 0 | 0 | 0.1691 | 29,990 |
| cvc5 | 1,768 | 10 | 0 | 0.8313 | 29,970 |

There are zero answer disagreements among overlapping definite query answers,
zero crashes and zero parser errors. An incomplete session receives 60 seconds
PAR2 regardless of partial progress. Rebuild printed another 373 responses in
incomplete sessions; assumptions printed 12, including its unknown responses.
Those responses do not increase the complete-session count.

Among the 1,770 sessions completed by both Primo modes, the median rebuild /
assumptions runtime ratio is **2.964** and the geometric mean is **2.738**.
Assumptions gains two complete sessions and loses five, for a net loss of three.
The losses are Goel `rushhour` cases reaching the query conflict limit. Z3 remains
faster overall and completes more sessions. Peak observed solver RSS is 990 MiB
rebuild, 898 MiB assumptions, 156 MiB Z3 and 194 MiB cvc5; these maxima come from
potentially different inputs and are not paired ratios.

| Family | Rebuild complete / mean PAR2 (s) | Assumptions | Z3 | cvc5 |
| --- | ---: | ---: | ---: | ---: |
| Goel (766) | 764 / 1.2568 | 760 / 0.9923 | 766 / 0.3378 | 756 / 1.7531 |
| CLEARSY (1,012) | 1,011 / 0.5058 | 1,012 / 0.0972 | 1,012 / 0.0414 | 1,012 / 0.1335 |

Reuse is observable on CLEARSY `0002/00126.smt2`: assumptions completes all 413
queries in 4.533 seconds, with one frontend, one engine, 413 working-state resets,
2,551 translated assertions, 2,459 prepared atoms and 453 retired selectors.
Rebuild times out after 371 queries and frontend creations. At the last
assumptions query there are 69 active lowered model roots, 7,392 retained core
terms, 7,750 flat terms, 5,892 flat definitions and 15,135 active SAT clauses.
This demonstrates persistent frontend/SAT/preparation reuse with fresh working
theory state per query.

The Goel regressions were investigated before acceptance. Baseline `1595948`
profiling of `QF_UF_firewire_tree.5.prop2_ab_core_max.smt2` found 14.932 seconds
solving and 3.913 seconds preprocessing in the first assumptions query, compared
with 8.994 and 0.704 in rebuild. Its working-state reset cost 0.041 seconds.
Local `-pg` self samples were dominated by assignment callbacks (23.53%), SAT
propagation (22.79%), congruence merging (13.73%) and trail restoration (5.88%).
The safe preprocessing restrictions and retained search matter; working-state
recreation alone does not explain the regression. This profile is diagnostic,
not a replacement for the final SLURM timings. No unsafe whole-goal preprocessing
or silent fallback was introduced to recover these cases.

## Correctness coverage and acceptance

Fresh jobs **142601**, **142606** and **142611** passed **3,000 generated scoped
UF sessions plus 3,000 native mutations**, with 48,000 reference-checked queries
and 28,352 direct Dolmen SAT model passes. Auto/Off preprocessing and
Online/Conflict propagation are covered separately by the matrix gate.
The cross-logic gate **142616** passed another 1,000 rebuild sessions, 15,000
queries and 10,000 models. Deterministic compute-node regressions **142599**
include the minimized and generated ITE-model witnesses below, seven existing
Boolean-argument seeds, and the backend/session/closure/CLI tests.

The default preservation sample (**142620/142661**) passed all 25 files,
50 scoped sessions, 200 queries and 121 Dolmen models, with no lost solved files.
The median file-mode runtime ratio against the accepted `04d2e23` result is
0.993; summed file-mode time is 16.395 seconds versus 16.621. Model-validation
work is excluded from those timing diagnostics.

Separate native validation job **142666** completed all selected sessions:

| Profile | Qualified complete sessions | Reference-checked queries | Expected SAT models | Direct Dolmen passes |
| --- | ---: | ---: | ---: | ---: |
| Rebuild | 1,775 | 29,573 | 24,746 | 24,746 |
| Assumptions | 1,772 | 29,978 | 25,027 | 25,027 |

There are no missing selected rows, model errors, replay answer changes,
reference disagreements, validator rejections or validation timeouts. Each
actual session model was checked against its original query context, and every
replay answer matched the timed sequence. The correctness-only budgets were
120 seconds per solver/reference response and 120 seconds per Dolmen check;
this work ran after timing on `node-02`/`node-03` and is excluded from PAR2.
The three rebuild timeouts and six assumptions unknown sessions remain
unqualified, including any earlier SAT responses. Model checking does not
certify UNSAT; those answers have independent reference comparison.

The earlier `1595948` timing run remains historical, with incomplete model
qualification. It exposed the shared reconstruction cost described below.
Fresh gate `142579` at `d96902f` then exposed the original-root model error;
that candidate's dependent timing never ran. Both issues were fixed before the
qualified `fa2fe5c` evaluation. Earlier passing gates are not substituted for
current evidence.

The mode is accepted **as opt-in**. Its lower PAR2 and strong CLEARSY reuse do not
justify default promotion given the Goel losses. Tests, ASan, lint, docs,
code/efficiency review and static scans are recorded with their exact scope in
`run-log.json`. No source changes from a rejected solver optimization remain.

Reproduce with `scripts/slurm_native_uf_sessions.sbatch` and
`scripts/summarize_native_uf.py`. Set `PRIMO_UF_TIMEOUT=120` and
`PRIMO_UF_DOLMEN_TIMEOUT=120` only for the separate validation phase; timing keeps
30 seconds. Exact invocations, hashes, versions, profiles, input identities and
per-family coverage are in `benchmarks/assumptions-uf-evaluation.json` and the
per-shard `run.json` files. `scripts/slurm_collect_native_uf.sbatch` archives the
raw model contexts/responses, validators, timing outputs, gates, preserved
binary and original input archive after successful validation. Collection array
**142733** completed ten independently checksummed archives under
`results/uf-fa2fe5c-artifacts/`; all ten were collected locally and passed checksum
and compressed-stream integrity checks. Their sizes and hashes, terminal job
accounting, and the verification-log bundle are recorded in the evaluation
metadata. The collected binary, manifest and original input archive match their
pinned identities. No solver or collection jobs remain active. The initial serial collection job **142729**
was cancelled when traversal proved too slow for its resource budget; its
partial archive is retained separately and is not accepted evidence.

## Model replay bottleneck and insertion checklist

Native replay at `1595948` exposed a pre-existing model-construction cost in
both modes: `ComponentModelBuilder` uses `IncrementalCongruenceClosure`, whose
`assert_distinct` rescans every previously stored disequality. On Goel
`adding.1.prop1_ab_core_max`, a local model-enabled profile recorded 1.871 billion
representative calls: 73.81% of self samples in `representative`, 18.25% in
`check_disequalities`. Larger `adding` sessions exceeded the separate replay
budget before their first answer. Timing without model construction is preserved.

The original checklist was recorded before editing. Its endpoint-registration
premise was corrected during unit review: registration can eagerly propagate
congruences, and those merges already check all older pairs. The final checklist is:

- Keep the existing closure, disequality vector, conflict explanation and
  snapshot representation from [](euf). This is an invariant-based scan reduction,
  not a new optimized congruence-closure algorithm.
- Endpoint registration may process congruence merges; each such merge checks
  every older disequality. After registration, appending the new pair cannot
  change representatives. Check only this newly appended pair at that point.
- Every actual class merge still checks the complete disequality vector, so a
  previously consistent pair cannot become contradictory unnoticed. Saturation
  processes queued congruences using that same merge path.
- Preserve existing conflict ordering after merges, reason construction and
  push/pop state. A pre-existing conflict remains sticky until rollback.
- Expose use through the existing `disequality_checks` counter. Adding N pairs
  without merges must cost N pair checks, rather than N(N+1)/2. Class merges
  retain their current full-scan cost; no adjacency-index claim is made.
- Tests must check the linear insertion count, a newly contradictory pair,
  contradiction of an older pair after a merge, queued congruence and rollback.
  Run model-enabled native probes, unit/ASan/static checks, fresh scoped gates
  and the complete native qualification before accepting the fix.

The insertion fix reduces representative calls on the same instrumented
`adding.1` probe from 1,871,013,144 to 172,130,838. The new model counter reports
86,058,135 pair checks; actual merges still rescan older pairs. The larger
`adding.5` assumptions probe now completes both model-producing queries within
the local 120-second bound, with approximately 1.33 billion pair checks per
query. These are mechanism diagnostics, not qualified benchmark speedups.

## Active lowered roots for UF model construction

Fresh gate `142579`, seed `202639843`, exposed a model error already present at
`1595948`, independently of the disequality scan fix. The minimal assertion is
`(f (ite b a c))` for `f : U -> Bool`. Lowering assigns `f(aux)` true, but adding
the original application `f(ite b a c)` to the model closure invents an unrelated
Boolean completion. The two applications then evaluate at the same argument
and disagree. Rebuild already constructs from its final lowered goal.

Original repair checklist, recorded before implementation:

- `IncrementalUfEngine` retains a vector of active lowered assertion IDs and
  a stack of vector boundaries alongside selectors. These IDs borrow the same
  session term manager and include the guarded lowering side constraints.
- Append records each lowered root once. Push records the current root count;
  pop truncates to that boundary. Empty and nested scopes preserve the ordinary
  assertion-stack invariant. Historical SAT atoms and preparation remain allocated.
- Construct the UF model from these active lowered roots plus the complete
  retained theory assignment and visible symbol IDs. Do not seed uninterpreted
  applications over original unlowered arguments with invented class values.
- `SmtSession` still validates the resulting immutable model against the active
  original assertions, so this does not weaken the model acceptance condition.
  Evaluation of an original ITE argument uses the resulting function table.
- Test both Auto and Off, repeat/pop/reappend, saved models, and agreement of
  original-expression evaluation. Keep the minimized witness in the deterministic
  compute-node regression gate, and rerun fresh scoped/model qualification.

This changes the model builder's input roots only; it adds no rewriting rule,
SAT lemma or new congruence algorithm. The retained assignment remains the source
of theory facts. See [](model-interpretation) for final-goal model construction.

## Profiling follow-up

[](uf-z3-profiling) analyzes the qualified gap to Z3 with fresh local gprof and
per-query reference statistics. It identifies large pairwise `distinct`
expansion, search/preprocessing gaps and repeated preparation/state-copy costs.
The study changes no solver behavior or qualification status.

## Lead 22: atom-scan experiment checklist

This is an original scan-reduction design for [open lead 22](#lead-incremental-uf-scans),
recorded before implementation on `perf/incremental-uf-atom-scans`, based on
`605062f`. It uses the append-only identity and conservative-definition invariants
above; it introduces no new congruence algorithm. Acceptance is pending measurement
and fresh correctness qualification. Traversal scratch and structural copies are
separate, deferred experiments.

- `BooleanClausifier::Implementation` owns a numeric registration cursor into its
  append-only `theory_atoms_`. Terms and their children are immutable; registration
  does not depend on assertion polarity or whether the originating scope is active.
  After scanning one root completely, advance the cursor. Re-read registry size
  while scanning: registration may append Boolean argument atoms to the same vector.
- Keep the visited-term set local to each registration call. New roots may revisit
  shared subterms, but completed roots are not scanned again. This removes repeated
  root scans across checks without allocating a persistent visited set proportional
  to the retained term manager. It does not claim linear total DAG traversal across
  all future appends.
- Snapshot term IDs and child vectors before registration can grow the atom vector.
  No reference into that vector may survive registration. A repeated call without
  appended atoms visits zero roots. Pop never rewinds the cursor: definitions and
  historical atoms remain valid; reset destroys their owner and resets the cursor.
- `IncrementalUfEngine` owns a separate prepared-atom cursor. Pass only the new
  registry suffix as a read-only span to `PreparedFlatTheory::ensure_theory_atoms`.
  Retain that helper's arbitrary-batch/deduplication semantics for mixed-theory
  callers. Require one prepared atom per suffix entry before advancing the cursor.
- The prepared closure still installs every new flat definition. All atoms remain
  observed/protected, all required root assignments are replayed, and working
  closures remain fresh per query. Assignment alignment and model construction
  keep their current complete-registry inputs.
- Publish per-query registration-root and preparation-input counts. Repeated
  checks must report zero for both; new registration atoms must be included in
  preparation during the same check. Tests cover growth, retained terms reused
  after pop, redeclaration, SAT/UNSAT/unknown recovery and scoped Boolean arguments.
- Compare the five representative native sessions with the preserved baseline;
  gate broad evaluation on fresh scoped generated/native-seed tests. Before
  accepting, independently validate actual native SAT models and check the default
  route. Reject the experiment if scan counts fall without a repeatable session
  benefit, or if any unexplained solved-case loss or correctness failure remains.
