# EUF And Congruence Closure

Primo's first theory target is QF_UF. The default implementation is the
incremental Nieuwenhuis-Oliveras-style congruence-closure engine over flat
terms. The older lazy EUF solver over `core::TermId` values is still available
with `--euf-engine=lazy` for diagnostics.

## Current Solver Path

SMT-LIB assertions are parsed into hash-consed terms. Boolean structure is
clausified into SAT clauses. Equality atoms and Boolean predicate applications
are tracked as theory atoms.

The QF_UF loop in `src/smt/check_sat.cpp` is:

```text
clausify assertions
set the default SAT phase of tracked theory atoms to false
loop:
  call CaDiCaL
  if SAT says UNSAT, return UNSAT
  read assigned theory atoms
  rebuild congruence closure for the assignment
  if all assigned disequalities are consistent, return SAT
  add a blocking theory-conflict clause and repeat
```

The current conflict clause is justified by the explained congruence closure.
Reasons are sound but not guaranteed minimum; the optimized proof-forest
structure from the fast congruence-closure literature is still deferred.

Before invoking EUF checks, Primo applies T-pure-literal filtering in the sense
of Sebastiani's lazy SMT survey, §6.10. Polarity is collected for original
theory atoms during Boolean abstraction. If an atom occurs only positively, a
false SAT assignment of that atom is omitted from the EUF assignment; if it
occurs only negatively, a true SAT assignment is omitted. Mixed-polarity atoms
are kept. This is not the SAT pure-literal rule: Primo does not force a value
for pure theory atoms and does not remove their SAT clauses. It only avoids
feeding theory literals to congruence closure when those literals are irrelevant
to satisfying the Boolean abstraction.

`--pure-theory-filter=auto|on|off` controls this filtering. The current `auto`
policy is the same as `on`; `off` is intended for ablation runs and for
benchmark families where filtering removes useful early theory conflicts.

Filtered literals are also omitted from model-time propagation premises and
from the incremental external-conflict closure. Candidate theory propagations
whose candidate literal would be filtered are skipped. This follows the same
caveat from the lazy SMT literature: filtered literals must not be used as
theory-deduced premises, because they were deliberately not passed to the
theory solver. The implemented subset is assignment filtering for QF_UF equality
atoms, Boolean predicate applications, and Boolean constants represented as EUF
terms; ghost-literal decision avoidance is not implemented.

The SAT phase setting is a search heuristic only. It asks CaDiCaL to try the
negative phase for equality and Boolean-predicate theory atoms before the SAT
solver's own activity heuristics override that choice. This is useful for
finite-domain QF_UF encodings with many disequality constraints and value-choice
disjunctions, but it does not assert or propagate any theory fact.

`--euf-engine=incremental|lazy` selects the EUF checker used for final conflicts
and propagation reason queries. `incremental` is the default. The incremental
engine is conservative: it uses the paper-style flat data structures, and
model-time propagation (`--euf-prop=model`) still rebuilds equality state from
the complete SAT assignment. When `--euf-prop` selects `conflict` or `online`,
the final per-solve-call conflict check reuses the persistent flat closure that
the CaDiCaL external propagator already kept in sync with the trail, instead of
rebuilding an equivalent closure from the complete assignment; this closure's
conflict state is authoritative once `backend.solve()` reports SAT, because
every observed-variable assignment was asserted into it synchronously via
`notify_assignment`, and any conflict is delivered back to CaDiCaL through
`cb_has_external_clause` before search can settle on a full model. `off` and
`model` propagation modes have no persistent propagator-owned closure, so their
final conflict check still rebuilds from the complete assignment.

`--euf-prop=auto|off|conflict|model|online`,
`--euf-prop-budget=auto|unlimited|N`, and
`--euf-prop-delivery=auto|unlimited|N` are the public option shape for callback
and propagation work. The default is `auto`: small EUF abstractions use online
callbacks, and large finite-domain-table shapes also use online callbacks when
preprocessing has preserved at least 500 finite-domain choices/domains and the
number of pure-negative theory atoms is no larger than the finite-domain count.
Other large abstractions use conflict-only callbacks. This avoids the ordinary
post-model conflict loop on Goel-style instances with thousands of EUF atoms
without enabling the currently expensive online propagation scan on low-domain
PEQ/QG shapes where local profiling showed little or no delivered propagation.
Use `off` to force the lazy post-model loop, `conflict` to force only external
conflict clauses, and `online` for explicit propagation experiments. The default
delivery budget is `1000`, which keeps the predecessor benchmark knob available
when online propagation is selected.

