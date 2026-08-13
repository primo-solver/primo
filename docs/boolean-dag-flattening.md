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
  assertion uses an explicit worklist and a per-assertion visited set, so a
  definition chain cannot consume the C++ call stack and a reconvergent DAG is
  clausified once per reachable `TermId`.
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
5. Top-level conjunction clausification and top-level disjunction-shape
   collection visit each reachable shared node at most once per root.

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

The post-preprocessing threshold also bounds the optional
`--boolean-implies-rewrite=clause` pass. That pass may widen
`p => (q or r)` into the three-literal `or` node but does not distribute a
compound antecedent or consequent. Its `all` mode only creates the binary
`(or (not A) B)` equivalence; the ordinary bounded flattening pass decides
whether any resulting nested `Or` is widened.

## Proof obligations and tests

- Core tests must show that nested `And`/`Or` children remain shared and that
  duplicate direct children are removed.
- Preprocessing tests must cover flattening below the threshold, retaining a
  nested child at the threshold, deterministic operand order, shared subterms,
  and deep chains without recursion.
- Solver tests must cover equivalent SAT and UNSAT formulas with the pass off
  and on, and a long nullary `And` definition chain whose stored node arity
  remains bounded before preprocessing. A Fibonacci-shaped reconvergent
  `And` DAG must reach its two shared `Or` leaves once and emit two clauses.
- Existing QF_UF, QF_LRA, and QF_UFLRA focused tests must pass because this is a
  cross-logic core representation change.
- The merge fuzz gate must exercise both the default disabled pass and at least
  one positive threshold against trusted references.
- The CPAchecker witness must be rerun with a hard outer resource guard. The
  accepted implementation must reach frontend and preprocessing statistics
  without superlinear resident-memory growth.

## Validation evidence

Commit `e9b8c39` passed `make lint`, `make check`, `make asan-test`, and
`make docs`. A release build was also required because GCC's
`-Wmaybe-uninitialized` found an exhaustiveness invariant that the debug build
and local clang-tidy did not prove; `ba3aecc` makes the invalid sentinel and
post-switch check explicit.

The exact CPAchecker witness now completes preprocessing with flattening
disabled. The source-exact local debug build took 52.37 seconds and 3,071,672
KB peak RSS. The clean SLURM release build took 19 seconds and 92,168 KB peak
RSS; parsing, frontend compilation, and preprocessing took 4.73, 5.09, and
5.52 seconds.
Its initial and final preprocessing term sizes were 1,805,077 and 1,816,291,
and every `:preprocessing.boolean-flatten.*` counter was zero. This replaces
the earlier same-input release probe that reached 154,539,380 KB and its
180-second hard guard without reaching one theory conflict.

The exact release commit also passed two differential SLURM gates. With the
pass disabled, 3,000 mixed QF_UFLRA cases and 1,000 finite-domain cases all
passed. The same 4,000-case gate passed with
`--boolean-flatten-threshold=8`. In each mode, all 11 tracked QF_UFLRA seeds
passed and YinYang completed 100 successful mutations without a reported bug.
On the established 45-file QF_UFLRA preprocessing-opportunity sample, the
default-disabled pass solved 45/45 with zero wrong or unsupported results and
PAR2 2.174189 seconds. That sample is a same-commit sanity result, not a
before/after attribution; the oversized witness is the direct memory evidence.

An exact local default A/B against the pre-change `c1a8288` release used the 12
tracked files in `benchmarks/boolean-dag-default-ab-12.txt`. All answers and
return codes agreed across three runs per binary, and every non-time counter was
deterministic. Nine files had identical shared non-time counters. The current
binary additionally reports the six zero-valued flatten counters. One compact
finite-domain seed changed three preprocessing counts by one.

The two MathSAT-derived files expose the known search limitation. On the
decoupled file, preserving the nested `Or` chain increased SAT variables from
268 to 744 and clauses from 695 to 2,105, but median total time improved from
46.9 to 35.1 milliseconds. On the coupled file, variables increased from 302
to 901, clauses from 758 to 2,556, decisions from 1,104 to 3,840, and median
time increased from 34.6 to 40.9 milliseconds. Enabling a threshold large
enough to flatten both formulas reproduced all parent non-time counters on the
decoupled file and all but three one-count differences on the coupled file.

Therefore the default preserves results on this sample, but it deliberately
does not preserve exact encoding or search behavior. Nested disjunctions below
the top-level shape can receive additional Tseitin literals. This remains an
efficiency target for DAG-aware clausification and is why default readiness
cannot be inferred from the memory fix alone.

(post-preprocessing-application)=
## The second application point, and why it was needed (2026-08-11)

