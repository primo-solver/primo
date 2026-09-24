# Incremental memory and reference counting

**Status: open lead; investigation not started. Owner: unassigned.**

Can Primo support long-lived incremental sessions and a library API without
reference-counted term handles? High memory use has been observed during
incremental work, but its causes and the appropriate reclamation policy need
measurement. Reference counting is a candidate, not a selected design.

## Evidence and current ownership

The [core chapter](../implementation/core.md) explains the storage and lifetime
contracts that this investigation may need to change.
The separate [compact term storage lead](term-storage.md) covers bytes per
node, duplicated interning payloads, and future leaf data. A more compact node
would not by itself reclaim terms retained by an incremental session.

The [theory-lemma forgetting lead](theory-lemma-forgetting.md) covers retained
SAT clauses and their explanation lifetimes. Include those stores in memory
attribution before choosing a term reclamation policy; they have different
owners and correctness obligations.

The {source}`historical QF_UF session evaluation <docs/assumption-incremental-uf.md>`
at `fa2fe5c` records peak solver RSS of 898 MiB in assumptions mode and 990 MiB
in rebuild mode, over 1,778 sessions with a 30-second whole-session limit and
10,000 theory conflicts per query. These are maxima across potentially different
inputs, not a paired memory comparison or a measurement of the current code.
The {source}`later atom-scan evaluation <docs/incremental-uf-atom-scans.md>`
also explicitly makes no memory-reduction claim.

{source}`TermManager <src/core/term_manager.hpp>` owns a term vector and a
hash-cons table. {source}`intern <src/core/term_manager.cpp#L540-L560>` appends
new terms and returns numeric IDs; the manager exposes no individual term-release
operation. The assumptions-mode
{source}`pop path <src/smt/smt_session.cpp#L295-L326>` removes active assertions
while retaining the frontend and engine. Popping a scope therefore does not
by itself establish that its term storage has been reclaimed. The existing
{source}`LRA retention note <docs/assumption-incremental-lra.md>` explicitly
defers automatic compaction of accumulated terms, atoms, clauses, and retired
scopes.

{source}`Goal <src/smt/tactic.hpp#L29-L57>` and
{source}`CheckSatOutcome <src/smt/check_sat.hpp#L233-L240>` already use shared
ownership for some objects. The open question concerns term/context lifetime
and reclaiming unused storage; it is not whether Primo uses any reference
counting at all. Define that boundary together with the
[library API](library-api.md).

## Reference counting precedent and GC risks

Z3 and cvc5 provide relevant reference-counting precedents:

- Z3's `Z3_mk_context_rc` API uses explicit AST retain/release operations.
  Its API also offers a context-lifetime mode for ASTs. See the
  [Z3 context and AST reference-counting documentation](https://z3prover.github.io/api/html/group__capi.html).
- cvc5 documents owning `Node` handles that maintain reference counts and
  non-owning `TNode` handles. Nodes with no remaining owners become eligible
  for reclamation, which may be deferred and batched. See the
  [cvc5 developer guide](https://github.com/cvc5/cvc5/wiki/Developer-Guide#dealing-with-expressions-nodes-and-tnodes).

These are comparison points for the investigation. Primo's ownership contract
and implementation must follow its own design and measurements. Reference
counting and the timing of reclamation are separate decisions.

Tracing garbage collection during incremental sessions is another candidate,
with substantial design risk. Even collection between queries needs a complete
account of live roots beyond the active assertions: external handles, retained
SAT/theory mappings, explanations and backtracking state, reconstruction data,
and caches. Collection must coordinate hash-cons cleanup and ID validity with
those owners. A compacting collector would additionally need to preserve or
update every live reference when moving storage.

The design must identify safe collection points, specify which callbacks or
borrows can remain live, and bound collection pauses. Evaluate implementation
and maintenance cost as well as reclaimed memory. GC for incremental sessions
remains an open alternative; no collector or reference-counting scheme is
selected.

## First investigation

- Reproduce high-memory sessions on the candidate build, recording exact input,
  options, revision, limits, and query/scope position. Compare rebuild and
  assumptions modes on the same inputs. Include repeated
  `push`/assert/`check-sat`/`pop` cycles with fresh terms, a bounded active
  problem, and runs with and without caller-retained models or handles.
- Measure current RSS over time as well as peak RSS, live versus retained term
  counts, hash-cons entries, symbols, clauses, theory data, caches, and scratch
  capacity. Peak RSS alone cannot show reclamation; allocator retention and
  useful retained solver state must be separated from unreachable objects.
- Identify every root that keeps terms alive: caller handles, assertions,
  preprocessing/reconstruction data, theory explanations, SAT mappings, and
  caches. Decide what remains valid after pop, reset, and context destruction.
  Hash-cons tables must not unintentionally keep every otherwise-dead term alive.
- Compare reference counting, tracing/reclamation at safe boundaries, and
  periodic rebuilding or compaction. Measure allocation and solve-time overhead,
  reclaimed bytes, and interaction with useful retained search state. Any ID
  reuse or remapping must preserve live references and detect stale handles.

## Decision and completion

Introduce per-term reference counting only if it addresses a demonstrated
retention or API-lifetime problem with acceptable cost. If another policy meets
the same lifetime and memory requirements, record that decision and why
reference counting was unnecessary. A profile dominated by live terms or
non-term allocations would weaken the case for term reference counting.

Close the investigation with a measured ownership/reclamation design or an
explicitly justified deferral. An implementation needs regressions for scoped
growth and retained handles/models, sanitizer checks, the applicable
[fuzz gate](../development/correctness.md), and separate model validation for
full evaluations. Record memory and runtime results in the run ledger before
claiming an improvement.