For compatibility with the predecessor benchmark command, Primo also accepts
`--prop-delivery-budget N`; `0` means unlimited delivery. `--prop-interval N`
and `--prop-assign-threshold X` are accepted for online propagation experiments.
Their defaults are `32` and `0.25` respectively.

`--theory-phase=negative|positive|none|polarity|auto` controls the initial
CaDiCaL phase set for theory atoms before solving. The default is `negative`,
which tries disequality assignments first and preserves the current validated
QF_UF benchmark profile. The `auto` mode is an experimental static heuristic
that keeps the negative phase unless preprocessing found finite-domain or
UF-symmetry structure, at most 25 theory atoms occur only negatively, and at
least 100 theory atoms occur only positively. This low-negative guard covers
some NEQ/SEQ-style rows where setting every theory atom negative causes many
avoidable SAT assignments and EUF callbacks, while avoiding broader PEQ/QG
regressions seen when all balanced finite-domain shapes used polarity. When
the heuristic selects polarity, it reports `:sat.phase.auto-polarity`.

The other modes remain available for ablation and tuning: `positive` tries
equality assignments first,
`none` leaves CaDiCaL's default phase untouched, and `polarity` sets a phase only
for atoms that occur with a single syntactic polarity in the clausified formula.
This is a local search-shaping heuristic derived from the NEQ/PEQ/QG/SEQ
performance trail in `docs/run-log.json`; it is not a theory decision callback
and it does not implement a paper-specific SAT branching algorithm.

When QF_UF preprocessing selects the small multi-assertion `basic` branch, Primo
sets syntactically single-polarity theory atoms according to that polarity and
reports `:sat.phase.auto-basic-multi-polarity`; the phase override can be
disabled independently with `--no-auto-basic-multi-polarity`. The
single-assertion `basic` branch has a narrower variant for finite-domain
formulas that contain
Boolean-valued UF predicate applications; it reports
`:sat.phase.auto-basic-single-predicate-polarity`. Any explicit
`--theory-phase=...` value disables these automatic overrides. These remain
static CaDiCaL phase hints set before solving; Primo still does not use an EUF
decision callback that dynamically chooses the next SAT decision literal.

`conflict` connects the CaDiCaL external propagator only for assignment,
backtrack, and external conflict clauses. It maintains an incremental flat
congruence closure along the SAT trail, learns EUF conflict clauses through
CaDiCaL's external-clause callback, and does not deliver theory propagations.
It is the default large-abstraction branch of `--euf-prop=auto` unless the
finite-domain-domain threshold above selects online propagation. The current
backtracking implementation still trails ordinary mutable closure state rather
than using the paper's timestamped lookup structure, so the large finite-domain
online trigger is an empirical NEQ-family heuristic recorded in
`docs/run-log.json`, not a paper-specific optimized congruence-closure
algorithm.

The callback path was first changed to `conflict` after the full QF_UF SLURM run recorded in
`results/slurm-qfuf-full-109880/summary.md`: at a 2s timeout it solved 5455 of
7503 files with no wrong answers, compared with 4778 for the previous
phase-negative/default baseline. The result is mixed by family: QG gained 684
solved files, while Goel lost 21 solved files, so family-level reports should
remain part of future tuning.

The legacy preprocessing switches were promoted to defaults after the full
QF_UF 10s comparison in SLURM jobs 109889, 109890, and 109891. The safe
configuration at that point, using `conflict` callbacks plus `--uf-symmetry-breaking`,
`--preprocess-passes 4`, `--eq-bridge`, `--prop-interval 32`,
`--prop-assign-threshold 0.25`, and `--prop-delivery-budget 1000`, solved 6647
of 7503 files with no wrong answers. The previous default solved 6611.

Online propagation was then fixed to cache reason clauses by signed propagated
literal and keep those global clauses across SAT backtracking. Before that fix,
SLURM job 109891 had 18 errors and one wrong answer. After the fix, matching
SLURM job 109913 solved 6684 of 7503 files at 10s with no errors and no wrong
answers. Later profiling on Goel firewire instances showed the callback
backtracking path was still too expensive on large abstractions, so the default
became size-adaptive rather than unconditionally online.

