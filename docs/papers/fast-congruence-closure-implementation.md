# Fast Congruence Closure Implementation Note

## Reference

Robert Nieuwenhuis and Albert Oliveras, "Fast Congruence Closure and
Extensions", *Information and Computation* 205, 2007.

Implemented material: Sections 3.1 through 3.5. Sections 4 and 5, which add the
optimized proof-producing union-find machinery, are not implemented.

## Paper Operations To Primo Types

The paper first Curryfies applications and then flattens non-constant subterms
so the congruence-closure module receives constants and flat equations
`f(a, b) = c`. Primo keeps SMT-LIB out of the theory layer: `FlatTermIndex`
reads `core::TermId` values owned by `core::TermManager`, converts every
application into deterministic binary apply definitions, and maps original
equality atoms to flat endpoints.

Paper data structures map as follows:

- `Pending`: `FlatCongruenceClosure::pending_`, a LIFO merge worklist
  containing explicit equality merges and congruence merges discovered through
  lookup hits. The worklist order is sound for closure but can affect proof
  shape and SAT search.
- `Representative`: `FlatCongruenceClosure::parent_`, where every class member
  eagerly stores the current representative.
- `ClassList`: `class_members_` and `class_size_`, used to merge the smaller
  class into the larger one.
- `UseList`: `use_lists_`, indexed by representative and containing flat
  definition results whose function or argument representative includes that
  class.
- `Lookup`: `lookup_`, keyed by `(representative(function),
  representative(argument))`, storing one flat definition with that current
  signature.
- Disequality candidates: `disequalities_` stores asserted disequalities in
  assertion order, while `disequality_lists_` indexes those rows by current
  representative. This is an implementation-local index for conflict checks,
  not a separate paper operation.

`Merge` is implemented by `assert_equal`, `enqueue`, `propagate`, and
`merge_classes`. Constant equalities are pushed to `pending_`; application
congruence is discovered when a flat definition's normalized signature hits an
existing lookup entry. `process_use_list` implements the Section 3.3 lines that
only revisit definitions affected by the representative that just disappeared.

`AreCongruent?` is implemented by `are_equal`, which drains `pending_` and
compares current flat representatives. Current Primo terms remain hash-consed
`TermId` values outside the theory engine; applications are normalized through
the flat signature table.

Disequality conflicts are checked immediately when a disequality is asserted.
After a merge, `FlatCongruenceClosure` only revisits disequalities indexed by
the two merged representatives. Candidate indices are sorted before checking so
the first conflict follows original disequality assertion order even though
unrelated disequalities are skipped. Direct disequality conflicts are checked
before the affected use list is saturated; once the branch is already
inconsistent, the closure can report that conflict without spending work on
congruence consequences that SAT backtracking will discard.

## Invariants

- Every inspected object is a `core::TermId`; SMT-LIB AST nodes never enter the
  equality engine.
- `parent_[t]` is the eager representative for every member `t` of a live class.
- `class_members_[r]` contains exactly the members of representative `r`;
  non-representative class lists are empty after a merge.
- Backtracking uses the class-list merge record to restore both
  `class_members_` and the eager `parent_` representative for every moved
  member. This is an implementation-local sparse rollback representation of the
  Section 3 class-list operation; it does not change the paper's merge
  semantics. Class sizes, use-list swaps, lookup insertions, disequality lists,
  edge adjacency, and proof-parent diagnostics remain separately trailed because
  their rollback lifetimes are not identical to the moved class suffix.
- `lookup_` entries are keyed by current flat representatives at insertion time.
  Stale keys may remain after a merge but are not queried because new probes are
  built from current representatives.
- Each flat result id has at most one definition. Re-adding the same
  `(function, argument, result)` triple is ignored; adding the same result with a
  different signature is rejected as an invariant violation before it can corrupt
  lookup witnesses or congruence explanations.
- `use_lists_[r]` contains definitions that must be reconsidered when `r` stops
  being a representative. Duplicates are tolerated while appending and are
  suppressed with a generation-stamp scratch vector when a list is processed.
- Disequalities are stored as theory literals and checked after each merge.
- Explanations are sound SAT-literal sets collected from an explicit equality
  graph. They are not the optimized proof forests of Sections 4 and 5.
