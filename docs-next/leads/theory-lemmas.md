# Dynamic Ackermannization and theory-lemma retention

**Status: open leads; implementation deferred. Owner: unassigned.**

The [Yices literature survey](../papers/yices.md) distinguishes three things:
generating a useful congruence clause, constructing an explanation only when
requested, and retaining a theory clause for later Boolean propagation.
The last two can affect both runtime and incremental memory.

## Conflict-driven Ackermann clauses

Primo has default-off, bounded propagation over already available equality
atoms. It also has diagnostics and a reverted older clause-generation
experiment. The remaining lead is the 2006 paper's selector: use congruence
steps from conflict explanations to choose which clauses to materialize.
Fresh atom registration, budgets, deduplication, and safe clause deletion
need an explicit design. This is not a proposal to restore the old scan loop.

The [paper-to-code table](../papers/yices.md#what-primo-currently-implements)
records the existing types, tests, defaults, and assumptions-mode restriction.
The earlier local rejection remains historical evidence, not a current result.

## Which theory lemmas should survive?

The 2016 talk suggests retaining small theory explanations as SAT clauses
when insertion does not require backtracking. But CaDiCaL already documents
retaining external reason clauses. Start by measuring its effective policy
and memory cost, then decide whether additional selection or deletion controls
would help. Building another store is not the first step.

The deletion part now has a separate
[safe-forgetting lead](theory-lemma-forgetting.md), grounded in papers that
state termination conditions. It must account for progress across restarts
and the outer SMT loop as well as live reason ownership.

The [worked explanation](../papers/yices.md#the-2016-talk-what-lemma-caching-means)
shows why caching a reason clause differs from retaining a C++ explanation
vector or learning the final conflict resolvent. Backend watch handling stays
inside `sat/`; theory terms never enter a SAT literal.

## Next decision

Use the survey's [acceptance conditions](../papers/yices.md#open-investigations-and-acceptance-conditions)
to prepare two independent investigations. Compare each with the current
solver, measure useful reuse and clause/memory growth, and preserve correctness
under backtracking, repeated checks, and scope retirement. Coordinate memory
measurements with the [incremental-memory lead](incremental-memory.md).
Neither lead is a promise of a performance improvement or a changed default.