`model` is implemented as a conservative post-model lemma learner. After each
SAT model, Primo rebuilds EUF closures that omit one candidate theory atom at a
time. If the remaining assigned equalities and Boolean predicate facts already
imply the candidate's current value, Primo adds a SAT clause from the explained
premise literals to the candidate literal. With `--euf-engine=lazy`, this uses
the older explained closure. With `--euf-engine=incremental`, it uses the flat
curried incremental closure and exports the `:euf.incremental.*` counters for
that propagation work. Reasons are sound, but not guaranteed minimum.

`online` connects a CaDiCaL external propagator through the SAT adapter. It
observes only EUF-relevant SAT variables: equality atoms and Boolean predicate
applications. The propagator tracks assignments and backtracking notifications,
then asks for entailed unassigned theory literals and their reason clauses.
The observed-variable set and theory-atom list are fixed after clausification,
so the callback stores observed-variable marks, variable-to-theory-atom
pointers, current assignments, pending-propagation marks, and queued-candidate
marks in vectors indexed by SAT variable or theory-atom index. Rollback uses
the same literal trail as before: `notify_backtrack` clears assignment and
pending slots for popped trail literals, and candidate queues are scratch state
cleared on backtrack. This replaces hot hash tables in assignment callbacks; it
does not add theory-aware SAT branching or change the set of learned theory
clauses.
With `--euf-engine=lazy`, reason queries use the older explained closure. With
`--euf-engine=incremental`, they use a persistent flat curried closure owned by
the external propagator. The propagator checkpoints assignment-trail positions
when SAT enters a new decision level and creates a flat-closure checkpoint
lazily, immediately before the first theory atom is asserted at that level. On
backtrack it restores the first checkpoint among the discarded levels, walks a
sparse undo trail for representative, class, use-list, disequality, edge, and
lookup mutations, and trims the assignment trail. This avoids replaying every
retained assignment after each backtrack and avoids copying the closure for
decision levels that never touch theory state, but it is still an
implementation-local trail rather than the paper's timestamped lookup undo
structure.
The trail entry itself is kept scalar for the common undo records. Full vector
snapshots for use-list and disequality-list restore operations live in separate
checkpointed payload arrays so ordinary parent, class-size, adjacency, lookup,
and pop records do not each carry unused vector members.
The closure's detailed counters are rollback-sensitive because checkpoints
restore the closure state. The online assignment state therefore accumulates
counter snapshots before rollback can erase them and once more when final stats
are flushed. With live timeout stats enabled, it also publishes periodically
after batches of assignments. This keeps timeout diagnostics fresh enough for
benchmarking without copying the whole counter block on every assignment
notification.
For the incremental engine, online propagation no longer starts every discovery
round with a blind scan of all theory atoms. The flat congruence closure records
flat terms whose equivalence-class representative changed during merges. The
SAT callback maps those changed flat terms to equality and Boolean-predicate
theory atoms that mention them, tries those candidates first, and only falls
back to the full atom scan when the changed-term queue is empty or unproductive.
The explanation builder remains the soundness gate: a queued candidate is
delivered to CaDiCaL only when the current flat closure can explain the implied
literal under the current SAT assignment.

When `--euf-prop=auto` selects the online path, zero-delivery propagation rounds
adaptively increase the callback discovery interval up to 1024 rounds. Explicit
`--euf-prop=online` keeps the configured `--prop-interval` cadence for
experiments.
A July 20, 2026 local experiment tried the predecessor-style policy of backing
off after every auto propagation scan, including productive scans. It reduced
candidate scans on the 51-row local NEQ/PEQ/QG/SEQ micro-manifest, but PAR2
slightly worsened and nearby solved rows regressed, including `NEQ023_size7`,
`PEQ002_size7`, `PEQ016_size7`, and `iso_icl_repgen006`. The change was
reverted; the current auto policy still resets the interval after a delivered
propagation.
This is still an initial online subset: it does not choose decisions. Like
`conflict`, it can serve EUF conflict clauses through CaDiCaL's external-clause
callback.

## Term Interface

The equality solver receives only `core::TermId` values. It inspects structure
through `core::TermManager::get`; it does not own term memory and it does not
refer to SMT-LIB syntax.

Syntactic equality and EUF equality are distinct:

- equal `TermId` values mean the terms are structurally identical after
  hash-consing and local simplification;
- EUF equality is the congruence relation computed from assigned equality
  literals.

## Congruence Closure

`src/theory/equality/congruence_closure.*` stores:

- a `UnionFind` over term ids;
- `terms_seen_`, the registered terms and their children;
- `seen_`, a term-id-indexed membership bitmap;
- a dirty flag used by saturation.

