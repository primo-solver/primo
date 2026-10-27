# Incremental memory and reference counting

**Status: Open lead. Owner: unassigned.**

Historical session measurements, current owners, and two reproduced single-query
allocation failures are identified below. Live-versus-retained memory profiling,
current incremental reproduction, and reclamation design remain pending.

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
reports runtime results without measuring memory reduction.

{source}`TermManager <src/core/term_manager.hpp>` owns a term vector and a
hash-cons table. {source}`intern <src/core/term_manager.cpp#L540-L560>` appends
new terms and returns numeric IDs; the manager exposes no individual term-release
operation. The assumptions-mode
{source}`pop path <src/smt/smt_session.cpp#L294-L325>` removes active assertions
while retaining the frontend and engine. Popping a scope therefore does not
by itself establish that its term storage has been reclaimed. The existing
{source}`LRA retention note <docs/assumption-incremental-lra.md>` explicitly
defers automatic compaction of accumulated terms, atoms, clauses, and retired
scopes.

{source}`Goal <src/smt/tactic.hpp#L29-L57>` and
{source}`CheckSatOutcome <src/smt/check_sat.hpp#L210-L217>` already use shared
ownership for some objects. The open question concerns term/context lifetime
and reclaiming unused storage; it is not whether Primo uses any reference
counting at all. Define that boundary together with the
[library API](library-api.md).

(memory-exhaustion-candidates)=
## Current memory-exhaustion candidates

The [October 26 allocation traces](../evidence/long-limits.md) provide concrete
inputs for memory attribution. They used Primo `4419dc9`, the preserved binary
recorded in run `2026-10-24-all-theories-enforced-memory` in the
[run ledger](../run-log.json), default arguments `--quiet`, and an actual 8 GiB
address-space limit per solver process. These are non-incremental, single-query
cases; they extend this lead to storage retained within one solve and do not
reproduce accumulation across `push`/`pop` cycles.

| Input | Observed allocation failure | Investigation role |
| --- | --- | --- |
| UF: `sledgehammer/FFT/uf.706467.smt2` | Term-vector growth in `TermManager::intern`, reached through quantifier enumeration and instantiation | Direct candidate for term-storage and term-lifetime profiling. |
| QF_LIA: `nec-smt/large/getoption_user/prp-23-48.smt2` | Binary-clause vector growth during SAT preprocessing | Comparison case for attributing core versus backend memory and temporary allocation peaks. |

The UF trace makes [compact term storage](term-storage.md) and reclamation worth
investigating, but does not show that terms were dead or reclaimable. Measure
which generated terms remain needed by active formulas, candidate pools, queued
or retained lemmas, SAT/theory mappings, explanations and model construction.
Also identify terms retained only to avoid regenerating earlier work, and
measure the cost of releasing and later reconstructing that work. Reference
counting alone would not reclaim objects still retained by such caches; their
ownership and retention policy are part of the question.

The {ref}`paired UF measurement <uf-rollback-resource-measurement>`
on October 27 reruns the saved FFT case and a second explicit-allocation-failure
case, `sledgehammer/Fundamental_Theorem_Algebra/uf.828004.smt2`, under the same
8 GiB address-space cap. Removing dormant EUF rollback arrays recovers neither
case and leaves their virtual-memory peaks essentially unchanged. These remain
useful attribution candidates; the rerun does not establish whether retained
terms are dead, live, or beneficially cached.

(simplified-instance-retention)=
### Raw instances retained after simplification

A specific risk is constructing a large ground instance, simplifying it to a
much smaller formula, and retaining the original term graph. The current
{equalityexperiment}`add_instance path <src/theory/quantifiers/quantifier_component.cpp#L212-L247>`
records the original instance ID in `raw_instances_` before calling the
simplifier. `canonical_instances_` records the simplified result, while
{equalityexperiment}`InstanceSimplifier::memo_ <src/theory/quantifiers/instance_simplifier.cpp#L22-L65>`
remembers original-to-simplified term IDs for the simplifier's lifetime. The
instance caches and enumeration records
{equalityexperiment}`survive SAT backtracking <src/theory/quantifiers/quantifier_component.cpp#L72-L73>`.
A normalized duplicate or trivial instance can therefore add no useful lemma
while its previously constructed nodes remain in the manager.

