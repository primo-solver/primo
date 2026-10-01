# Sessions: scopes, replay, and retained engines

An SMT-LIB session is a sequence of commands, not one conjunction passed to the
solver once. A later assertion can change the answer; `pop` can remove both
assertions and declarations; a model is available only for the current SAT query.

## Representation and ownership

{source}`SmtSession::Implementation <src/smt/smt_session.cpp#L44-L79>` owns
the selected logic, an original-command journal with scope depths, active roots,
and the latest immutable `CheckSatOutcome`. It may also own a `Frontend` and an
`IncrementalEngine`. The engine borrows the frontend's term manager and is
destroyed before that frontend.

The journal is the authority for replay. A transformed tactic goal cannot
replace it: eliminated variables or discarded one-sided constraints may matter
to a later assertion. [Tactics](tactics.md) explains query composition;
[models](models.md) explains independently owned model snapshots.

## Command state and scope

{source}`Command dispatch <src/smt/smt_session.cpp#L233-L369>` implements:

| Command | Effect |
| --- | --- |
| `set-logic` | Selects one admitted logic in start mode. A second selection requires `reset`. |
| Declaration, definition, `assert` | Checks and records the command at the current depth; invalidates the last query result. |
| `push n` | Adds empty levels; checks depth overflow. Even `push 0` invalidates the current model in this implementation. |
| `pop n` | Removes commands from the popped levels; popping the base is an error. Even `pop 0` invalidates the current model. |
| `check-sat` | Runs a query over the active context and replaces the latest outcome. |
| `get-model` | Requires `:produce-models true` and a current SAT outcome with a typed model. |
| `reset-assertions` | Clears all journal entries, including user declarations and definitions, and releases translated/engine state; retains the logic and SMT-LIB option settings. |
| `reset` | Also clears the logic and restores `:produce-models` and `:print-success` to false. CLI configuration remains the session's supplied configuration. |
| `exit` | Stops command processing. |

`:produce-models` and `:global-declarations` are start-mode options.
`:global-declarations true` is unsupported; declarations therefore follow scope.
`:print-success` can change immediately. `check-sat-assuming`, `get-value`,
proofs, unsat cores, and output redirection are not implemented. The internal
assumptions engine does not expose the `check-sat-assuming` command.

## Default engine: replay the active journal

{source}`ensure_frontend <src/smt/smt_session.cpp#L87-L101>` creates a new
frontend when none is available, applies the logic, and replays surviving
journal commands in order. A query transfers that frontend and its assertion
roots to the one-query solver. Consequently the next query needs a new
translation, even if no assertion changed. A portfolio fallback also replays
original commands instead of reusing the previous arm's transformed goal.

Positive `pop` removes the journal suffix above the new depth and discards any
translation. Surviving declarations and definitions are rebuilt on demand.
This gives fresh SAT and theory state and permits the ordinary whole-query
preprocessing pipeline. Its cost includes translating and preprocessing the
active context repeatedly; it is not persistent SAT learning across queries.

## Assumptions engine: retain meanings, guard assertions

`--incremental-mode assumptions` is opt-in for `QF_UF` and `QF_LRA`.
The frontend, SAT backend, clausifier, atom registry, and prepared theory
structure survive queries. The implementations are
{source}`IncrementalUfEngine <src/smt/solver_engines.cpp#L12184-L12364>` and
{source}`IncrementalLraEngine <src/smt/solver_engines.cpp#L12366-L12494>`.

Each nonempty pushed scope receives a fresh selector literal $s$ lazily. If
the root of a scoped assertion is encoded by literal $r$, the permanent clause
is $\neg s\lor r$. Queries assume every active selector. Pop adds the unit
clause $\neg s$ and never reuses that selector. Base-level assertions need no
selector. Full Boolean definitions remain unconditional, with only their
asserted roots guarded; those definitions conservatively describe the fresh
Boolean variables even after the root is retired.

For example, base assertion $a$ and scoped assertion $\neg a$ produce the
clauses $a$ and $\neg s\lor\neg a$. Under assumption $s$ the query is UNSAT.
After pop, $\neg s$ disables the second requirement, and $a$ is SAT again.
Any retained clause derived from the scoped assertion must retain the relevant
assumption dependency; treating $\neg a$ as permanent would be unsound.

