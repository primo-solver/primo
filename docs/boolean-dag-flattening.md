# Boolean DAG representation and bounded associative flattening

This note specifies the repair for the frontend memory failure observed on
`cpachecker-induction.cs_fib_true-unreach-call.i.smt2`. The input is a compact
SMT-LIB DAG of nullary definitions, dominated by definitions of the form
`d_n = (and d_(n-1) atom)`. At commit `4e19d1b`, a controlled SLURM probe was
killed after 180 seconds at 154,539,380 KB RSS without reaching one theory
conflict.

The failure is before SAT or theory solving. `TermManager::mk_and` and
`mk_or` flatten same-kind children during term construction. Eagerly compiled
nullary definitions retain every flattened prefix. A streaming analysis of the
input counted 361,983,696,912 child slots that complete eager flattening would
retain, or about 1.45 TB for 32-bit `TermId` values before vector capacity,
hash-consing, parser, and map storage.

## Algorithmic basis and scope

Primo's existing `BooleanClausifier` uses a cached Tseitin-style encoding: each
reachable Boolean `TermId` has at most one SAT literal and nested `And`/`Or`
nodes receive local defining clauses. That encoding operates on a DAG and does
not require associative connectives to be globally flat. The representation
repair does not change the clausification algorithm.

Bounded associative flattening is an original preprocessing policy, not a claim
to implement an optimized algorithm from a paper. Its purpose is experimental:
some later preprocessing may benefit from wider nodes, but term construction
must remain safe when the source intentionally shares long prefixes.

## Ownership and data structures

- `core::TermManager` owns the canonical term DAG. `mk_and` and `mk_or` perform
  only local sort checking, constant elimination, and duplicate-child removal.
  They do not inspect a same-kind child's operands.
- `smt::flatten_associative_booleans` is an optional preprocessing pass over the
  reachable assertion DAG. A `TermId`-indexed memo table stores one rewritten
  result per visited source node; an explicit post-order worklist avoids input-
  depth recursion.
- `smt::BooleanClausifier` consumes nested DAGs directly. Top-level conjunction
  assertion must also use an explicit worklist so a definition chain cannot
  consume the C++ call stack.
- The SAT backend continues to receive only literals and clauses.

## Operations and invariants

Core construction has these invariants:

1. Constructing a Boolean node copies only the direct operands supplied by its
   caller. It is therefore linear in direct arity, independent of descendant
   size.
2. Hash-consing preserves shared prefixes by `TermId`.
3. Removing `true` from `And`, removing `false` from `Or`, absorbing the
   opposite constants, and removing duplicate direct operands preserve Boolean
   equivalence.
4. Nested and flat forms are both legal core terms. No consumer may assume
   global associative flattening.

The optional pass takes a positive maximum arity `K`. For each visited `And` or
`Or`, it considers each same-kind child in source order. It replaces that child
by its direct operands only when the resulting parent, including all remaining
direct children, would have at most `K` operands. Otherwise it retains the
child node and records a threshold stop. Rebuilding is bottom-up and memoized.

For fixed `K`, the pass uses `O(V * K)` copied child identifiers in the worst
case over `V` reachable Boolean nodes, and `O(V)` worklist and memo storage.
With the pass disabled, the core representation of the CPAchecker chain uses
`O(V)` nodes and child identifiers. The pass deliberately gives no bound below
the source arity of an explicitly wide SMT-LIB `and` or `or`; it only bounds
arity growth caused by flattening.

## Statistics

The pass reports:

- `:preprocessing.boolean-flatten.attempts`;
- `:preprocessing.boolean-flatten.accepted`;
- `:preprocessing.boolean-flatten.threshold-stops`;
- `:preprocessing.boolean-flatten.operands-copied`;
- `:preprocessing.boolean-flatten.rewrites`;
- `:preprocessing.boolean-flatten.max-arity`.

All remain zero when the pass is disabled. The command-line option is
`--boolean-flatten-threshold N`; `0` disables the pass.

## Proof obligations and tests

- Core tests must show that nested `And`/`Or` children remain shared and that
  duplicate direct children are removed.
- Preprocessing tests must cover flattening below the threshold, retaining a
  nested child at the threshold, deterministic operand order, shared subterms,
  and deep chains without recursion.
- Solver tests must cover equivalent SAT and UNSAT formulas with the pass off
  and on, and a long nullary `And` definition chain whose stored node arity
  remains bounded before preprocessing.
- Existing QF_UF, QF_LRA, and QF_UFLRA focused tests must pass because this is a
  cross-logic core representation change.
- The merge fuzz gate must exercise both the default disabled pass and at least
  one positive threshold against trusted references.
- The CPAchecker witness must be rerun with a hard outer resource guard. The
  accepted implementation must reach frontend and preprocessing statistics
  without superlinear resident-memory growth.

Known limitation: disabling flattening can change preprocessing visibility and
SAT search because nested disjunctions may receive additional Tseitin literals.
That is why default readiness requires benchmark comparison rather than being
inferred from the memory fix alone.
