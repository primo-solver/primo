# Incremental memory and reference counting

**Status: open lead; investigation not started. Owner: unassigned.**

Can Primo support long-lived incremental sessions and a library API without
reference-counted term handles? High memory use has been observed during
incremental work, but its causes and the appropriate reclamation policy need
measurement. Reference counting is a candidate, not a selected design.

## Evidence and current ownership

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
