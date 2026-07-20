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

- `Pending`: `FlatCongruenceClosure::pending_`, containing explicit equality
  merges and congruence merges discovered through lookup hits.
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
- `lookup_` entries are keyed by current flat representatives at insertion time.
  Stale keys may remain after a merge but are not queried because new probes are
  built from current representatives.
- `use_lists_[r]` contains definitions that must be reconsidered when `r` stops
  being a representative. Duplicates are tolerated while appending and are
  suppressed with a generation-stamp scratch vector when a list is processed.
- Disequalities are stored as theory literals and checked after each merge.
- Explanations are sound SAT-literal sets collected from an explicit equality
  graph. They are not the optimized proof forests of Sections 4 and 5.

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
