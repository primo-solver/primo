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
unrelated disequalities are skipped.

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
  being a representative. Duplicate suppression is local to a list.
- Disequalities are stored as theory literals and checked after each merge.
- Explanations are sound SAT-literal sets collected from an explicit equality
  graph. They are not the optimized proof forests of Sections 4 and 5.

## Complexity Expectations

The implemented congruence discovery follows the Section 3 affected-use-list
shape rather than rescanning all registered term pairs. Class merging uses the
paper's smaller-into-larger rule, so each term changes representative
logarithmically often. The current lookup table is a sparse hash table.

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

## Counters

The engine exposes counters for:

- registered terms and flat application definitions;
- explicit and congruence merges;
- pending queue pops;
- lookup probes and hits;
- disequality checks and conflicts;
- explanation length;
- backtracks.

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
