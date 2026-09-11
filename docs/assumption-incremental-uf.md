# Assumption-based incremental QF_UF

Status: implementation and local tests present; SLURM qualification pending.
The design checklist below was recorded before implementation.
The default remains `rebuild`. The input archive is `sandbox/QF_UF.tar.zst`,
SHA-256 `52c55a17ee15e5c4a0fc723b9605ae42615da34499efc7e5a5e8b2c0ffc26a27`.
Its command-boundary inventory contains 1,778 sessions (766 Goel and 1,012
CLEARSY), 29,990 queries and 33,716 pushes/pops. Only 1,532 queries have a fresh
status annotation; remaining answers require reference comparison. Production
parsing must additionally verify the command stream.

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
- Model construction consumes the complete theory assignment and explicit
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
Investigate lost solves and significant regressions with phase counters and
profiling before deciding acceptance. Preserve the existing QF_LRA assumptions
and default cross-logic tests. Run check, ASan, lint, docs and required reviews/
static scans, record experiments in `run-log.json`, commit coherent milestones,
collect artifacts and audit all jobs/processes and the worktree before handoff.