- Explanation adjacency lists are chronological: each endpoint list receives
  edge identifiers in the same append order as `edges_`. Bounded recursive
  explanations may therefore stop scanning a node's adjacency list when the
  first edge at or after the causal bound is reached; all later entries are
  also outside the allowed proof prefix. Checkpoint rollback removes adjacency
  entries from the back, preserving this order.

## Complexity Expectations

The implemented congruence discovery follows the Section 3 affected-use-list
shape rather than rescanning all registered term pairs. Class merging uses the
paper's smaller-into-larger rule, so each term changes representative
logarithmically often. The current lookup table is a sparse hash table.
Use-list maintenance deliberately avoids a linear duplicate scan on every
append. Each `process_use_list` invocation increments a generation stamp and
skips repeated result identifiers in the affected list; this keeps duplicate
discipline at the point where duplicates would otherwise cause repeated lookup
work. The stamp vector is scratch metadata sized with the flat node arrays and
does not participate in checkpoint rollback.

Two deviations matter for performance claims:

- Backtracking uses `push`/`pop` checkpoints plus a sparse undo trail for the
  mutable flat-closure state that changes after a checkpoint: representatives,
  class sizes and members, use lists, disequality lists, explanation adjacency,
  and lookup maps. This is correct for SAT integration and avoids copying the
  whole closure on every checkpoint, but it is still not the paper's timestamped
  lookup-table backtracking policy.
  Hot undo entries store only scalar rollback data; operations that need full
  vector snapshots, currently use-list restore and disequality-list restore,
  place those snapshots in side payload arrays whose sizes are checkpointed
  with the main trail. This is a storage-layout choice for the current
  rollback implementation, not a separate paper operation.
- Final model checks and model-time propagation still rebuild the flat equality
  state from a complete SAT assignment.
- Opt-in online propagation with `--euf-engine=incremental` owns a persistent
  flat closure and asserts theory literals as CaDiCaL reports assignments. It
  creates those checkpoints lazily, immediately before the first theory
  assertion in a SAT decision level, and restores the first discarded
  checkpoint on SAT backtrack. This avoids replaying the retained assignment
  trail and avoids allocating rollback state on Boolean-only levels, but still
  is not the paper's timestamped lookup-table backtracking policy.
- Online propagation candidate discovery uses the same affected-class idea at
  the SMT callback boundary: flat-class merges record terms whose representative
  changed, and the callback maps those terms to existing theory atoms before
  falling back to a full scan. This is an implementation-local work queue, not a
  new theory rule; reason construction still checks every delivered candidate.

## Counters

The engine exposes counters for:

- registered terms and flat application definitions;
- explicit and congruence merges;
- pending queue pops;
- lookup probes and hits;
- disequality checks and conflicts;
- explanation calls and per-explanation cache hits;
- explanation length;
- explanation-shape diagnostics: top-level equality queries, recursive
  congruence-premise queries, repeated pair queries served from the per-reason
  cache, and maximum recursive explanation depth;
- explanation diagnostics: BFS searches, BFS nodes visited, BFS path edges,
  maximum BFS queue size, recursive causal edge skips, direct equality edges
  expanded, congruence edges expanded, and duplicate reason literals skipped;
- conflict-reason size diagnostics: maximum emitted reason size and buckets for
  reasons of length at most 4, 8, 16, 32, 64, and greater than 64;
- backtracks;
- use-list appends, process-time duplicate skips, affected-list duplicate skips,
  and maximum observed use-list size;
- compact class-member rollback volume via
  `:euf.incremental.class-member-trail-entries-saved`.

The SMT layer exports these under `:euf.incremental.*` when
`--euf-engine=incremental` is used for final conflict checks, model-time
propagation, or online propagation.

## Tests Derived From The Algorithm

The unit tests in `tests/unit/euf_solver_tests.cpp` cover:

- unary and binary congruence discovered by lookup hits;
- shared-subterm congruence explanations;
- disequality conflict clauses;
- backtrack undo followed by rediscovery;
- nested checkpoint rollback, disequality-conflict rollback, and
  post-checkpoint definition rollback;
- deterministic Curry-style flat definitions;
- order-preserving flat disequality candidate scans;
- intersection-only scans for disequalities that can newly conflict after a
  merge;
- direct disequality conflicts before affected use-list scans, including
  checkpoint rollback after that early exit;
