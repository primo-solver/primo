# Goals, tactics, and strategies

A solver query passes through transformations before it reaches an engine.
Primo represents that work as **tactics**, which transform goals, and
**strategies**, which describe how to compose those tactics. A goal carries
both its current assertions and the information needed to reconstruct a model
of the original assertions. This chapter follows the default ground-query
route; [preprocessing](preprocessing.md) gives its transformation order.

The basis is de Moura and Passmore's *The Strategy Challenge in SMT Solving*,
§3 {cite:p}`deMoura2013`: assertions within a goal are conjunctive, subgoals
are alternatives, and transformations compose with model conversions. Primo
implements a subset of that framework, with the differences described below.

(tactics-representation)=
## Representation

| Type and code | Meaning and ownership |
| --- | --- |
| {source}`Goal <src/smt/tactic.hpp#L29-L57>` | Ordered assertion IDs, model-reconstruction records, an optional immutable typed model, and a typed inconclusive reason with diagnostic text. Updates return a new value and share unchanged immutable payloads. |
| {source}`AnyOf <src/smt/tactic.hpp#L59-L76>` | A nonempty, shared immutable sequence of alternative goals. Its constructor rejects an empty sequence. |
| {source}`Tactic <src/smt/tactic.hpp>` | A named executable function from a context and `AnyOf` to another `AnyOf`, with an incremental-safety classification. Copies share the implementation. |
| {source}`TacticContext <src/smt/tactic.hpp#L85-L141>` | Borrows the executing term manager and statistics; supplies cancellation, deadlines, model requirements, and a fresh-constant factory. |
| {source}`Strategy <src/smt/strategy.hpp>` | Immutable composition graph: leaves, sequence, conditional, repetition, ordered fallback, timed execution, and skip. Leaves carry an identifier, explicit parameters, and an executable tactic. |

Owning assertion IDs does not own their terms. A `Goal` and any captured term
snapshots must be used with the manager that gives those IDs meaning. Tactics
create all replacement terms through that manager. They may allocate terms
and update statistics even though the input goal itself is immutable.

Preprocessing leaves obtain execution services from `TacticContext` and
capture options by value. This avoids borrowing the context that constructed
the strategy. A prepared finite-domain strategy can also capture assertion
and clause IDs, so its snapshots still have the term-manager lifetime above.
In contrast, {source}`make_session_solver_strategy <src/smt/solver_leaves.cpp>`
deliberately builds closures borrowing the frontend, statistics, and context
of one solve. That concrete solver strategy must remain inside that solve.

(tactics-goal-truth)=
## Conjunction within a goal, alternatives between goals

For assertions $A_1,\ldots,A_n$, a goal denotes
$A_1\land\cdots\land A_n$. An `AnyOf` containing $G_1,\ldots,G_k$ denotes
their disjunction. Proving one alternative satisfiable settles the whole
disjunction; proving it unsatisfiable requires eliminating every alternative.

Each transformation owes the same satisfiability equivalence: its input has
a model exactly when at least one output goal does. When it changes the
vocabulary, it must also supply the reconstruction needed to turn a successful
output model into an input model. Immutable storage does not establish these
logical properties; they are obligations of each leaf.

The {source}`truth query <src/smt/tactic.cpp#L190-L241>` recognizes only
syntactic terminal forms:

- an empty assertion sequence is true;
- a goal containing an explicit `false` assertion is false;
- every other goal is inconclusive;
- an `AnyOf` is true if any goal is true, and false if all goals are false.

This query does not call a solver. In particular, an assertion sequence
containing only `true` is not the empty sequence; a transformation must remove
that assertion to expose the terminal form. Non-Boolean assertions encountered
by the query are contract errors.

The {source}`solver-leaf adapter <src/smt/solver_tactic.cpp>` turns engine
answers into this representation. SAT clears the successful goal's assertions
and attaches its typed model. UNSAT removes that alternative; if none remain,
the adapter returns a single false goal. Unknown or unsupported results retain
the goal with a typed reason. Diagnostic strings do not control fallback.

(tactics-composition)=
## Composition and stopping

The runtime combinators live in
{source}`tactic.cpp <src/smt/tactic.cpp#L273-L383>`; strategy compilation
constructs them from the graph.

| Operation | Execution rule |
| --- | --- |
| `skip` | Return the input unchanged. |
| `sequence` | Feed each result into the next child; stop when the combined result is syntactically terminal. |
| `conditional` | Evaluate the predicate on each current inconclusive goal, then run the selected child on it. |
| `repeat` | Apply the child until a terminal result, fixed point, continuation-policy stop, or iteration limit. Zero iterations returns the input. |
| `try_in_order` | Run each arm on the **original** input. Return the first terminal result; if none is terminal, return the last result. Cancellation stops the fallback. |
| `try_for` | Apply the child with a context deadline bounded by any enclosing deadline. Mark a still-inconclusive result as resource-limited if time has expired. |