These caches hold numeric IDs, not owning term handles. Today the
`TermManager` itself retains the nodes and hash-cons entries until destruction;
clearing a cache alone would not reclaim them. A future reference-counting or
collection design must specify whether cache entries keep original terms alive,
use non-owning entries with safe invalidation, or can be discarded and rebuilt.
Keeping strong owners for every raw instance and simplifier key would preserve
this retention even after adding reference counting. Hash-consing shares common
subterms, so retention does not imply two independent full copies of a formula.

On the UF candidate, measure nodes and bytes created during instantiation,
those reachable from the simplified result, and the remainder retained only
through the manager or caches. Exclude nodes also needed by other live roots
and avoid counting shared subterms repeatedly. Correlate these measurements
with simplified, normalized-duplicate and trivial instances; also separate
memo-table bytes from term storage. This tests whether substantial
post-simplification retention actually occurs on the failing input before
choosing reclamation or [fewer generated instances](quantifier-instantiation.md).
The code establishes the retention mechanism, but not its contribution to this
memory-out or whether the caches can safely be cleared.

For both inputs, attribute the full footprint to core nodes, hash-cons keys,
child/payload allocations, quantifier state, SAT/theory state, allocator
retention and transient vector growth. The QF_LIA failing allocation is in
backend storage; that location neither identifies the largest existing owner
nor establishes a need for core-term reference counting. The subsequent
external-propagator cleanup abort is a separate error-handling defect, not
evidence that a collection policy would recover enough memory.

## Lifetime of flattened EUF terms

Reference counting for core terms would also require a policy for the
[flattened EUF representation](../implementation/euf.md#representation-and-ownership).
{source}`FlatTermIndex <src/theory/equality/flat_terms.hpp>` stores core `TermId`s
in its maps and original-term fields, alongside separately allocated flat nodes
and definitions. {source}`PreparedFlatTheory <src/smt/solver_engines.cpp#L893-L913>`
retains this structure, and working closures can copy it. Reclaiming a core
term would not automatically reclaim its flattened representation.

The ownership design must answer:

- Should the core-to-flat maps and original-term fields keep core terms alive,
  or be non-owning references that must be invalidated before reclamation?
  Retaining every mapped term could defeat the intended memory saving.
- What keeps a flat node alive? Shared curried prefixes, definitions, prepared
  and working closures, atom mappings, explanations, and undo records must all
  be considered. Dropping the last caller handle to a core term is insufficient
  grounds to delete a flat node still used by theory state.
- Should flat nodes have their own reference counts, use another collection
  policy, or be reclaimed by rebuilding the index and dependent state at safe
  boundaries? Any reuse or remapping of `FlatTermId`s must preserve live
  references, including those in retained SAT/theory mappings.

Measure core and flattened storage separately during repeated scope creation
and retirement, including prepared structures and working copies.

## Reference counting precedent and GC risks

Z3 and cvc5 provide relevant reference-counting precedents:

- Z3's `Z3_mk_context_rc` API uses explicit AST retain/release operations.
  Its API also offers a context-lifetime mode for ASTs. See the
  [Z3 context and AST reference-counting documentation](https://z3prover.github.io/api/html/group__capi.html).
- cvc5 documents owning `Node` handles that maintain reference counts and
  non-owning `TNode` handles. Nodes with no remaining owners become eligible
  for reclamation, which may be deferred and batched. See the
  [cvc5 developer guide](https://github.com/cvc5/cvc5/wiki/Developer-Guide#user-content-dealing-with-expressions-nodes-and-tnodes).

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
and maintenance cost as well as reclaimed memory.

## First investigation

- Reproduce high-memory sessions on the candidate build, recording exact input,
  options, revision, limits, and query/scope position. Compare rebuild and
  assumptions modes on the same inputs. Include repeated
  `push`/assert/`check-sat`/`pop` cycles with fresh terms, a bounded active
  problem, and runs with and without caller-retained models or handles.
- Profile the single-query candidates above alongside the incremental cases.
  Measure unreachable storage, deliberately cached state and indispensable live
  data separately; record address-space/capacity peaks as well as RSS.
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