- repeated sub-explanation reuse inside one flat congruence reason;
- causal recursive explanations for congruence edges: child premises ignore
  equality-graph edges introduced at or after the congruence edge being
  justified;
- independent reason checking: every produced flat explanation in a deterministic
  push/pop sequence must entail the queried equality under a naive saturation
  oracle using only the explicit equality literals named by the reason;
- changed-term draining after flat merges and rollback of undrained changed-term
  records on backtrack;
- process-time use-list duplicate handling across checkpoint rollback;
- lookup-hit definitions remain represented by the existing lookup/use-list
  witness and still participate in future congruence discovery;
- deterministic randomized flat-closure sequences checked against a naive
  saturation oracle under checkpoint rollback;
- nonzero lookup-hit and congruence-merge counters.

The SMT tests in `tests/unit/boolean_solver_tests.cpp` cover conflict checks,
model-time propagation, and persistent online propagation with
`--euf-engine=incremental`. The online test requires nonzero flat-term,
flat-definition, lookup-hit, and congruence-merge counters.

## Known Limitations

- The default EUF engine is the flat incremental checker.
- Final conflict checks and model-time propagation still rebuild flat closures
  from complete SAT assignments.
- Online and conflict callback modes do not yet implement the paper's
  timestamped backtracking structure, and are not default on the hard Goel
  regressions.
- Explanations are intentionally non-minimal.
- Proof output and the Section 4-5 proof-producing union-find are deferred.

## 2026-07-19 Audit Notes

Two local experiments checked whether the remaining PEQ/NEQ slowdown could be
closed by smaller patches around the Section 3 use-list and the current
explanation graph:

- A stricter reading of Section 3.3 would avoid moving an affected flat
  definition to the new representative's use list after its normalized
  signature hits `Lookup`. In Primo's current online/backtracking setting this
  reduced lookup work on `PEQ019_size7` and `iso_brn_repgen_sk032`, but
  regressed harder PEQ rows such as `PEQ002_size8`; the change was rejected.
- A bounded per-branch memo table for completed `explain_equal` reasons was
  also rejected. Existing repeated-explanation counters did not move
  materially on the local PEQ/SEQ/NEQ/QG matrix, while wall time became noisier
  or worse.

The audit therefore points back to the paper's Section 4-5 proof-forest
mechanism as the plausible missing congruence-closure component. The current
`FlatCongruenceClosure` explanation path is still an adjacency-list BFS over
the equality edge graph with recursive expansion of congruence labels. The
paper instead maintains a proof forest during `Propagate` and answers
`Explain` by walking to a nearest common ancestor, using an auxiliary
union-find/highest-node structure to avoid repeated visits across recursive
subproofs. That implementation remains future work; it must be designed from
the paper rather than from any reference solver code.

A follow-up local attempt implemented a Section 4-style reoriented proof forest
and then replaced recursive congruence-label expansion with a Section 5.2-style
pending-proof worklist plus auxiliary union-find/highest-node scratch state.
Focused flat-CC tests passed, but the local PEQ/SEQ/NEQ/QG matrix was mixed:
`PEQ019_size7` improved, while `PEQ002_size8` regressed badly because the proof
forest produced substantially larger conflict reasons and changed SAT search.
The attempt was rejected and reverted. The lesson is that the missing optimized
`Explain` machinery is not just connectivity; proof-tree shape and reason
quality must be measured directly before replacing the current BFS reason path.

Next CC review items:

- Add diagnostic-only counters for per-conflict reason length distribution and
  compare BFS path length against proof-forest path length without changing the
  emitted clause.
- Revisit the paper's proof-forest orientation together with explanation
  length/age tradeoffs from Section 5.4 before enabling it in the default
  online EUF path.
- Do not replace per-append `UseListPop` rollback with a naive per-level
  `UseListResize` trail entry. A local prototype preserved the focused oracle
  tests but regressed `SEQ035_size6` from about 1.2 seconds to about 21.7
  seconds because use-list appends exploded from about 4.3 million to about
  84.9 million after backtracking. Any batching of use-list rollback must account
  for `UseListRestore` swaps in `process_use_list`, not only append-only growth.
- Keep reference-solver CC code as behavioral evidence only. The implemented
  Primo design must remain paper-derived and tested through local invariants.