These rules distinguish two different operations. A fallback strategy
`try_in_order(A, B)` gives `B` the same formula that `A` received. It does not
give `B` a partly simplified output from `A`. A sequence `A; B` does pass
that output, including its model conversions, to `B`.

`make_goal_tactic` applies a leaf to each inconclusive alternative and flattens
its returned alternatives. It preserves terminal goals. Some preprocessing
groups instead use the internal `complete_sequence` helper: every phase of
the group runs, including bookkeeping phases, before the enclosing strategy
can stop. It is not interchangeable with the public short-circuiting sequence.

Repetition normally compares complete goal values, first exploiting shared
storage and otherwise comparing contents. The production
preprocessing loop supplies an assertion-vector comparison: accumulating
bookkeeping alone must not keep simplification running. On a fixed point,
`repeat` returns the **new** result, retaining conversions added by that last
iteration. Replacing it with the previous result could lose a reconstruction
step even though both assertion vectors match.

The framework adds no implicit term-DAG traversal for this comparison: the
preprocessing check compares ordered assertion IDs. Reachable-size scans are
explicit work of its continuation policy. The iteration limit bounds the
number of calls, not the work or allocation within an individual leaf.

Timeouts are cooperative. `Tactic::operator()` checks cancellation and the
deadline before invocation; a long-running child must also poll its context.
`try_for` creates no worker process and cannot forcibly interrupt a child.
A terminal answer returned after the deadline is retained. Contract errors
and backend exceptions propagate rather than becoming another fallback arm.

Immutable goals isolate formula and reconstruction changes between arms.
They do not roll back term allocations, fresh-symbol creation, or statistics.
The scheduler is sequential; no parallel tactic execution is implemented.

(tactics-strategy-identity)=
## Strategy identity and compilation

{source}`Strategy::canonical <src/smt/strategy.cpp#L230-L294>` serializes
composition, leaf identifiers and parameters, named probes and repeat policies,
and time budgets. Parameters have a canonical ordering. `fingerprint()` hashes
this string with FNV-1a; strategy equality compares the canonical strings.
The fingerprint is a compact configuration identifier, not a source hash or
a correctness certificate. It does not describe the implementation inside a
leaf's callable or the state of an executing context. Reproducing an experiment
still requires the solver revision, options, inputs, and resource limits.

{source}`compile <src/smt/strategy.cpp#L296-L378>` walks the composition graph
in postorder and builds runtime tactics. Both compilation and canonicalization
use explicit traversal stacks, so a deeply nested strategy does not require
an equally deep C++ call stack during those operations. Execution still follows
the composed callables; this is not a bound on every leaf's stack use.

The probe interface separates numeric measurements from Boolean tests. The
current built-in examples are assertion count and comparison with a limit.
A probe sees the goal at its point in the sequence, after preceding
transformations. The richer production preprocessing selector is a separate
profile-analysis function, not a learned strategy synthesizer.

(tactics-production)=
## From a session query to an engine

The default rebuild path in
{source}`solver_session.cpp <src/smt/solver_session.cpp#L624-L793>` first
routes quantified assertions to the dedicated quantified-UF implementation.
For ground assertions it prepares and executes the
[preprocessing strategy](preprocessing.md). The resulting alternatives retain
their own reconstruction records throughout the subsequent solver call.

The session drops false alternatives, handles typed interruption reasons,
and can return SAT immediately for an empty surviving goal. If a model was
requested, even that immediate answer must reconstruct an interpretation.
For statistics and shape heuristics the session collects assertions from all
surviving goals into one vector. That vector is not solved as a conjunction:
the actual solver receives the original disjunctive `AnyOf`.

{source}`make_solver_dispatch <src/smt/solver_strategy.cpp>` examines each
goal's declared logic, current formula features, and model requirements in
this priority order:

| Condition | Route |
| --- | --- |
| Declared `QF_LIA` or `QF_UFLIA` | Corresponding integer engine. |
| Arithmetic combined with equality/UF features | Mixed Real engine; model requests can also require this route after simplification removes arithmetic atoms. |
| Arithmetic remains | LRA engine. |
| A model needs declared Real symbols, with no equality/UF features | LRA model-construction route. |
| Equality or UF applications remain | EUF engine. |
| Otherwise | Boolean engine, with EUF fallback on an inconclusive Boolean result. |

The exact mixed-route predicate also accounts for declared `QF_UFLRA` and
Real symbols. An engine cannot be selected solely by counting the remaining
arithmetic atoms: eliminated terms and declared symbols still affect the
requested model. [Theory combination](combination.md) explains which concrete
mixed coordinator each route uses.

The public outcome conversion first checks typed inconclusive reasons, then
the syntactic truth of the result. It does not silently turn a flagged
interruption into a definitive answer. After SAT, the session can evaluate
the original assertions against the reconstructed model before printing it;
see [model construction](models.md).

(tactics-model-example)=
## A transformation and its model

Consider the integer goal

$$G=[x=y+2,\quad y\ge1].$$

