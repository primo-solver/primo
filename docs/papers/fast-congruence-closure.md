# Fast congruence closure and extensions

## Citation

Robert Nieuwenhuis and Albert Oliveras, *Fast Congruence Closure and
Extensions*, Information and Computation 205(4), 557–580, 2007
{cite:p}`nieuwenhuis2007`. Section numbers refer to this journal edition.

## Role in Primo

This paper is the target reference for Primo's fast QF_UF theory solver. The
default engine selected with `--euf-engine=incremental` implements the Section 3
use-list and signature-lookup shape over Primo's own flat term representation.
The older lazy scaffold remains available with `--euf-engine=lazy` for
diagnostics. Default-off `--euf-explain-search=proof-forest`,
`reoriented-proof-forest`, and `structural-proof-forest` modes implement
increasingly closer subsets of the paper's Section 5 explanation machinery.
The reoriented mode implements Section 4/5.2-style raw-endpoint proof forest
reorientation, and the structural mode adds the Section 5.5 structural-class
node refinement. They are not default-ready: current evidence shows correctness
but mixed benchmark effects, and Primo still does not implement proof output or
the paper's timestamped lookup-table backtracking policy.

## Implemented Subsets

The diagnostic lazy path implements a basic ground-congruence scaffold:

- union-find over term ids;
- recursive term registration;
- repeated saturation for application congruence;
- explicit equality assertions;
- explanation graph edges labelled by SAT reason literals;
- congruence edges justified by child equality explanations.

The default incremental path adds:

- deterministic Curry-style flat definitions over `core::TermId` values;
- eager representatives and class lists;
- affected-definition use lists;
- sparse signature lookup for congruent flat applications;
- a pending merge worklist for explicit and congruence merges;
- disequality conflict detection;
- sparse `push`/`pop` checkpoints and an undo trail for online propagation
  experiments;
- drained changed-term notifications that are not resurrected by later pops;
- counters for terms, flat definitions, lookup probes and hits, pending pops,
  congruence merges, conflicts, explanation length, and backtracks.

This is a Section 3 shaped subset, not the complete optimized paper algorithm.
Explanations still come from a simple equality edge graph and are sound but not
minimal by default. The proof-forest mode is an experimental explanation
alternative, not a default or a complete paper implementation. Backtracking
trails ordinary mutable closure state rather than using the paper's optimized
timestamped lookup-table policy.

(section-4-5-mechanism-read-directly-from-docs-oliveras-euf-pdf-2026-07-22)=
## Section 4/5 mechanism

Sections 4 and 5 of {cite:p}`nieuwenhuis2007` were read on 2026-07-22.

Prior proof-forest experiments (see `fast-congruence-closure-implementation.md`
audit notes) built proof edges on top of Primo's existing size-oriented merge
structure and only added the Section 5.2 auxiliary-union-find/HighestNode
explain layer. That is not what the paper specifies, and it explains the
observed "correct traversal, worse clause shape" failure mode: the auxiliary
structure's efficiency argument depends on the proof forest itself having been
built by the paper's reorientation procedure, not by union-by-size.

The paper's actual Section 4 procedure, `Union(e, e')` (p.567), assuming
w.l.o.g. that the tree of `e` has no more elements than the tree of `e'`:

1. Reverse all edges on the path between `e` and the root of *its own* tree.
2. Add an edge `e -> e'`.

This is a *separate* structure from the ordinary Representative/Find union-find
used for congruence discovery. Theorem 15 combines a path-compressed union-find
with a separate parent array for the proof forest. Its two parent relations
serve different purposes and must be maintained independently.
Lemma 13: each proof-forest edge is reoriented at most O(log n) times over a
sequence of n-1 unions, because each reversal at least doubles the reversed
edge's tree size -- this is the amortized bound the whole approach depends on,
and it requires the tree to actually be a proof forest built this way, not a
relabeled copy of the size-based merge tree.