- The accepted parent-restore batching removes per-member `Parent` trail
  entries and restores moved parents from the existing `ClassMembersMerge`
  suffix record. On a local NEQ/PEQ/QG/Goel matrix it reduced trail-entry counts
  on every completed row, for example `NEQ016_size7` default
  109,691,307 -> 84,296,375, while wall-time medians were mixed and should not
  be reported as a headline speedup without fresh SLURM evidence.

The follow-up audit on the same date rechecked the current `FlatCongruenceClosure`
implementation against the Section 3 data-structure checklist and the
LLM2SMT behavior notes without copying reference code. No rollback or
congruence-discovery soundness bug was found in the inspected paths:
definitions are trailed, lookup insertions are removed on pop, use-list
duplicates are tolerated and handled at process time, changed-term queues are
truncated by checkpoints, and explanation edges are removed with the merge edge
that introduced them. The local gprof profile for `PEQ002_size8` still puts
`FlatCongruenceClosure::explain_equal_into` first by self time, with 25.6M
explanation calls, 7.0M BFS searches, 105.9M BFS nodes visited, and 21.4M
recursive congruence-edge expansions in the ordinary stats run. This reinforces
the earlier conclusion: the current closure is a Section 3 congruence-discovery
engine with a sound adjacency-BFS explanation layer, not the Section 4-5
optimized proof-forest explanation algorithm.

The next diagnostic patch added explanation-shape counters without changing the
emitted conflict or propagation clauses. These counters expose how much of the
current explanation work is top-level disequality/propagation explanation versus
recursive congruence-premise explanation, how often a recursive pair is repeated
inside one reason, and the maximum recursion depth reached. They are not a
Section 4-5 proof-forest implementation; they are instrumentation to decide
whether a future proof-forest design is targeting repeated subproof traversal,
deep congruence nesting, or reason quality.

The audit also checked `NEQ027_size8` through `NEQ027_size11` against the tuned
local LLM2SMT command used in prior benchmark notes. Those rows no longer
reproduce as local slow-Primo CC gaps after the raw-AMO and use-list work:
Primo solved size11 in about 2.2 seconds, while the tuned local LLM2SMT binary
took about 4.0 seconds. The remaining difference on that family is more likely
preprocessing policy, especially finite-domain equality-definition selection,
than congruence-closure implementation correctness.

The next diagnostic pass added conflict-reason length buckets without changing
the emitted clauses. On the local matrix, `PEQ002_size8` had a large-reason tail
with 31,562 conflicts over 64 literals and maximum reason length 173, while
`SEQ035_size6` had mostly short reasons but still performed 1,040,412 BFS
explanation searches. This separates the remaining CC work into two mechanisms:
reason quality for PEQ-style operation-table rows, and explanation traversal
cost for formulas that generate many short conflict reasons.

A follow-up direct-edge fast-path prototype was rejected. The idea was to scan
the left endpoint's explanation adjacency list before BFS and immediately use a
one-edge reason when the requested endpoints were directly connected. This
reduced BFS search counters, but the per-call adjacency scan was too expensive:
the local PEQ/SEQ/NEQ/QG matrix slowed down across the board. Future traversal
work should avoid adding per-explanation scans before the main search.

The next review pass hardened the current explanation graph without replacing
it. Recursive expansion of a congruence edge now propagates failure from child
explanation queries instead of assuming those invariants silently hold; direct
disequality conflicts fail closed if the equal endpoints cannot be explained.
The new explanation-oracle unit test checks returned reason literals against an
independent naive saturation model under deterministic checkpoint rollback.
This did not claim a performance improvement. Local `NEQ015_size4` and
`PEQ019_size7` solver smoke runs preserved the expected `unsat` stats shape; on
`PEQ019_size7`, the remaining hot signature is still explanation traversal and
large conflict reasons, with 1,261,146 explain calls, 320,435 BFS searches, and
1,239 conflicts over 64 reason literals.

The next CC review found that recursive explanation of a congruence edge was
searching the full current equality graph. Congruence edges are causal proof
steps: a child equality premise must be explained using equality edges available
before the congruence merge was added. `FlatCongruenceClosure` now passes the
congruence edge index as a recursive BFS bound, so child explanations ignore
edges added at or after that proof step. This is still the current adjacency-BFS
explanation layer, not the paper's Section 4-5 proof forest. The local
`a228e9d` baseline comparison showed unchanged conflict counts, explanation
calls, BFS search counts, and reason-length buckets on the sampled rows, but
fewer BFS nodes visited: `PEQ002_size8` dropped from 105,904,124 to 79,293,756,
`PEQ019_size7` from 4,634,102 to 3,529,805, `SEQ035_size6` from 5,035,533 to
5,013,254, and `qg7/iso_icl_repgen006` from 8,754,428 to 5,960,538.