`add_term(t)` recursively registers `t` and its children. `assert_equal(a, b)`
registers both sides and merges their classes. `are_equal(a, b)` saturates the
closure and then compares representatives.

The saturation algorithm is intentionally simple:

```text
while a merge happened:
  for every pair of seen terms:
    if they are not already equal and are congruent:
      merge them
```

Two terms are considered congruent when they are both constants or applications
with the same symbol and their corresponding children are already equal. This is
quadratic over seen terms and rebuilds from each SAT assignment; it is adequate
for the scaffold but not the final QF_UF engine.

`src/theory/equality/explained_congruence_closure.*` is the first
explanation-capable EUF state. It keeps a union-find plus an explicit equality
edge graph. Explicit assigned equalities add edges labelled by the corresponding
SAT blocking literal. Congruence merges add edges labelled by the union of child
equality explanations. This is not the optimized proof-forest structure from
Nieuwenhuis and Oliveras, but it provides independently checkable reason
literals for the current lazy conflict clauses and propagation lemmas.

`src/theory/equality/incremental_congruence_closure.*` is a direct `TermId`
prototype of the Section 3 data structure:

- representatives in `parent_`;
- class lists and class sizes in `class_members_` and `class_size_`;
- affected-application use lists in `use_lists_`;
- sparse signature lookup in `lookup_`;
- pending explicit and congruence merges in the `pending_` worklist;
- disequality conflict checks in `disequalities_`, with per-representative
  index lists in `disequality_lists_`;
- non-minimal explanation paths in `edges_`;
- sparse `push`/`pop` checkpoints with an undo trail for backtracking tests and
  online propagation.

