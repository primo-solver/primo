# From commands to an answer

Primo separates the lifetime of an SMT-LIB session from the lifetime of a
solving attempt. The session remembers declarations and assertions; a solving
attempt translates that context into typed terms, transforms it, and asks the
appropriate Boolean/theory engines for an answer.

This overview maps the implemented boundaries to their owning types and
functions. Detailed algorithms belong in the component chapters.

## Follow one query through the code

1. **Read a command.** {source}`execute_stream <src/main.cpp#L87-L119>` owns a
   {source}`smtlib::Parser <src/smtlib/parser.hpp#L25-L44>` and an
   {source}`smt::SmtSession <src/smt/smt_session.hpp>` for an input stream.
   `Parser::next_command()` produces command syntax; the session interprets it.
2. **Maintain the context.**
   {source}`SmtSession::Implementation <src/smt/smt_session.cpp#L44-L98>` owns
   the command journal, scope depth, frontend, optional retained engine, and
   last query outcome. The journal is the replayable source of declarations
   and assertions.
3. **Translate and solve.** In the default rebuild mode,
   {source}`SmtSession::Implementation::execute <src/smt/smt_session.cpp#L224-L232>`
   transfers the frontend and assertion roots into
   {source}`check_sat_frontend_once <src/smt/solver_session.cpp#L808-L815>`.
   The {source}`Frontend <src/smt/frontend.hpp#L25-L56>` resolves names and
   types and creates terms in its own manager. Solver and theory code consume
   core IDs rather than SMT-LIB syntax nodes.
4. **Select a route.** Quantified formulas take a
   {source}`separate quantified-UF route <src/smt/solver_session.cpp#L624-L649>`.
   Ground goals pass through preprocessing and
   {source}`make_solver_dispatch <src/smt/solver_strategy.cpp>`, using formula
   features, declared logic, and model requirements to choose a leaf.
   {source}`make_session_solver_strategy <src/smt/solver_leaves.cpp>` connects
   these leaves to concrete engines.
5. **Return an owning outcome.**
   {source}`CheckSatOutcome <src/smt/check_sat.hpp#L233-L240>` carries the result,
   diagnostic, statistics, model text, and optional immutable typed model.
   The {source}`command loop <src/main.cpp#L103-L119>` prints responses and
   handles command errors separately from solver answers.

A declared logic is therefore an admission rule and dispatch input, not a
promise that every assertion travels through the same theory engine.
Preprocessing can settle a goal or expose a simpler route.

## Representation and ownership

| Object and code | What it owns or borrows | Lifetime rule |
| --- | --- | --- |
| {source}`Frontend::Implementation <src/smt/frontend.cpp#L503-L504>` | Symbol table and term manager | The manager borrows the symbol table; the frontend keeps them together. |
| {source}`core::TermManager <src/core/term_manager.hpp>` | Term vector and hash-cons table | A `TermId` identifies a term in this manager. It is not portable across frontend instances. |
| {source}`smt::Goal <src/smt/tactic.hpp#L29-L57>` | Shared immutable goal data: assertion IDs, reconstruction records, optional typed model | Owning a vector of IDs does not own the term manager those IDs address. |
| {source}`smt::TacticContext <src/smt/tactic.hpp#L85-L141>` | Borrowed manager/statistics; invocation callbacks and deadlines | Execution state must remain within the owning solve. Strategies obtain capabilities from the executing context. |
| {source}`smt::BooleanClausifier <src/smt/clausifier.hpp#L20-L57>` | Boolean abstraction and theory-atom registry; borrowed backend and terms | The SMT layer owns the term-to-SAT mapping; the SAT backend receives literals and clauses. |
| {source}`core::Model <src/core/model.hpp#L53-L75>` | Copied signature, typed values, and interpretation tables | The completed model can survive destruction of the query frontend; evaluation of terms still needs their live manager. |

{source}`TermManager::intern <src/core/term_manager.cpp#L540-L560>` looks up a
structural key and appends a node only when that key is absent. Term identity
is `TermId` identity within that manager. This is distinct from two different
terms becoming equal under theory reasoning.

IDs remain stable when the term vector grows; borrowed references into that
vector may not. A term-creating call can invalidate a reference returned by
`get()`, including through a helper. Copy the IDs or scalar payloads needed
before the call, then reacquire the node afterwards. Reserving capacity or
observing a hash-cons hit does not establish a general lifetime guarantee.

Model ownership is a separate contract.
{source}`Model::Model <src/core/model.cpp#L57-L73>` copies the signature, and
construction validates the interpretation. The
{source}`ModelEvaluator <src/core/model.hpp#L77-L94>` borrows a live term manager
and caches values for that interpretation. The printer serializes the completed
model; it must not repair inconsistent function rows.