A subsequent cache audit found that the per-reason repeated-pair cache also has
to respect the causal edge bound. A pair explained with a later edge prefix is
not necessarily valid for an earlier congruence edge's premise. The cache now
records the smallest edge bound under which each pair has been successfully
explained and only reuses the entry when the current recursive bound is equal
or looser. The regression test directly exercises a broad successful query
followed by a stricter failed query in one reason collection. This is a
correctness and diagnostics tightening, not a performance win: on local
`NEQ016_size8`, the stricter cache exposed millions of bound misses and more
BFS work while preserving the same timeout outcome. That reinforces the earlier
conclusion that the remaining CC-specific performance gap is the missing
Section 4-5 proof-forest explanation mechanism and reason-quality policy.

A fresh local audit on commit `5621a24` repeated the `NEQ016_size8` stats and
perf profile after the cache-bound fix. The 45-second capped stats run still
timed out with 72.3M explanation calls, but only 366k top-level explanation
queries; nearly all work came from recursive congruence-premise explanations.
The same run reported 23.9M BFS searches, 253.6M BFS nodes visited, 65.7M
expanded congruence edges, 29.5M causal edge skips, 7.0M cache-bound misses,
833M trailed closure mutations, and zero affected-use-list duplicate skips.
The corresponding perf sample put `FlatCongruenceClosure::explain_equal_into`
first at 19.0 percent self time, followed by CaDiCaL propagation, class merges,
rollback, and use-list processing. This review did not find a new Section 3
congruence-discovery or rollback bug. It narrows the next CC work to a
paper-derived explanation design: either implement and validate the Section 4-5
proof-forest machinery with reason-length safeguards, or add a diagnostic mode
that computes proof-forest path lengths without changing emitted clauses before
attempting another replacement.

A rejected micro-optimization tried to cache failed bounded explanations during
one reason collection: if no path exists using edges before bound `N`, stricter
bounds cannot succeed either. The focused unit test passed, but local probes on
`NEQ016_size8`, `NEQ016_size7`, `NEQ023_size7`, `PEQ019_size7`,
`PEQ002_size8`, and `qg7/iso_brn_repgen033` produced zero failure-cache hits.
The patch would therefore add a hash lookup to the hottest explanation path
without reducing BFS work on the sampled rows, so it was reverted. This leaves
successful bounded-pair reuse as the only current per-reason cache and again
points at proof-tree shape, not failed-query repetition, as the next CC target.

The next accepted source change kept the default explanation path unchanged and
added `--euf-explain-search=bidirectional` as an explicit experiment. This mode
uses bidirectional BFS over the same causal equality-edge graph. It is a
baseline graph-search alternative, not a proof-forest implementation. Local
same-binary probes showed why it is not a default: it improved `NEQ016_size7`
and `PEQ019_size7` and reduced BFS visits on `NEQ016_size8`, but it regressed
`PEQ002_size8` badly by changing proof paths and SAT search. The option exists
so SMAC and focused local runs can test whether bidirectional search is useful
inside a family-specific profile while the validated default remains
one-sided BFS.

The next review step added `--euf-proof-forest-diagnostics`, a default-off
measurement mode that maintains a rollback-aware proof-parent forest beside the
existing explanation graph. This is still not the Section 4-5 `Explain`
algorithm: emitted conflict and propagation clauses continue to use the current
causally bounded BFS reasons. The diagnostic records how many cache-miss
explanation queries have a proof-forest path, the total proof-path edge count,
the maximum path length, missing-LCA cases, and per-query shorter/equal/longer
comparisons against the BFS path that actually emitted the reason. A local probe
found no missing LCAs on the sampled rows. It also showed why a direct
proof-forest replacement needs reason-quality safeguards: proof-path edge counts
were lower than BFS path edge counts on `NEQ016_size7` and `PEQ019_size7`, but
higher on `PEQ002_size8`, the same row where earlier reason-shape experiments
regressed.