`src/theory/equality/flat_terms.*` and
`src/theory/equality/flat_congruence_closure.*` are the SMT-facing incremental
path. `FlatTermIndex` curries applications into binary internal apply
definitions and maps original equality atoms to flat endpoints.
`FlatCongruenceClosure` runs the same representative, class-list, use-list,
lookup, pending-merge, disequality, explanation, and sparse backtracking
structure over those flat definitions. Explanation edges are stored both in
insertion order and in per-flat-term adjacency lists so reason BFS visits only
edges incident to the current proof-graph node. Congruence edges store the two
flat definitions that collided in the lookup table and expand their child
equalities only when a conflict or propagation explanation asks for the reason.
Recursive expansion is causal: when a congruence edge is explained, child
equality explanations ignore equality-graph edges added at or after that
congruence edge. This keeps premise explanations tied to facts that existed
when the congruence merge was created. Rollback is sparse: a class merge records
the moved suffix of the kept class list once, and that same trail entry restores
the eager `parent_` representative for every moved member. Use-list swaps,
lookup insertions, disequality-list changes, edge adjacency, proof-parent state,
and class sizes keep separate trail entries because their lifetimes do not align
with the moved class suffix.
when the congruence merge was produced and avoids recursive BFS paths through
later proof edges. The diagnostic counter
`:euf.incremental.explain-causal-edge-skips` reports how often recursive
explanation rejected such later edges. The per-reason repeated-pair cache also
stores the smallest edge bound under which a pair was proved; a cached pair is
only reused for an equal or looser bound. Stricter recursive premise queries
therefore recompute instead of reusing a path that may mention later proof
edges. `:euf.incremental.explain-cache-bound-misses` counts these rejected
cache hits.
Each flat result has at most one definition inside `FlatCongruenceClosure`.
Exact duplicate definitions are ignored, and conflicting duplicate result
definitions are rejected as invariant violations. This keeps lookup witnesses,
use-list entries, and congruence-edge explanations tied to one flat application
signature.
Once a theory conflict is recorded, flat-closure propagation stops draining
pending merges for the current check. Those merges cannot change the current
theory answer and continuing would only add class, use-list, explanation-edge,
and trail mutations that will be discarded by SAT backtracking.
Use-list appends do not scan the destination vector for duplicates. Duplicates
can arise when the same flat definition mentions two classes that later merge,
so the closure removes duplicate affected results with a generation-stamp
scratch vector when a disappearing representative's use list is processed.
Rollback only restores semantic state; the stamp vector is recomputable
scratch state. The use-list public counters are
`:euf.incremental.use-list-appends`,
`:euf.incremental.use-list-duplicate-skips`,
`:euf.incremental.affected-use-list-dedup`,
`:euf.incremental.max-use-list-size`,
`:euf.incremental.trail-entries`, and
`:euf.incremental.class-member-trail-entries-saved`.
The explanation-shape counters
`:euf.incremental.explain-top-level-queries`,
`:euf.incremental.explain-recursive-queries`,
`:euf.incremental.explain-repeated-pair-queries`, and
`:euf.incremental.explain-max-recursion-depth`, together with
`:euf.incremental.explain-cache-bound-misses`, are diagnostic only: they measure
the current BFS explanation path and do not implement or select a proof-forest
reason engine.
`--euf-explain-search=one-sided|bidirectional|proof-forest` selects how the
incremental flat closure builds explanations. The default `one-sided` mode
preserves the validated reason shape by searching the current causal
equality-edge graph. The experimental `bidirectional` mode expands from both
requested endpoints and can reduce BFS visits on some NEQ/PEQ rows, but it
changes selected proof paths and has known PEQ regressions in the local run log.
`proof-forest` is a default-off Section 5.2 experiment: it maintains the
rollback-aware proof-parent forest, then answers explanations with a
per-explanation auxiliary union-find, `HighestNode` jumps, and a pending-proof
worklist. Each proof-parent edge stores the original endpoint in the merged
class; when that endpoint is not the disappearing representative, the explainer
adds a bounded endpoint-detour obligation before using the edge label. This is
still not a default because it uses Primo's existing representative-oriented
proof forest rather than the full reoriented Section 4 forest, and local matrix
runs show mixed reason quality and search effects. It is a tuning and research
knob until broader fuzzing and SLURM evidence justify a profile.
`--euf-proof-forest-diagnostics` maintains a rollback-aware proof-parent forest
beside the current explanation graph and records path-length counters for
cache-miss explanation queries. The flag does not change emitted conflict or
propagation clauses. It is a measurement tool for deciding whether a later
Section 4-5 proof-forest explanation engine is worth implementing for a given
family. The diagnostic also classifies each measured proof-forest path as
shorter than, equal to, or longer than the BFS path that actually emitted the
current reason. Diagnostic parent links store the merge-edge index and obey the
same causal edge bound as recursive BFS explanations. Local probes show that
proof paths can be shorter on some NEQ rows but longer on `PEQ002_size7`,
`SEQ035_size6`, and `iso_icl_repgen006`.
Primo currently steers SAT search for EUF atoms through initial phase selection
and external propagation/conflict clauses, not through a theory decision
callback. A July 20, 2026 clean-room inspection of the local reference checkout
found that its combined propagator exposes `cb_decide`, but the EUF solver in
that checkout does not override the default no-op decision callback. This makes
EUF decision hints a possible future experiment, but not an identified
reference-behavior gap for the current NEQ/PEQ investigation.
Online-mode checkpoints are created lazily on the first theory assignment in a
SAT decision level, so levels with only Boolean search do not allocate closure
rollback state. Checkpoints remember vector sizes, conflict state, stats, and
the current trail length; the trail records only mutations to representatives,
class sizes and members, use lists, disequality lists, explanation adjacency,
and lookup maps. Destination class-member lists are rolled back with one compact
merge record containing the kept representative, restored representative, and
moved-member count; rollback moves the appended tail back after later merges
have already been undone. This preserves the same backtracking state while
avoiding a full loser-list copy on merge-heavy finite-domain problems.
Changed-term notifications are a consumed queue for online propagation
candidate discovery: `drain_changed_terms` clears the queue and lowers active
checkpoint baselines so a later pop can discard post-checkpoint notifications
without resurrecting notifications already delivered to the SAT callback.
Self-applications whose function and argument are already in the same class are
registered once in the affected use list; duplicate use-list entries remain
tolerated and are deduplicated when an affected representative is processed.
The BFS marks, parent-edge arrays, queue storage,
per-recursion path buffers, and reason-literal marks are owned by the closure
and reused across explanation queries; signed SAT literals are marked
separately, so `p` and `not p` can both occur in a non-minimal reason when
different proof edges justify them. Congruence-edge expansion is checked
recursively, and a direct disequality conflict fails closed if the closure can
no longer explain the equality it just detected. A newly asserted disequality is
checked immediately. After a class merge, older disequalities are not scanned globally;
the closure collects the disequality indices attached to the two merged
representatives, sorts those indices to preserve original insertion order, and
checks only that candidate set. This keeps the first reported conflict
deterministic while avoiding scans over disequalities whose endpoint classes did
not change. This is the engine used by `--euf-engine=incremental`.