A substitution step eliminating $x$ returns the reduced assertions and records
$x\mapsto y+2$ in that goal's conversion chain. The defining equality becomes
`true`, leaving $y\ge1$ as the only nontrivial obligation. A solver
model with $y=1$ extends to $x=3$. Returning only the reduced model would leave
the original symbol $x$ unexplained.

For a chain of transformations $G_0\to G_1\to G_2$, a model of $G_2$ must
first be converted to a model of $G_1$, then to a model of $G_0$.
{source}`ModelConversionChain <src/smt/model_reconstruction.hpp>` stores typed
steps in execution order and replays them backwards. Its immutable storage
lets split goals share their common history and append different steps.
The [model chapter](models.md#following-a-model-back-through-preprocessing)
explains dependencies, replay epochs, and the concrete conversion variants.

(tactics-experiments)=
## Splitting and session portfolios

The default strategy produces one goal. The optional
{source}`Boolean splitter <src/smt/experimental_tactics.cpp>` appends all
truth assignments to up to $k$ selected Boolean atoms, producing $2^k$
alternatives. For $G=[p\lor q]$ and selected atom $p$, these are
$[p\lor q,\neg p]$ and $[p\lor q,p]$. Either satisfying model solves $G$.
Proving only the first alternative unsatisfiable would not settle $G$.

Selection follows a deterministic term traversal, preferring Boolean symbols
and predicate applications before equality/comparison atoms. The option
`--experimental-split-bools` defaults to zero and rejects values above 16.
The implementation materializes every alternative, so the cap bounds an
exponential allocation; it is not a lazy cube scheduler. Alternatives are
solved sequentially, retaining the first SAT model.

The separate {source}`QF_UF session portfolio <src/smt/session_portfolio.cpp>`
retries complete session executions. Its first arm uses basic preprocessing,
polarity phases, and a theory-conflict budget. Only an `Unknown` answer with
typed reason `ResourceLimit` triggers the fallback with the requested options.
Ordinary unknown, cancellation, unsupported results, and exceptions do not
trigger that retry. This portfolio rebuilds session state; it is not the
goal-level `try_in_order` combinator.

(tactics-incremental)=
## Scope and incremental use

An eliminated assertion cannot generally be forgotten when a later user
assertion arrives. In the example above, adding $x=0$ to the reduced formula
$y\ge1$ without restoring or translating the definition would miss the
contradiction. Model conversion repairs a completed model; it is not an
incoming-assertion translator.

The default session avoids this problem by rebuilding from the active original
assertions on each query. Assumptions-based engines use a smaller audited
set of leaves. `IncrementalSafety` distinguishes context-independent rewrites,
conservative extensions, and unclassified tactics. A context requiring
incremental safety rejects unclassified execution. A conservative extension
such as extended-term lowering must guard its side constraints with the
assertion that introduced them. General composition, solve-eqs, and monotone
elimination are not thereby authorized for incremental reuse.

The concrete contracts remain in the
{source}`LRA assumptions design <docs/assumption-incremental-lra.md>` and
{source}`UF assumptions design <docs/assumption-incremental-uf.md>`.
Primo currently has no user-facing tactic language, proof/unsat-core conversion
framework, or general incremental tactic restoration mechanism.

(tactics-evidence)=
## Checks and implementation status

| Contract | Existing check |
| --- | --- |
| Disjunctive truth, immutable updates, reverse conversion, fallback input, fixed-point output, cancellation, deadlines, and deep strategy construction | {source}`tactic tests <tests/unit/tactic_tests.cpp>` |
| Context services belong to the execution, not the construction | {source}`strategy lifetime tests <tests/unit/strategy_lifetime_tests.cpp>` |
| Legal profile composition, integer restrictions, and actual solver leaves | {source}`composition tests <tests/unit/tactic_composition_tests.cpp>` |
| Public options, repeated-run statistics, reference answers, direct model validation, and leaf activity | {source}`composition matrix workflow <scripts/tactic_composition_matrix.py>` and {source}`profile manifest <benchmarks/tactic-composition-matrix.json>` |

The matrix's {source}`SLURM gate <scripts/slurm_tactic_composition_matrix.sbatch>`
exercises generated formulas under applicable profiles. It checks composition
correctness and activity, not performance superiority. These are existing
checks; this documentation review does not report a new solver evaluation.
The [preservation map](../coverage/tactics-preprocessing.md) locates earlier
verification and experiment records.

The implemented paper subset is ordered goals, disjunctive subgoals, model
conversion, probes, and sequential composition {cite:p}`deMoura2013`.
Primo uses assertion-derived terminal states and typed inconclusive reasons
instead of the paper's complete return algebra. Its ordered fallback retries
inconclusive arms, and its timeout is cooperative. Proof converters,
approximation attributes, parallel tacticals, and the paper's full tactic
catalogue are outside the implementation. Profile selection and the concrete
pass schedule are original Primo policies documented in the next chapter.