A subsequent CC review against Section 5 found no new rollback or explanation
soundness defect in the current adjacency-BFS implementation, but it did close
two test gaps and one local performance hygiene issue. `propagate()` now stops
draining pending merges after `conflict_` is set, because those merges cannot
change the current theory answer and only add class, use-list, explanation-edge,
and trail work. The unit suite now covers bidirectional BFS causal-bound and
cache-bound behavior, and checks a nested-congruence disequality conflict reason
against the naive flat oracle. This still leaves the important paper gap
unchanged: Primo does not store labeled reoriented proof-forest edges for
`Explain`, and it does not use the per-explanation auxiliary union-find with
`HighestNode` from Section 5.2 to avoid repeated subproof traversal.

The next rollback audit split rare vector snapshots out of `TrailEntry` and
kept them in checkpointed side payload arrays. Focused tests and local
PEQ/SEQ/NEQ/QG smoke runs preserved answers and the main search counters. This
reduces hot trail-entry memory traffic in the existing checkpoint trail, but it
is not the missing Section 4-5 explanation algorithm and should not be reported
as reference parity.

A follow-up CC review tightened two invariants. First, `add_definition` now
rejects conflicting duplicate flat definitions for the same result id and
ignores exact duplicates; `FlatTermIndex` normally creates unique definitions,
but the closure now enforces that precondition directly. Second, proof-forest
diagnostics now trail the merge-edge index attached to each diagnostic parent
link and stop parent walks at the same causal edge bound used by recursive BFS
explanations. The diagnostic is still only a path-length comparison beside the
current BFS reason engine; it is not the Section 5 `Explain` algorithm and does
not emit proof-forest reasons.

A clean-room comparison against the allowed local reference checkout and a fresh
read of Sections 3 and 5 of Nieuwenhuis-Oliveras refined that limitation. The
reference behavior includes a proof-producing explanation path with a per-call
auxiliary union-find, while Primo's active implementation still emits reasons
from the causally bounded equality-edge BFS. Primo's
`--euf-proof-forest-diagnostics` mode is useful for checking whether
representative proof-parent LCAs exist, but it is not an exact predictor of
Section 5 explanation cost: a real proof edge must also account for the original
endpoint in the merged class when that endpoint is not the representative. The
current diagnostic stores only the representative parent edge and therefore can
understate the extra sub-explanation work a full proof-forest replacement would
need.

The same review did not find a new Section 3 congruence-discovery or rollback
bug in the emitted-default path. The lookup table still has the intended
Primo-specific backtracking discipline: entries inserted after a checkpoint are
trailed and removed, older keys may become stale after merges, and current
probes use current representatives so stale keys containing dead
representatives are not reachable until rollback restores those
representatives. Use-list entries remain duplicate-tolerant and are deduplicated
only when an affected representative is processed.

Local proof-diagnostic probes on `NEQ023_size7` and `iso_icl_repgen005` showed
why the next CC implementation needs a guarded paper-derived design rather than
a direct parent-path substitution. On `NEQ023_size7`, diagnostic proof-parent
paths were shorter than BFS on 3907 queries, equal on 22025, longer on 796, and
had no missing LCAs. On `iso_icl_repgen005`, they were shorter on 331218
queries, equal on 4547349, longer on 463354, and had no missing LCAs. The
`iso_icl_repgen005` run still spent 7.83M calls and 5.34M BFS searches in
explanation, so the hotspot is real, but the representative-only diagnostic
does not justify replacing the emitted reason path without implementing the
full Section 5 endpoint-label and auxiliary-union-find obligations.

The next implementation step added `--euf-explain-search=proof-forest` as a
default-off Section 5.2 experiment. This mode no longer uses the earlier
representative-only diagnostic as an explanation. Every maintained proof-parent
edge stores the original endpoint from the disappearing class, and the explainer
uses a per-explanation auxiliary union-find with `HighestNode` jumps and a
pending-proof worklist. When the stored endpoint differs from the disappearing
representative, the worklist receives a bounded endpoint-detour obligation
before the edge label is accepted. Congruence labels add bounded child
obligations for the function and argument premises instead of recursively
calling the BFS explainer.