## EufSolver

`src/theory/equality/euf_solver.*` is a batch checker:

- `assert_equal(lhs, rhs)` records an equality;
- `assert_distinct(lhs, rhs)` records a disequality;
- `is_satisfiable()` rebuilds a `CongruenceClosure`, applies equalities, and
  rejects the assignment if any recorded disequality becomes equal.

The diagnostic lazy SMT loop uses `ExplainedCongruenceClosure` so conflict and
propagation clauses carry reason literals rather than all assigned theory
premises.

## Preprocessing Interaction

The CLI default `--qf-uf-preprocess=auto` pass runs before QF_UF solving. On
all assertion shapes it runs equality bridging, finite-domain at-most-one
clauses, SMT symmetry restrictions, and unit-equality simplification when
their recognizers can prove the required local conditions, but skips
finite-domain equality-definition expansion. The explicit `strong` preset adds
finite-domain equality definitions for experiments. `--qf-uf-preprocess=basic`
keeps only the unit-equality simplification subset, and `off` disables this
loop.
`--ground-congruence` additionally enables bounded ground-congruence clauses.
These generated assertions are then handled by the same Boolean clausifier and
congruence-closure loop as ordinary user assertions. `--uf-symmetry-breaking`
and `--eq-bridge` remain available as narrower experimental switches.

See [](preprocessing.md) for the finite-domain symmetry-breaking design.

## Implemented And Deferred

Implemented:

- union-find over `TermId`;
- recursive term registration;
- congruence closure for constants and applications;
- equality and disequality checking;
- explanation graph for explicit and congruence equality reasons;
- deterministic Curry-style flat definitions at the equality-theory boundary;
- opt-in use-list, signature-lookup, pending-worklist congruence closure for
  final conflict checks;
- order-preserving flat disequality scans over per-class candidate lists;
- intersection-only disequality checks when two flat classes merge, so a merge
  only retests disequalities whose endpoints were in the two merged classes;
- scratch-reused flat equality explanations with signed-literal reason
  de-duplication and a per-explanation memo for repeated sub-equality
  expansions;
- sparse flat-closure backtracking over representative, class, use-list,
  disequality, edge-adjacency, and lookup mutations;
- changed-flat-term tracking for online propagation candidate discovery, with a
  full-scan fallback;
- model-time propagation using the flat incremental closure when
  `--euf-engine=incremental` is selected;
- online propagation with a SAT-callback-owned persistent flat closure when
  `--euf-engine=incremental` is selected;
- incremental Boolean/EUF integration with CaDiCaL, with the older lazy engine
  kept as a diagnostic option;
- Boolean predicate applications participating in EUF by equality to
  `true`/`false`;
- model-time EUF propagation lemmas with deterministic inspection and delivery
  budgets;
- optional online EUF propagation through CaDiCaL observed-variable callbacks;
- auto QF_UF preprocessing by default;
- strong preset with finite-domain AMO clauses, equality-definition clauses,
  default-on UF symmetry breaking, and opt-in bounded ground-congruence clauses.

Deferred:

- optimized explanation-producing proof forests;
- richer online conflict-clause injection;
- minimal theory-propagation reasons;
- proof output for preprocessing-generated lemmas;
- model construction.

## Paper Traceability

Nieuwenhuis and Oliveras, *Fast Congruence Closure and Extensions*, motivates
the EUF direction and the eventual data structures. Primo now implements the
Section 3 use-list, lookup, pending-merge shape as an opt-in conflict and
propagation checker over deterministic Curry-style flat definitions. Online
incremental propagation owns a persistent flat closure and uses SAT
decision-level callbacks with assignment-trail checkpoints. Closure checkpoint
creation is lazy per decision level and rollback uses a sparse undo trail, but
backtracking is not yet the paper's timestamped lookup undo structure. Under
`conflict`/`online` propagation, the final per-solve-call conflict check reuses
that persistent closure's already-verified conflict state instead of rebuilding
a flat closure from the complete assignment. Model-time propagation
(`--euf-prop=model`) and the final conflict check under `off`/`model`
propagation still rebuild flat closures from complete assignments.
Primo does not yet implement the Section 4-5 proof-producing union-find.

Deharbe et al., *Exploiting symmetry in SMT problems*, and Reger and Suda,
*Using SAT and SMT Solvers for Finite Model Finding with Sorts*, motivate the
default-on finite-domain symmetry-breaking preprocessing pass described in
[](preprocessing.md).