## Where SAT and theories meet

The {source}`IpasirUpBackend interface <src/sat/ipasir_up.hpp#L68-L94>` exposes
SAT variables, clauses, assumptions, solving, and result queries. The concrete
{source}`CadicalBackend <src/sat/cadical_backend.hpp>` contains backend-specific
integration. Primo does not implement its own SAT search engine.

{source}`ExternalPropagator <src/sat/ipasir_up.hpp#L39-L60>` is the callback
boundary for assignment notifications, backtracking, explained propagation,
external clauses, and checking a candidate assignment. Theory interaction can
happen during Boolean search as well as when a complete assignment is found.
SAT literals themselves do not contain terms; the SMT-owned atom registry
connects them to the corresponding theory obligations.

Mixed solving must also establish agreement between components. The production
`QF_UFLIA` engine enters
{source}`solve_component_recipe <src/smt/solver_engines.cpp#L10382-L10414>`;
component capabilities and model fragments are described by
{source}`TheoryComponent's interface types <src/theory/theory_component.hpp>`.
`QF_UFLRA` retains a
{source}`dedicated engine <src/smt/solver_engines.cpp#L10957-L10977>`.
These routes must not be described as one uniform implementation merely because
both combine uninterpreted functions with arithmetic.

## Sessions and result validity

The default is
{source}`IncrementalMode::Rebuild <src/smt/check_sat.hpp#L242-L249>`.
Rebuilding solving state does not prevent the user from making repeated queries
or using scopes. In this mode, replay constructs a frontend from the current
journal after the previous one has been consumed.

The opt-in assumptions mode retains additional frontend/engine state and is
admitted only for `QF_UF` and `QF_LRA`. Its
{source}`option validation <src/smt/smt_session.cpp#L100-L127>` excludes
transformations and modes that are unsafe or unsupported in that setting.
SAT decision-level backtracking and SMT-LIB `push`/`pop` are different lifetimes.

{source}`Scope changes and get-model <src/smt/smt_session.cpp#L290-L332>` show
the user-facing boundary: changing the active context invalidates the last
answer, and `get-model` requires both model production and a current SAT model.
An old owning model may still exist in memory without being valid for the
session's new context.

`unknown`, `unsupported`, and command errors are distinct from `unsat`.
{source}`CheckSatResult <src/smt/check_sat.hpp#L214-L219>` and
{source}`CommandResponse <src/smt/smt_session.hpp#L12-L21>` keep those layers
separate. For requested ground models, the
{source}`finalization path <src/smt/solver_session.cpp#L650-L674>` optionally
checks the original assertions and then serializes the model. An internal
model inconsistency is not a logical UNSAT result.

## Worked example and tests

In the [first example](../index.md#build-and-solve-a-first-problem), the session
records declarations for `a`, `b`, and `f`, followed by `a = b` and
`f(a) != f(b)`. The frontend creates sorted core terms. The conjunction is
unsatisfiable because function congruence requires `f(a) = f(b)`.

Preprocessing may expose that contradiction before a full SAT/theory exchange;
the example does not promise a particular callback trace. The abstraction
boundary remains the same: the
{source}`BooleanClausifier <src/smt/clausifier.hpp>` maps Boolean structure and
atoms to SAT literals while theory code reasons about terms.

Useful entry points for checking these contracts are:

- {source}`core identity and construction tests <tests/unit/core_tests.cpp>`;
- {source}`incremental_closure_discovers_unary_congruence_with_lookup <tests/unit/euf_solver_tests.cpp#L248-L269>`;
- {source}`session scope and reuse tests <tests/unit/smt_session_tests.cpp>`;
- {source}`model representation tests <tests/unit/model_tests.cpp>`;
- {source}`SAT backend protocol tests <tests/unit/sat_backend_tests.cpp>`;
- {source}`strategy lifetime tests <tests/unit/strategy_lifetime_tests.cpp>`.

These are code pointers and regression coverage, not substitutes for the
[fresh correctness gates](../development/correctness.md).

## Sources and implementation status

This page is an original integration overview based on the interfaces and
implementations linked above. Existing Primo design notes provide the detailed
{source}`session replay contract <docs/incremental-smt.md>`,
{source}`model ownership contract <docs/model-interpretation.md>`, and
{source}`tactic contract <docs/tactics.md>`.

The existing {source}`IPASIR-UP paper map <docs/papers/ipasir-up.md>` and
{source}`combination implementation notes <docs/modular-theory-combination.md>`
record algorithm sources and implemented subsets. Their detailed paper-to-code
audits remain separate chapter work. This overview does not establish full
paper-algorithm implementation, generic support for additional theories, proof
production, or completeness for bounded integer/quantified search.