Implemented subset and limitations: the mode follows the Section 5.2
pending-proof/auxiliary-union-find explanation shape over Primo's current
representative-oriented proof-parent forest. It does not yet implement the full
Section 4 reorientation policy, structural-class proof forests from Section
5.5, or a reason-quality guard from Section 5.4. The emitted reasons are tested
against the existing independent naive flat-oracle checks, including a
non-representative endpoint-detour case and a nested congruence conflict. Local
same-binary probes kept the option default-off: `NEQ023_size7` regressed from
about 0.38s to 0.93s, while `NEQ027_size8`, `PEQ019_size7`, `SEQ035_size6`,
and `iso_brn_repgen_sk011` improved or stayed close to default but were often
still behind the existing bidirectional BFS option. The option is therefore a
research/tuning hook, not a reference-parity claim.

A later CC review pass profiled current head after parent-restore batching on
`NEQ016_size7`. The remaining current-head gprof sample still put
`FlatCongruenceClosure::explain_equal_into` first, followed by CaDiCaL
propagation, `merge_classes`, `process_use_list`, and `append_use_tracked`.
The profile also exposed avoidable overhead: every explanation called the
proof-forest path-length diagnostic helper even when
`--euf-proof-forest-diagnostics` was disabled. The implementation now guards
that call at the caller, and the hot merge/node-growth paths test the
proof-forest mode flags directly instead of calling a tiny helper. Default
mode therefore avoids disabled proof-forest diagnostic/maintenance calls. This
is a small implementation cleanup only; it does not change emitted reasons,
does not implement the Section 4-5 optimized proof forest, and does not close
the NEQ/reference gap by itself.

The next CC-focused review re-ran the comparison on current commit `0be41eb`
after the diagnostic-guard cleanup. Source inspection again found no new
Section 3 representative, lookup, use-list, or rollback invariant violation in
`FlatCongruenceClosure`: current probes are built from current representatives,
lookup insertions after checkpoints are removed on pop, use-list duplicate
metadata is scratch-only, and causal explanation bounds are applied to recursive
congruence-premise explanations.

The new evidence sharpens the remaining gap. On a same-binary local matrix,
disabling EUF propagation timed out at 20 seconds on `NEQ016_size7`,
`NEQ023_size7`, and `iso_icl_repgen005`, so the persistent online closure is
not optional for these rows. The experimental `proof-forest` explainer improved
`NEQ016_size7` from about 4.38 seconds to about 2.43 seconds, but regressed
`NEQ023_size7` from about 0.35 seconds to about 1.02 seconds and left
`iso_icl_repgen005` around 2 seconds with more explanation calls. On
`NEQ016_size8` with `--max-theory-conflicts 50000`, proof-forest reduced the
fixed-conflict time from about 3.64 seconds to about 2.49 seconds, but a direct
solve attempt was still running after 68.8 seconds and was interrupted. This
rejects both extremes: the proof-forest experiment is a real CC direction for
NEQ, but it is not yet a default or reference-parity solution.

The current paper-derived implementation target therefore remains narrower than
"rewrite CC": preserve the Section 3 discovery engine, then design a
reason-quality guard or a fuller Section 4-5 proof-forest/reorientation policy
that improves recursive congruence-premise explanation without changing SAT
search badly on nearby NEQ/PEQ/QG rows. Any accepted change needs independent
reason-oracle tests, QF_UF fuzzing, and SLURM evidence before it can be promoted
from local diagnostic evidence.

A follow-up micro-optimization tried to replace the proof-forest explainer's
full per-query auxiliary union-find initialization with generation-stamped lazy
node initialization. This is compatible with the paper's scratch `Explain`
union-find idea, but the local evidence rejected it as an implementation
priority. The focused EUF oracle tests passed after sentinel handling was fixed,
yet the same matrix did not improve: `NEQ016_size7` proof-forest remained around
2.5 seconds, `NEQ023_size7` still regressed relative to default, and
`iso_icl_repgen005` became slightly worse in that run. On `NEQ016_size8` with
`--max-theory-conflicts 50000`, proof-forest moved from the previous 2.49
seconds to about 2.80 seconds. The patch was reverted. The remaining cost is
therefore not dominated by zeroing the auxiliary arrays; it is still the proof
path obligations, reason shape, and resulting SAT search.