Section 5.2 extends this to congruence closure: `Propagate` additionally
inserts one proof-forest edge per merge, labelled either with the single input
equation (`a=b`) or, for a congruence step, the *pair* of structural equations
`(f(a1,a2)=a, f(b1,b2)=b)` (recursively explained). `Explain(c1,c2)` computes
the nearest common ancestor via an auxiliary union-find with a `HighestNode`
operation (the node of a class closest to the forest root), then walks both
sides to that ancestor via `ExplainAlongPath`, unioning each traversed node
into the auxiliary structure so a later recursive sub-explanation (from a
congruence-step label) can jump directly over already-output subpaths instead
of re-walking them. Theorem 17: a k-step proof is found in `O(k * alpha(k,k))`.

Section 5.4 is the quality-of-explanations analysis, and it is directly
relevant to the clause-size regression: the algorithm as given always returns
the *oldest* explanation (shortest is NP-hard, Ashish Tiwari, personal
communication), and Theorem 21 proves this can still contain redundant steps
-- only when the proof contains three equations of the same *structural
class* (same representative pattern). On the paper's own benchmark set (~20k
proofs), 13.92% of proofs had redundant steps, averaging 51 steps with 6
redundant when redundant.

Section 5.5 is the fix for that: build the proof forest over *structural
classes* (maximal groups of constants already connected by direct, non-
congruence equations) rather than over individual constants, so a congruence
step merges two whole classes at once instead of adding an edge between two
arbitrary representatives. This alone drops the redundancy rate from 13.92% to
3.3% (Theorem 24 gives an even tighter "two equivalent non-singleton
structural classes" redundancy condition for this variant). This -- not the
reorientation alone -- is the most likely reason a correctly-reoriented plain
proof forest could still occasionally produce a larger-than-BFS reason.

Scope decision for the next implementation attempt: build the plain
(non-structural-class) Section 4 + 5.2 forest first, correctly reoriented,
tested against the paper's own worked examples, and measure clause quality
before deciding whether Section 5.5's structural-class refinement is needed
on top. The paper itself treats 5.5 as a refinement of an already-correct 5.2
forest, not a prerequisite for correctness.

### 2026-07-22 status

`src/theory/equality/proof_forest.{hpp,cpp}` implements the plain Section 4 +
5.2 forest as a standalone class (`ProofForest`), independent of
`FlatCongruenceClosure`:

- `union_nodes(e, e_prime, label)` implements Section 4's Union exactly:
  walks from `e` to the root of e's own tree, reverses every edge on that
  path (moving each edge's label with it), then links `e -> e_prime` with
  the new label. Edges connect the caller-supplied raw nodes directly, not
  representatives -- the prior attempt's "endpoint-detour obligation"
  workaround (needed only because it linked via representatives) does not
  exist here because there is nothing to work around.
- `explain(c1, c2)` implements Section 5.2 self-contained: nearest common
  ancestor via an auxiliary union-find with `HighestNode`, reset per
  top-level call, and recursive congruence-obligation resolution within the
  same call (the paper's `PendingProofs` loop), returning a flat list of
  direct-equation reason ids.
- Backtracking (`push`/`pop`) is *not* in the paper (its Union sequence is
  online-only); this is Primo's own extension. Each `union_nodes` call
  records the pre-mutation `(node, parent, label)` triple for every node on
  the reversed path in one trail batch, so popping restores the exact prior
  state in one step, undoing both the reversal and the link.
- `structural-proof-forest` adds Section 5.5-style structural-class nodes as an
  opt-in mode; it is correct on the recorded tests and fuzzing, but not promoted
  because the benchmark evidence remains mixed.

Verified with hand-derived tests in `tests/unit/euf_solver_tests.cpp`
(`proof_forest_*`) that specifically exercise: reversal when `e` is *not*
currently a root (the exact case the paper's procedure exists to handle,
constructed and traced by hand against the algorithm, not copied from a
diagram); recursive congruence-obligation explanation; backtracking; and the
disconnected/trivial cases. All pass under the default and ASAN test suites.

**Integrated and benchmarked the same day.** `FlatExplanationSearchMode::
ReorientedProofForest` wires `ProofForest` into `FlatCongruenceClosure::
merge_classes`, calling `union_nodes(proof_from_endpoint, proof_to, label)`
with the pending merge's raw endpoints (the same variables the prior,
representative-based `record_proof_parent` call already computed, just used
correctly this time) instead of `lhs_repr`. `push`/`pop` call the forest's
own `push`/`pop` in lockstep so checkpoint indices line up. Passes the
existing randomized 180-step interleaved-backtracking oracle test
(`flat_congruence_closure_explanations_match_naive_oracle_under_backtracking`)
under both the default and ASAN suites.

One implementation bug found and fixed before measuring: `ProofForest::
explain` originally reset the auxiliary union-find with an eager O(n)
`assign` on every top-level call. With ~180K top-level Explain calls on
`PEQ002_size8`, that reset alone dominated wall time despite the algorithm
being asymptotically correct. Fixed with the generation-stamp pattern used
elsewhere in this file (bump a counter, lazily reinitialize each node the
first time it's touched under the new generation) -- this is a real lesson
for anyone reimplementing Section 5.2's "reset the auxiliary structure":
literally resetting an array does not scale in a system that calls Explain
once per theory conflict.

Local matrix on the two cases that sank the prior (representative-based)
attempt, current-branch Release build, single uncontended run each:

| file | mode | explain-calls | conflicts | max reason len | seconds |
|---|---|---:|---:|---:|---:|
| PEQ002_size8 | default (BFS) | 31,655,604 | 99,698 | 173 | 15.5 |
| PEQ002_size8 | reoriented-proof-forest | 183,286 | 179,492 | 149 | 35.5 |
| NEQ016_size7 | default (BFS) | 5,352,993 | 41,495 | 118 | 3.5 |
| NEQ016_size7 | reoriented-proof-forest | 45,228 | 43,765 | 130 | 3.1 |

Both answers remain `unsat` (sound) on both files, and explain-calls drop by
100-170x as the paper's complexity bound promises. But this is a mixed, not a
clean, result:

- `NEQ016_size7` is a real, if modest, win: 40% fewer SAT conflicts
  (18,221 -> 11,013) and slightly faster wall time.
- `PEQ002_size8` is a real loss: 1.8x more theory conflicts overall
  (99,698 -> 179,492), and gprof shows the extra time is not in
  `ProofForest` (whose own functions are now cheap, ~5% self-time combined)
  but in `CaDiCaL::Closure::find_subsuming_clause`, whose absolute cost rose
  ~10x (1.27s -> 12.11s) -- the larger, more numerous clauses this mode
  produces make CaDiCaL's own clause-database subsumption bookkeeping far
  more expensive.

This reproduces, with a now-provably-correct implementation, the exact
mixed pattern every prior (representative-based, therefore differently
broken) proof-forest attempt hit: the tree-structured Explain path is cheap
to compute but is not the graph-shortest path BFS finds, and on some
instances that costs more in SAT-search terms than it saves in
explain-computation terms. This is direct evidence for the Section 5.4/5.5
hypothesis above -- structural-class nodes (5.5) are the documented fix for
exactly this kind of "correct but not high quality" explanation, and remain
the next thing to try, not this reorientation-only forest.

**Decision:** kept as an opt-in mode (`--euf-explain-search=
reoriented-proof-forest`), not promoted to default. It is a correct,
independently useful building block (worth keeping given the drastic
explain-cost reduction and the NEQ016_size7 win), but not a net win on its
own, matching this project's standing policy of not promoting a change on
a 2-file local matrix. See `docs/run-log.json` for the run record.

### 2026-07-22 status: Section 5.5 structural-class nodes (correct, but blocked on a performance regression)

Implemented on top of `ProofForest` via two new public methods,
`coalesce_into_structural_class(member, canonical, label)` and
`resolve(node)`, wired into `FlatCongruenceClosure::merge_classes` as a
third mode, `--euf-explain-search=structural-proof-forest`.

**What a structural class actually is.** Re-reading Section 5.5 and
Theorem 21 carefully (the previous status note above had this slightly
wrong): a structural class is *not* "constants pre-merged by direct
equations". It is the set of congruence-*produced* terms sharing one
`(function, argument)` representative pair -- e.g. `f(a1,a2)=a`,
`f(b1,b2)=b`, `f(c1,c2)=c` are one class iff `a1~b1~c1` and `a2~b2~c2`
(Example 22). The paper coalesces every such term into a single
proof-forest node (no edge between them at all) instead of linking them by
tree edges. This codebase's `lookup_` signature table
(`ApplyKey{rep(function), rep(argument)}` in
`flat_congruence_closure.cpp`) already computes exactly this partition as
a side effect of ordinary congruence detection: every `enqueue_congruence`
call fires precisely when a new definition's result collides with
`lookup_`'s existing entry for that key, i.e. every congruence merge in
this codebase is already "one new structural-class member joining the
class's existing canonical member." That is the hook
`coalesce_into_structural_class` uses.

**Three soundness bugs found and fixed, all one root cause.** A node's own
signature gets rehashed and can be rediscovered congruent more than once
over its lifetime, as *unrelated* merges elsewhere change its own
arguments' representatives -- so a node already used as a structural-class
canonical, or already itself a coalesced member, is routinely asked to
join *another* class later. Naively resolving to "the current identity"
and linking through that, instead of the raw node, silently drops
whichever bridge fact got resolved away from the explanation -- unsound,
not just suboptimal. Concretely:

1. `ProofForest::explain`'s recursive obligation loop originally only
   alias-resolved the top-level `(c1, c2)` query; obligations produced
   *during* the walk (from a congruence edge's own sub-obligations, or
   from a bridge label further down an alias chain) were fed to
   `nearest_common_ancestor` raw, with no guarantee they were tree-identity
   nodes. Fixed by resolving (and re-seeding bridge obligations for) both
   sides of every obligation the main loop pops, not just the initial pair.
2. Re-coalescing an already-aliased member by pointing its alias slot at
   the new canonical (or by linking the two *resolved* identities and
   reusing the same label) discards the member's earlier bridge fact
   entirely, since nothing else in the tree ever visits that member's raw
   id again. Fixed by adding `ProofForest::materialize(node)`: converts an
   existing alias into a real tree edge first (exactly what a
   non-coalescing Union would have produced), so the new fact can be
   linked on top of it via ordinary Union and Section 4's own
   path-reversal handles reconnecting everything.
3. The same problem applies symmetrically to `canonical` (a canonical is
   only guaranteed alias-free at the moment it is first recorded in
   `lookup_`, not forever) and to direct-equation endpoints (an equation
   can directly mention an already-coalesced member, the paper's own
   Example 23). Both call sites now `materialize` before linking. A
   related trap: `parent_[node] == node` alone does not mean `node` is
   safe for the cheap alias fast path, because `union_nodes` never
   rewrites `parent_[e_prime]` -- a self-rooted node can already have a
   real incoming edge from an earlier `e`. Added `has_incoming_edge_`
   (set on `e_prime` inside `union_nodes`, trailed for backtracking) to
   catch this; the fast alias path now additionally requires
   `!has_incoming_edge_[member]`.
4. **Found by fuzzing, not by the tests above.** The hand-derived Example
   22/23 tests and the 180-step randomized oracle test all passed after
   fix 3 -- and still missed a fourth instance of the same class of bug: a
   node could become an alias *target* (something else's
   `coalesce_into_structural_class` fast-path pointed at it) without
   `has_incoming_edge_` being set, since that flag was only set inside
   `union_nodes` for real tree edges. Such a node could then itself be
   fast-aliased away, leaving whoever pointed at it via a later
   `materialize()`-created real tree edge dead-ended at a self-rooted
   `parent_` the ordinary tree walk cannot see past (it only follows
   `parent_`, never `alias_`, past the initial query). This surfaced as an
   uncaught `std::logic_error` ("flat congruence closure failed to explain
   disequality conflict") from `FlatCongruenceClosure::check_disequality`
   -- a different call path than the public `explain_equal` the unit tests
   exercise. `scripts/fuzz_external.py --fuzzer fuzzsmt` against z3 found
   it in the first 500 generated QF_UF instances (6 crashes); `ddsmt`
   shrank one to an 8-line reproduction in two passes. Fixed by marking
   `has_incoming_edge_[canonical]` on the fast alias path too, not only
   inside `union_nodes`.

Verified against hand-derived unit tests built directly from the paper's
Example 22 and Example 23 (`proof_forest_structural_class_*` in
`tests/unit/euf_solver_tests.cpp`, same rigor as the existing
`proof_forest_*` tests -- traced by hand against the paper's own worked
examples, not copied from a diagram), the existing 180-step randomized
backtracking oracle test extended to cover this mode, and -- after finding
fix 4 required going beyond what those catch -- a 2000-instance
`fuzzsmt`-against-z3 round against an ASAN/UBSan build (0 crashes, 0
reference disagreements). All pass. The lesson for next time: for a
data structure this stateful, run the fuzz gate *during* development, not
only benchmarks after -- it found in under a minute what several rounds of
hand-tracing benchmark failures took a full session to isolate.

**Performance: two real bugs, both found by profiling and fixed.** With the
crash fixed, the project's standard 334-file primo.html benchmark subset
(`benchmarks/user-primo-html334-2026-07-18.txt`) initially gave a clean but
bad comparison: default 270/334 solved (PAR2 7.500), reoriented-proof-forest
259/334 (7.982), structural-proof-forest 232/334 (9.329) -- a clear,
consistent net loss, and `NEQ016_size7` standalone did not finish in 60+
seconds (versus 3.1s for the other two modes). `perf record` on
`NEQ016_size7` (bounded to a 20s window, since the process did not
terminate on its own) showed why: ~76% of all samples were inside
`ProofForest::explain` and its two NCA-search helpers, `aux_find` (38%) and
`nearest_common_ancestor` (13%) -- the opposite of every other mode's
profile, where theory/SAT-search code dominates and the explain family
barely registers. Two distinct causes, found and fixed in sequence:

1. **Tree depth wasn't bounded.** Lemma 13's amortized O(log n) reversal
   bound requires every Union to reverse the *smaller* side's path, so a
   given edge's subtree size at least doubles between reversals.
   `reoriented-proof-forest`'s direct-equation caller already gets this
   right (chooses e/e' by live `class_size_` before calling `union_nodes`).
   `coalesce_into_structural_class`'s fallback branch and `materialize`
   both created real tree edges by semantic role (member vs canonical,
   alias source vs target) with no size discipline at all -- and once
   fast-path aliasing exists, live class size stops being an accurate
   proxy for real tree size anyway (an aliased member inflates its live
   class without adding real tree depth). Fixed by giving `ProofForest`
   its own tree-size tracking (`size_`) and making `union_nodes` itself
   swap which physical node plays `e` vs `e'` when the caller's choice
   disagrees with size -- sound regardless of caller intent, since a tree
   edge's label proves its two endpoints equal however it is attached.
   Confirmed via direct instrumentation: max real tree depth stayed at 46
   after 200,000 `union_nodes` calls on `NEQ016_size7`, versus unbounded
   growth before.
2. **Alias-bridge obligations weren't deduplicated within a call.** Fixing
   (1) alone did not fix `NEQ016_size7`. Further instrumentation showed
   `seed_and_resolve` (the alias-chain walker inside `explain`) being
   called over a *billion* times in 15 seconds against only ~5,000
   top-level `Explain` calls in that window. The auxiliary union-find
   (`aux_find`/`aux_union_to`) already deduplicates repeat *tree*-edge
   congruence obligations reached via `explain_along_path`'s NCA walk --
   that compression is what Section 5.2's own complexity argument depends
   on. It does not cover *alias*-bridge obligations reached via
   `seed_and_resolve`: a coalesced node's bridge obligations were pushed
   onto `pending_proofs` every time *any* obligation referenced it, with
   no memoization, and structural-class coalescing routinely produces a
   few popular "hub" canonical nodes referenced by many different
   obligations within one `Explain` call. Fixed with the same
   generation-stamped mark pattern already used for
   `aux_generation_`/`ancestor_generation_` in this file: a node's bridge
   obligations are pushed at most once per top-level `Explain` call.

Combined, both fixes take `NEQ016_size7` from not finishing in 180+ seconds
to solving in 3.67s -- in line with default (3.11s) and
reoriented-proof-forest (3.05s) -- and move the 334-file comparison from a
clear net loss to close to competitive:

| mode | solved / 334 | PAR2 |
|---|---:|---:|
| default | 262 | 7.989 |
| reoriented-proof-forest | 254 | 8.279 |
| structural-proof-forest | 248 | 8.643 |

(Absolute counts shift a few files run-to-run from shared-core contention
during the 14-way parallel sweep -- default and reoriented's own code did
not change between the two sweeps -- so the meaningful comparison is
structural's *relative* position: only 6 files behind reoriented now,
versus 27 before these fixes, with divergence down to 20/334 from 43/334.)
Verified against unit tests, a freshly-configured ASAN/UBSan build, and a
2000-instance `fuzzsmt`-against-z3 round after each fix (0 crashes, 0
disagreements both times).

**Decision:** implementation is verified correct (fuzzed clean) and is now
close to competitive with reoriented-proof-forest rather than a clear
performance loss -- still not a clear net *win*, so still not promoted to
default. Kept as an opt-in mode (`--euf-explain-search=
structural-proof-forest`). The remaining 6-file gap has not been profiled
further this session; that would be the natural next step before any
promotion decision. See `docs/run-log.json` for the run record.

## Remaining implementation checklist

Before claiming that Primo implements the full Nieuwenhuis-Oliveras fast
congruence closure, the code must add and document:

- a default-ready Section 4/5 proof-forest explanation policy with broad
  benchmark evidence, not only opt-in modes;
- the paper's timestamped lookup-table backtracking policy;
- pure-class performance probes showing the Section 3 machinery improves the
  intended QF_UF families;
- benchmark evidence showing solved-count or PAR2 improvement without wrong
  answers.

## Deferred

- optimized incremental backtracking
- default-ready optimized proof-forest explanation machinery
- proof production

## Data structures

Diagnostic lazy scaffold:

- union-find over `TermId`;
- set/vector of registered terms;
- explicit explanation edge graph.

Default incremental subset:

- flat term definitions;
- equivalence classes;
- application lookup/signature table;
- affected-definition use lists;
- pending merge queue;
- disequality set;
- explanation graph.

## Invariants

Current scaffold invariants:

- all inspected terms are created by `core::TermManager`;
- equality is over `TermId`, not SMT-LIB AST nodes;
- explanations contain SAT literals supplied by the SMT layer.

Incremental subset invariants:

- lookup probes are built from current representatives; older lookup keys may
  remain after representative changes and are ignored by current-signature
  probes;
- every pending merge is either processed or invalidated by backtracking;
- use lists contain applications that must be reconsidered after a class merge;
  duplicate entries are tolerated and deduplicated when an affected list is
  processed;
- explanation edges only cite SAT literals supplied by the SMT layer.

## Tests derived from the paper

- equality/disequality contradiction
- unary congruence
- binary congruence
- nested congruence
- lookup-table congruence discovery without scanning all term pairs
- backtrack rediscovery
- proof-forest explanation paths, including reorientation, recursive
  congruence-obligation resolution, backtracking, and structural-class
  coalescing.