The repair above is correct and the memory result is real -- reproduced here
on a synthetic 20,000-link `d_n = (and d_(n-1) atom)` chain, which peaks at
**1.88 GB** on the pre-repair binary and **57 MB** after. But the pass as
first landed ran **only before** the rewriting preprocessing passes, and that
turned out not to be enough.

Those passes build fresh `And`/`Or` nodes through `TermManager`, which -- by
this note's own design -- no longer flattens at construction. So they
reintroduce nesting the *input* never had, after the pre-pass has already gone
past. The symptom is diagnostic: on affected inputs the pre-pass reports
`attempts = 0` (there is genuinely nothing nested yet) while the finished
formula carries ~10% more nodes than it did before the repair.

Measured on `yPositive-SIscaled50.bpl_Iteration1_Loop_6-phaseTemplate`:
`:preprocessing.term-size.initial` is **identical** either way (28,011) and
only `term-size.final` diverges (151,804 -> 162,106), which is what localises
the cause to preprocessing rather than parsing.

Cost of the gap, against the commit before the repair:

| instance | before repair | after repair | with post-pass |
|---|---:|---:|---:|
| `yPositive-SIscaled50` | 9.0s / 485 MB | 22.7s / 605 MB | **8.9s / 485 MB** |
| `Gcd_havoc` | ~4.0s | 9.7s / 76 MB | **4.0s / 55 MB** |
| `sas2.t2.c` | ~13.4s | 21.5s / 258 MB | **12.4s / 237 MB** |

Note memory moves the *same* way as time: flattening removes intermediate
Tseitin nodes, so on these inputs it is cheaper on both axes. The pathological
chain is the opposite case, which is exactly why this is a bounded, tunable
policy rather than a fixed choice.

**`--boolean-flatten-post-threshold`, default 2048**, chosen by measurement.
256 restores most QF_LRA inputs but leaves one of 55 short --
`simple-scaled200` hits 20,200 threshold stops there and needs 2048 to match
the pre-repair clause and decision counts exactly. Raising the bound costs
nothing where the bound exists to protect: on the 94 MB
`cpachecker-induction.cs_fib` instance, 2048 peaks at 3,346,944 KB against
3,347,208 KB at 256, a 264 KB *saving*, with threshold stops falling from 17
to 1.

The bound still matters -- it is what stops a pathological input from
unbounded arity growth -- but it is not the axis that decides memory on real
inputs, because the pass rewrites each reachable node once instead of
materialising every prefix. `0` disables the pass entirely.

Broader check, 34 QF_LRA instances solved by both settings: **0 answer
disagreements**, median time ratio **0.76**, six instances more than 30%
faster and none more than 30% slower.

### Are we back to the old behaviour on QF_LRA?

Effectively yes, and where it differs it is better. Against the pre-regression
binary over 55 QF_LRA instances (45 solved by both):

- **0 answer disagreements**, and **nothing lost or gained** -- the same
  instances solve.
- Time median **1.02**, with **no instance more than 30% faster or slower**.
- Clause counts: 34 identical, 10 **fewer** now, 1 more (closed by the 2048
  default).

The instances with fewer clauses are not a flattening effect. `assert_positive`
gained a per-assertion visited set in the same commit, so a subterm reached
through several paths of the top-level `And` spine is clausified once instead
of repeatedly. Terms and variables are unchanged there; only redundant
duplicate clauses disappear. That part of the DAG work is a straight
improvement and is deliberately kept.

### Verified on the motivating instance

`cpachecker-induction.cs_fib_true-unreach-call.i.smt2` -- the 94 MB input this
note was written about -- is in `sandbox/non-incremental/QF_UFLRA`, and the
post-pass **reduces** its peak memory rather than threatening it:

| setting | peak RSS | flattenings accepted |
|---|---:|---|
| `--boolean-flatten-post-threshold 0` | 4.85 GB | 0 |
| `--boolean-flatten-post-threshold 256` (default) | **3.19 GB** | 3,512,924 (17 threshold stops, max arity 243) |

Both still time out at 180s, so this is a memory result, not a solved/unsolved
one. The direction is the same as on QF_LRA and for the same reason:
flattening removes intermediate Tseitin nodes, so where it applies it costs
less, not more. The 17 threshold stops are the bound doing its job -- without
it those nodes are where unbounded growth would start.

This is the opposite of what the bound was expected to buy. The risk it
guards against is real but is a property of flattening *at construction*,
where every prefix of a shared chain is materialised; the post-pass rewrites
each reachable node once, so the same policy is cheap when applied to a DAG
rather than to a construction stream.

Across 28 sampled `QF_UFLRA` instances: 0 answer disagreements, all 28 solved
under both settings, and no instance's peak RSS moved by more than 5% either
way.