A fresh size8 review after the rejected hybrid experiment checked
`NEQ016_size8` and `PEQ002_size8` with 20-second option ablations and
50,000-theory-conflict capped proof diagnostics. The result again separates
path traversal from clause quality. On `NEQ016_size8`, proof-forest emission
was faster at the fixed conflict cap than default BFS, but increased total
explanation length and the number of conflicts with more than 64 reason
literals. On `PEQ002_size8`, the capped proof-forest run was also faster, but
the emitted reasons became much larger: the over-64-literal conflict bucket and
maximum reason length both grew substantially, matching the full-run timeout
seen when proof forest is used directly. The current conclusion is therefore
not that Section 3 congruence discovery or rollback is visibly unsound, and not
that the existing proof-forest switch should become a default. The next
paper-grounded CC implementation target is a reason-quality policy around
Section 5-style explanations: preserve the cheaper proof traversal only when it
does not emit clauses that are materially worse for SAT search, or implement a
fuller Section 4/5 proof-forest and reorientation design whose invariants
control both explanation cost and learned-clause shape.

A direct follow-up tried the simplest such guard as a temporary default-off
mode: construct the proof-forest reason first, emit it only when it has at most
64 literals, and otherwise fall back to the current BFS reason. The focused
flat-CC oracle test passed, but the mixed local matrix rejected the idea.
Guarded proof forest still timed out on `PEQ002_size8` and `PEQ003_size10`,
regressed `NEQ023_size7` and `SEQ026_size7`, and had worse PAR2 than both the
default and unguarded proof-forest modes. The lesson is that post-construction
reason-length fallback is too late and too costly. A useful guard must predict
or shape proof obligations before constructing a large proof reason, or the
implementation must move closer to the full Section 4/5 reorientation policy
so that path cost and emitted clause shape improve together.

The next temporary patch moved that cutoff into proof construction itself:
proof-forest explanation aborted as soon as the partial reason crossed the
64-literal threshold, then retried the same query with BFS. This also lost the
same mixed guard matrix. It preserved the PEQ timeouts, regressed
`NEQ023_size7` and `SEQ026_size7`, and had worse PAR2 than the default and the
unguarded proof-forest mode. Fixed literal-count fallback is therefore not the
right abstraction for this gap. Further CC work should either implement a
paper-derived reorientation/premise policy that improves proof path and clause
shape together, or move to a different ranked performance lever before spending
more time on proof-forest fallback variants.

A fresh review at commit `3a152f8` rechecked the implemented Section 3 closure
against the current code and the allowed local reference checkout. The reviewed
default path still has the expected class-list/use-list/lookup shape: current
signature probes use current representatives, lookup insertions after a
checkpoint are trailed, use-list duplicates are tolerated and removed when the
affected list is processed, and randomized flat-oracle tests cover equality
reachability and explanations under push/pop. This pass again did not identify
a concrete representative, use-list, or rollback bug that would explain the
NEQ/PEQ gap.

The same review found two explanation-shape levers that do change search but
remain mixed. First, proof-forest diagnostics on `NEQ023_size7`,
`NEQ027_size8`, `PEQ019_size7`, and `PEQ002_size8` showed no missing LCAs, so
the maintained parent forest usually has a path. The path-quality counters are
not decisive: on `PEQ019_size7`, proof-forest emission improved runtime from
about 0.90s to 0.64s, but on `PEQ002_size8` it timed out at 25s while default
solved in about 15.7s. Second, a temporary FIFO pending-merge order changed the
proof graph and search substantially. It improved `PEQ019_size7` and slightly
improved `NEQ027_size8`, but regressed `NEQ023_size7` from about 0.37s to
0.85s and left `PEQ002_size8` slower than default. The temporary FIFO patch was
reverted. Pending order is therefore a real CC tuning axis, but not a default
fix without a family-specific reason-shape policy and mixed-suite evidence.

Another proof-forest micro-experiment at commit `53a6442` added a bounded
per-reason cache for completed proof obligations inside
`explain_equal_with_proof_forest_into`, mirroring the cache already used by the
BFS explainer. The focused EUF oracle tests passed, but every measured
proof-forest row reported zero cache hits: `NEQ023_size7`, `NEQ027_size8`,
`PEQ019_size7`, `PEQ002_size8`, `iso_icl_repgen006`, and
`iso_icl_repgen005`. The patch did not fix `PEQ002_size8`, which still timed
out at 25s in proof-forest mode, and it did not improve the QG rows. The patch
was reverted. This rules out missing repeated-obligation caching as the main
proof-forest regression source; the remaining issue is first-time proof
obligation shape and the SAT effect of the emitted clauses.