{source}`Frontend scope trails <src/smt/frontend.cpp#L240-L262>` remove
name bindings, definitions, numeric-declaration lists, and side-assertion
emission records. Stored term/symbol identities remain allocated. Redeclaring
a popped name creates a fresh identity. UF model construction uses active
lowered roots, not all historical atom terms, so popped declarations do not
reappear as current model obligations.

### What is rebuilt for each query

Prepared LRA atoms describe an unasserted base tableau; prepared UF structure
describes retained term and atom meanings. A new query creates a working theory
propagator and reconnects the backend's observed variables. Permanent root
assignments must be delivered again. Callback owners and their reasons remain
alive until disconnection; preserving SAT clauses is not permission to retain
a pointer into a destroyed propagator.

UF preparation processes the new atom suffix using a cursor. Other costs remain
proportional to retained state, including working-state construction and backend
reconnection. The mode therefore does not promise constant-time queries or
memory reclamation on pop. Terms, clauses, and retired selectors can accumulate
until reset; see [incremental memory](../leads/incremental-memory.md).

### Why preprocessing is restricted

Suppose a query eliminates `x` using `x = y + 1`. A later assertion `x = 0`
must receive the same substitution, or the earlier elimination must be undone.
Likewise, dropping `x >= 0` because `x` has no upper bound is unsafe if a later
assertion adds `x < 0`. A backward model-conversion record alone cannot make
those future queries sound.

{source}`Option validation <src/smt/smt_session.cpp#L105-L130>` permits
only preprocessing `auto` or `off`. Auto runs audited simplification and Boolean
normalization; both modes run required extended-term transformations, and LRA
normalizes arithmetic/equalities. The result must remain one conjunction.
Assertion-dependent substitution, congruence preprocessing, monotone/fixed
elimination, Boolean splitting, portfolios, preprocess-only, and custom CaDiCaL
options are rejected. The retained backend disables SAT preprocessing and
inprocessing and protects relevant variables.

UF additionally requires the incremental EUF checker, propagation
`auto`, `online`, or `conflict`, and no experimental Ackermann modes.
Pure-theory filtering must be `auto` or `off`; auto resolves to off here.
These checks are admission conditions, not silently ignored tuning requests.

## Follow a scoped session

Start with `:produce-models true`, select `QF_UF`, and declare Boolean `a`.
Assert `a`, then query: SAT, with `a = true`. Push a scope, declare `b`, assert
`(not a)`, then query: UNSAT. Pop the scope: `b` is no longer visible and the
old query outcome is invalid. Query again: SAT, with only the active signature
printed. A new declaration of `b` is now permitted.

Rebuild mode reconstructs the surviving context from commands. Assumptions
mode retires the scope selector and removes the name binding while retaining
old IDs internally. Both must give the same logical answers. Previously saved
C++ outcome/model objects remain snapshots; they do not authorize a new
`get-model` command without a current SAT result.

(responses)=
## Responses

{source}`CommandResponse <src/smt/smt_session.hpp#L12-L19>` separates
success, query answer, model, error, unsupported command, and exit.
Recognized unsupported commands/options leave the session context intact,
although the CLI remembers exit status 2. Translation, type, syntax, or protocol
exceptions terminate the session and produce an error; the CLI exits with 2.
An unsupported solver outcome also causes exit status 2. A resource-limited
query can return `unknown`; it is neither UNSAT nor a model-producing result.

The one-query C++ entry point rejects scoped/multi-query scripts. It is not a
substitute for the command session API, and its outcome-based errors differ
from the stream protocol.

## Sources, invariants, and evidence

SMT-LIB 2.7 §§4.1–4.2 specifies command modes and scope
{cite:p}`smtlib2026`. MathSAT5 §§4.2–4.3 motivates scope labels and protecting
incremental SAT state {cite:p}`cimatti2013mathsat`. Primo guards roots, retains
storage on pop, and does not implement that paper's clause-deletion or
incremental variable-elimination policies. Bjørner–Fazekas §3.2 and §5 explains
preprocessing replay/restoration {cite:p}`bjorner2023incremental`; these are
future designs for Primo, not implemented transformations.

The {source}`session tests <tests/unit/smt_session_tests.cpp>` exercise nested
scopes, side-constraint re-emission, stale models, lexical definitions, retained
state growth, cancellation, unsafe options, and preparation cursors. The
{source}`IPASIR-UP tests <tests/unit/sat_backend_tests.cpp>` cover the backend
boundary. The [preservation map](../coverage/frontend-sessions.md) retains
revision-specific differential, native-session and model-validation evidence.
No new solver qualification is claimed by this chapter.
