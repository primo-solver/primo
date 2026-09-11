# Assumption-based incremental QF_UF

Status: initial archive timing complete; a shared model-construction fix is
under qualification. The initial replay is incomplete.
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

## Native archive measurement at `1595948`

SLURM timing job **142464** replayed all 1,778 unchanged files through real stdin,
with 30 seconds per whole session and a 10,000 theory-conflict budget for both
Primo modes. The Release binary uses CaDiCaL `3.0.0-7b99c07`; the references are
Z3 5.1.0 and cvc5 1.3.4. Tasks ran serially on `node-02.ciircluster`, with 8 GiB
requested per task. The scheduler allocated two logical CPUs for each requested
single-CPU task. Each result retains direct-child peak RSS, stdout and stderr.

| Profile | Complete sessions | Timeout | Unknown | Mean whole-session PAR2 (s) | Complete-session queries |
| --- | ---: | ---: | ---: | ---: | ---: |
| Rebuild | 1,775 | 2 | 1 | 0.8223 | 29,573 |
| Assumptions | 1,772 | 0 | 6 | 0.4804 | 29,978 |
| Z3 | 1,778 | 0 | 0 | 0.1691 | 29,990 |
| cvc5 | 1,768 | 10 | 0 | 0.8313 | 29,970 |

There are zero answer disagreements among overlapping definite query answers,
zero crashes and zero parser errors. These are timing results; separate model
qualification is required below. An incomplete session receives 60 seconds PAR2
regardless of partial progress. Rebuild additionally printed 380 responses in
incomplete sessions; assumptions printed 12, including its unknown responses.
Those responses do not increase the complete-session count.

Among the 1,770 sessions completed by both Primo modes, the median rebuild /
assumptions runtime ratio is 2.952 and the geometric mean is 2.741. Assumptions
gains two complete sessions and loses five, for a net loss of three. The losses
are Goel `rushhour` cases reaching the query conflict limit, not incorrect
answers. Z3 remains faster overall and completes more sessions. Peak observed
solver RSS is 987 MiB rebuild, 903 MiB assumptions, 156 MiB Z3 and 194 MiB cvc5;
these maxima come from potentially different inputs and are not paired ratios.

| Family | Rebuild complete / mean PAR2 (s) | Assumptions | Z3 | cvc5 |
| --- | ---: | ---: | ---: | ---: |
| Goel (766) | 764 / 1.2415 | 760 / 0.9892 | 766 / 0.3378 | 756 / 1.7531 |
| CLEARSY (1,012) | 1,011 / 0.5049 | 1,012 / 0.0953 | 1,012 / 0.0414 | 1,012 / 0.1335 |

Reuse is observable on CLEARSY `0002/00126.smt2`: assumptions completes all 413
queries in 4.502 seconds, with one frontend, one engine, 413 working-state
resets, 2,551 translated assertions, 2,459 prepared atoms and 453 retired
selectors. Rebuild times out after 377 queries and frontend creations. Retained
storage at the last assumptions query includes 7,392 core terms, 7,750 flat
terms, 5,892 flat definitions and 15,135 active SAT clauses. This is persistent
frontend/SAT/preparation reuse, with fresh working theory state each query.

The largest completed-session regression is Goel
`QF_UF_firewire_tree.5.prop2_ab_core_max.smt2`: 11.966 seconds rebuild versus
20.436 assumptions. Its first assumptions query spends 14.932 seconds solving
and 3.913 seconds preprocessing, compared with 8.994 and 0.704 in rebuild.
The assumptions working-state reset costs 0.041 seconds. A local `-pg` profile
attributes self samples primarily to assignment callbacks (23.53%), SAT
propagation (22.79%), congruence merging (13.73%) and trail restoration (5.88%).
The reduced safe preprocessing and different retained search therefore matter;
working-state recreation alone does not explain the regression. The local
profile is diagnostic and does not replace SLURM timings. No unsafe whole-goal
preprocessing or silent fallback was introduced to recover these cases.

Fresh Auto gate **142459** passed 1,000 generated scoped sessions and 1,000 native
seed mutations, with 16,000 reference-checked queries and 9,458 direct Dolmen SAT
model passes. Earlier source-identical Auto/matrix jobs **142359/142364** passed
another 4,000 sessions and 18,908 models. The unchanged default passed the
25-file cross-logic preservation sample (**142369/142370**): 50 scoped sessions,
200 queries and 121 Dolmen models, with no lost solved files; median file-mode
runtime ratio against the previous accepted LRA-session build was 1.013.

Separate native validation job **142519** selects all complete Primo sessions:
24,746 rebuild SAT query models and 25,027 assumptions SAT query models. The initial replay encountered eight response
timeouts in model-heavy Goel `adding` sessions. Job **142567** retried affected
shards at 120 seconds; both runs were stopped after the bottleneck below was
profiled. Qualification of that binary is **incomplete**. The corrected
candidate requires a fresh replay. This stage checks actual incremental models, query-context Z3
answers, successful process exit, and exact agreement with the timed answer
sequence. Timeout/unknown sessions remain unqualified even if some queries
answered SAT. No full-session correctness claim applies to them.

Reproduction uses `scripts/slurm_native_uf_sessions.sbatch` and
`scripts/summarize_native_uf.py`; exact per-shard invocations, binary/input hashes,
reference versions and limits are retained in `run.json`. See `run-log.json`
entry `2026-09-11-assumptions-uf-native-1595948`. Timing artifacts are collected
under `results/uf-native-timing-1595948`; large model replay artifacts are kept
in the corresponding SLURM workspace until collection. The mode remains opt-in;
these mixed solve counts do not support changing the default.

## Model replay bottleneck and insertion checklist

Native replay at `1595948` exposed a pre-existing model-construction cost in
both modes: `ComponentModelBuilder` uses `IncrementalCongruenceClosure`, whose
`assert_distinct` rescans every previously stored disequality. On Goel
`adding.1.prop1_ab_core_max`, a local model-enabled profile recorded 1.871 billion
representative calls: 73.81% of self samples in `representative`, 18.25% in
`check_disequalities`. Larger `adding` sessions exceeded the separate replay
budget before their first answer. Timing without model construction is preserved.

The following original micro-optimization checklist was recorded before editing:

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
