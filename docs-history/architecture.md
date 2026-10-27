# Architecture

```{note}
This chapter preserves the old documentation structure. Read the
<a href="../../implementation/architecture.html">current architecture overview</a>
for module responsibilities and data flow. The current explanation of
<a href="../../implementation/frontend.html#let-bindings">SMT-LIB <code>let</code> bindings</a>
belongs to the frontend chapter.
```

Primo is organized as layered components with explicit data ownership. This page describes the implementation that exists now, not the intended final solver.

For supported routes, see the
<a href="../../capabilities.html">current capability summary</a>. The
<a href="../../implementation/combination.html">combination chapter</a>
distinguishes the dedicated Real coordinator from the general component driver.

## Layers

- `smtlib/` parses SMT-LIB commands into lightweight syntax objects.
- `core/` owns sorts, symbols, hash-consed terms, and immutable typed models
  with a shared evaluator.
- `sat/` owns propositional variables, literals, clauses, assumptions, the
  backend-neutral IPASIR-UP callback interfaces, and the concrete CaDiCaL
  adapter.
- `smt/` owns conversion of SMT-LIB commands to core terms, preprocessing, Boolean abstraction,
  clausification, tactic composition, pure UF/LIA/LRA solver orchestration, the
  specialized mixed QF_UFLRA coordinator, and the N-component driver used by
  QF_UFLIA, including model-fragment assembly. The tactic data structures,
  ownership rules, and current migration boundary are described in
  [](tactics.md).
- `theory/` owns equality reasoning, congruence closure, and arithmetic
  tableau solving.

The build enforces the same direction with static-library targets:
`primo_util`; `primo_terms` over utility code; `primo_sat`; `primo_theory` over
terms, SAT interfaces, and utilities; `primo_smtlib`; and `primo_smt` over the
preceding solver layers. `primo_core` is a compatibility interface target for
whole-solver executables and broad tests. New narrow tests should link the
lowest target they exercise. `primo_smtlib` contains only the owned handwritten
lexer/parser/printer; ANTLR and its generated sources are removed. CaDiCaL
configuration is confined to `primo_sat`, and GMP to `primo_util`.

## Boundaries

- SMT-LIB syntax objects are consumed by the pimpl-isolated `smt::Frontend`
  session and converted to `core::TermId` values before reaching SAT or theory
  code. Its iterative `TermBuilder` is private to `frontend.cpp`.
- SAT clauses contain `sat::Lit`, not terms.
- The Boolean clausifier keeps the mapping from Boolean theory atoms to SAT
  literals and memoizes theory-atom polarity collection per hash-consed term and
  polarity.
- Theory solvers reason over `core::TermId` values and never own SMT-LIB syntax.
  The production QF_UFLIA boundary is the solve-scoped
  `theory::TheoryComponent` protocol; `TheorySolver` remains a private adapter
  helper and the older experimental QF_UFLRA projection interface. Components
  receive typed literals and an SMT-owned interface arrangement but no SAT
  backend or access to another component's private state. Descriptors declare
  ownership, convexity, stable infiniteness and capabilities; the plan validates
  them before solving. See [](n-way-combination-migration.md).
- All core terms are created through `core::TermManager`.
- Cancellation is carried as a read-only callback in `CheckSatOptions` and
  `TacticContext`. Each solver invocation owns an RAII-scoped SAT terminator;
  neither the active backend nor live statistics are stored in process-global
  state. The CLI's process-global signal state is limited to a
  `sig_atomic_t` request flag.
- A tactic goal owns the model-reconstruction records created while rewriting
  it. Solver leaves receive the complete goal rather than separate assertion
  and reconstruction side channels, and `CheckSatOutcome` is derived from the
  returned goal after dispatch.
- Formula analysis/metrics, frontend translation, Boolean clausification,
  preprocessing strategy leaves, Boolean solving, common solver statistics,
  solver-family dispatch, result adaptation, runtime termination, and
  unsupported-input exceptions have independent SMT modules. The complete
  single-goal QF_UF, QF_LRA, QF_LIA, QF_UFLRA, and QF_UFLIA implementations
  and their shared theory/model support live in `solver_engines.cpp`; the small
  formula-valued adapters live in `solver_leaves.cpp`. Public
  `check_sat.cpp` is a small boundary wrapper; `session_portfolio.cpp` owns the
  reparsing QF_UF portfolio and its sequential executor policy; and
  `solver_session.cpp` owns parsing/frontend lifetime, preprocessing-strategy
  execution, final solver-strategy compilation, timing, and outcome conversion.

## Command-line enum options

`cli::EnumName` tables in `src/cli/option_names.hpp` map command-line names to
typed solver enums. A value may have aliases; its first table entry is the
canonical spelling. `CheckSatOptions` owns the defaults. The CLI's
`add_enum_option` helper derives the displayed default from the initialized
field and uses the same table for allowed choices and parsing. Solver and
theory code do not depend on CLI spellings.

The callback borrows the option field and table, which must outlive the
`CLI::App`; tables must remain unchanged while the app uses them. Current
call sites use static tables and option storage declared before the app.
The callback owns its diagnostic option name and checks parse failure before
assigning the field, independently of the CLI validator.

The table tests check unique names, aliases, round trips and coverage of
declared enum values in the range 0–63. Detection relies on GCC/Clang function
signature spelling and has a compiler-format self-check. This is a bounded
test, not exhaustive reflection: an unnamed negative value or a sparse jump
such as `NewMode = 100` is not detected. The upper-bound guard catches dense
growth into the boundary only. Review the probe range when changing enum
values, and keep the name table synchronized with the declaration.

## Term Representation

The primary term types are:

- `core::TermId` in `src/core/ids.hpp`: a `std::uint32_t` index into a `TermManager`.
- `core::TermKind` and `core::Term` in `src/core/term.hpp`.
- `core::TermManager` in `src/core/term_manager.hpp` and `src/core/term_manager.cpp`.

`Term` is a compact node:

```text
Term:
  id:        TermId
  kind:      BoolValue | Constant | Application | Equality | Not | And | Or | Implies
             | Rational | Plus | Times | Le | Lt | Ite | IntDiv | IntMod | Abs
             | BoundVariable | Forall | Exists | Pattern
  sort:      SortId
  symbol:    SymbolId for Constant and Application; the de Bruijn index for
             BoundVariable; the number of bound variables for Forall/Exists
  children:  vector<TermId>
  bool_value: meaningful only for BoolValue
  rational_value: literal value, scale coefficient, or normalized integer divisor
```

Terms are not individually heap-allocated. `TermManager` owns all term nodes in a `std::vector<Term> terms_`. The public API returns `TermId`, not pointers or owning handles. A term id is the vector index assigned at insertion time.

### Ownership and Lifetime

`TermManager` owns every `Term` it creates. Other components may store `TermId` values, but they do not own term memory. A `const Term&` returned by `TermManager::get` is a borrowed reference into `terms_`.

Important lifetime rule: `TermId` values are stable for the lifetime of the `TermManager`; `const Term&` references are not guaranteed to remain stable across later term insertions because `terms_` is a vector and may reallocate. Code that calls a `mk_*` method while holding a `const Term&` must reacquire the reference afterwards.

Before any direct or indirect term-creating call, copy the child IDs and scalar
payloads needed by the operation. This also applies within a single expression:
do not mix reads through a borrowed term with allocating calls in sibling
function arguments, whose evaluation order is not guaranteed to be left-to-right.
Construct intermediate terms from the copied values, then reacquire any needed
references by ID. `const` does not prevent mutation through the manager, and
spare vector capacity or hash-cons reuse must not be assumed for correctness.
The code-review skill checks borrow/mutation/use paths; the testing skill calls
for allocation-boundary regressions under ASan. These are review and testing
requirements, not an API-level enforcement mechanism or a claim that all
existing call sites have been audited.

`TermManager` stores a reference to a `SymbolTable`:

```text
const SymbolTable& symbols_;
```

The symbol table must outlive the term manager. In the current SMT front end this is satisfied by member order in `smt::Frontend`: `symbols_` is declared before `terms_`.

### Hash-Consing

`TermManager` hash-conses all terms through:

```text
std::unordered_map<Key, TermId, KeyHash> term_by_key_
```

The hash-consing key contains:

- `TermKind kind`
- `SortId sort`
- `SymbolId symbol`
- `std::vector<TermId> children`
- `bool bool_value`
- `util::Rational rational_value`

`TermManager::intern` first looks up this key. If an identical key already exists, it returns the existing `TermId`; otherwise it appends a new `Term` to `terms_` and records the key-to-id mapping.

The equality notion here is syntactic identity after local simplification. If two constructed terms receive the same `TermId`, they are structurally identical under the current constructors. This is separate from EUF congruence equality, which is computed later by the equality theory.

### Quantifiers and bound variables

Quantified terms use de Bruijn indices; the design and its alternatives are in
[](quantifiers.md).

- **`BoundVariable`.** A leaf whose `symbol` field holds the index and whose
  `sort` is the variable's sort. Index 0 is the innermost enclosing bound
  variable. `Rational` has no integer accessor, which is why the index is not
  kept in `rational_value`.
- **`Forall`/`Exists`.** Bool nodes whose `symbol` is the arity `n`. Their
  children are `n` declaration leaves `BoundVariable(n-1-i, sort_i)` in SMT-LIB
  order, then the body, then `Pattern` nodes. The declarations keep an unused
  variable's sort in the hash key. Alpha-equivalent formulas are therefore the
  same term.
- **`Pattern`.** A Bool node holding the terms of one `:pattern`. `mk_pattern`
  rejects terms that contain quantifiers.

`TermManager` caches two binder facts per node, computed once at intern time
from the children:

- `loose_index`: one past the largest index not bound inside the term, so 0
  exactly for closed terms;
- `contains_quantifier`.

The storage is created by the first binder node. A quantifier-free session
therefore allocates nothing, and every earlier term reads as closed.
`quantifier_count()` counts interned `Forall`/`Exists` nodes, like
`extended_term_count()`.

`core/binders.hpp` is the only code that computes indices:

- `shift` moves an open term under more binders;
- `instantiate` opens a quantifier with replacement terms, shifting each
  replacement as it passes inner binders and lowering indices that point past
  the opened binder;
- `check_bound_variables` is a debug checker for scoping and sorts.

All three are iterative, memoized per `(term, cutoff)`, and skip any subterm
whose loose index shows it is unaffected. `tests/unit/binder_tests.cpp` covers
constructor validation, cache values, shift laws, the design note's worked
examples, and a differential test against a named-variable oracle with capture
avoidance. It also covers 20,000 nested binders.

Every exhaustive `TermKind` switch handles the new kinds:

- the clausifier treats `Forall`/`Exists` as opaque theory atoms;
- the evaluator, EUF flattening, mixed purification and preprocessing either
  reject them or pass them through as opaque leaves.

Quantified input does not reach those passes yet: frontend parsing and the
routing gate are later milestones in [](quantifiers.md).

### Sorts and Symbols

Sorts and symbols are owned by `core::SymbolTable`:

- `SortId`, `SymbolId`, and `TermId` are all `std::uint32_t` aliases from `src/core/ids.hpp`.
- `SymbolTable` stores sorts in `std::vector<Sort> sorts_`.
- `SymbolTable` stores symbols in `std::vector<Symbol> symbols_`.
- names are mapped with `std::unordered_map<std::string, SortId>` and `std::unordered_map<std::string, SymbolId>`.

SMT-LIB names arrive as canonical symbol text, not quoted spellings; the empty
name is supported. `Symbol::internal` is explicit auxiliary ownership, set only
by `declare_internal_constant`, not inferred from `$primo_` prefixes. The flag is
copied into immutable model signatures. User declarations that resemble internal
names still require interpretations and must be printed. See [](lexical-conformance.md).

`SymbolTable` creates the built-in Bool, Real and Int sorts in its constructor,
with IDs 0, 1 and 2 respectively. Uninterpreted sorts receive further IDs.

Function and predicate declarations are represented as `Symbol` records:

```text
Symbol:
  id
  name
  arg_sorts
  result_sort
  kind: Constant | Function | Predicate
  internal: bool (false for user declarations)
```

`TermManager::mk_constant` accepts only constant symbols. `TermManager::mk_app` accepts function and predicate symbols and checks arity and argument sorts against the symbol declaration.

### Term Constructors and Simplification

All constructors validate sorts before interning. Invalid input throws `std::invalid_argument`; an unknown term id throws `std::out_of_range`.

`smt::Frontend::process_command` is the user-input type-checking boundary. It
converts constructor `std::invalid_argument` failures caused while translating
SMT-LIB commands into the typed `UnsupportedInput` result used by the public
session. This conversion is deliberately local to frontend translation:
preprocessing and solver exceptions are not reclassified as unsupported input.

Implemented constructors:

- `mk_bool(value)`: creates or reuses the Boolean constant.
- `mk_constant(symbol)`: creates or reuses a constant of its declared sort,
  including Bool, Int, Real and uninterpreted sorts.
- `mk_app(symbol, args)`: creates or reuses a function or predicate application.
- `mk_eq(lhs, rhs)`: requires matching sorts and returns `true` immediately when `lhs == rhs`.
- `mk_not(child)`: requires Bool, folds Boolean constants, and removes double negation.
- `mk_and(children)`: requires Bool children, preserves nested `And` children, drops `true`, removes duplicate direct children, returns `false` if any child is `false`, and returns the single child for singleton conjunctions.
- `mk_or(children)`: requires Bool children, preserves nested `Or` children, drops `false`, removes duplicate direct children, returns `true` if any child is `true`, and returns the single child for singleton disjunctions.
- `mk_implies(lhs, rhs)`: requires Bool operands and folds trivial cases such as `p => p`, `false => p`, `true => p`, `p => true`, and `p => false`.
- `mk_rational(value, sort)`, `mk_plus`, `mk_times`, `mk_le`, and `mk_lt`:
  build exact linear arithmetic. `Times` is one child with a rational scalar,
  not general multiplication; Int literals and coefficients must be integral.
- `mk_ite(condition, then_term, else_term)`: requires a Bool condition and
  matching branch sorts. It remains a term until mandatory extended lowering.
- `mk_int_div`, `mk_int_mod`, and `mk_abs`: preserve typed Int operators for
  lowering. Division/modulo normalize a nonzero literal divisor; their unary
  core nodes store the positive divisor in `rational_value`.

The constructors do not currently canonicalize commutative operands. For example, `(= a b)` and `(= b a)` are different syntactic terms unless another layer rewrites them before calling `mk_eq`.

### Interaction with SMT-LIB `let`

Current explanation: <a href="../../implementation/frontend.html#let-bindings">Frontend — let bindings</a>.

SMT-LIB `let` does not create a term node. `smt::Frontend` keeps a stack of local maps from names to bindings. A binding is either a built `TermId`, stamped with the quantifier depth it was built at, or a variable bound by an enclosing `forall`/`exists`, stored as its binder level. Binding right-hand sides are built in the outer scope, then the body is built in an extended scope. A name resolves in one of two ways:

- a value is shifted under the quantifiers entered since it was built, which is the identity outside quantifiers;
- a bound variable becomes its de Bruijn index (see [](quantifiers.md)).

Because bound names resolve to existing `TermId` values, `let` expansion does not duplicate subtrees beyond the normal constructor calls and hash-consing behavior.

SMT-LIB `define-fun` supports nonrecursive macros over the admitted term
language, not only UF terms. `smt::Frontend::Definition` has two representations. A nullary
definition is compiled at its declaration into one hash-consed `TermId`; a
parameterized definition retains its `SExpr` body and is expanded at each use by
binding formal parameters to already-built argument terms. Both paths check the
declared result sort. Parameterized expansion also checks argument sorts and
rejects recursive use instead of treating it as an uninterpreted function.

Compiling a nullary definition at its declaration gives it definition-site
name resolution and prevents a surrounding parameterized macro or `let` from
capturing names in its body. The frontend does not retain or deep-copy the
nullary `SExpr`. A reference is a direct lookup of `compiled_nullary`, so a DAG
of nullary definitions costs time linear in the sum of declaration-body syntax
plus its references, rather than walking the general definition-instantiation
path on the first demand for every node.

`core::TermManager` preserves that sharing for Boolean connectives. `mk_and`
and `mk_or` simplify constants and duplicate direct operands but do not flatten
a same-kind child. Nested associative nodes are valid core terms. Optional
bounded flattening is an SMT preprocessing transformation over reachable
assertions, controlled by `--boolean-flatten-threshold`; it is not part of term
construction. This distinction prevents a chain of eagerly compiled nullary
definitions from retaining every flattened conjunction prefix. The data
structures, complexity bound, and 154 GB regression are documented in
[](boolean-dag-flattening.md).

The compiled representation also owns `compiled_side_assertions`. These are the
Boolean equivalences for compound `Bool` UF arguments. Non-Boolean `ite` bodies
used to contribute guarded equalities here too; since `ite` became a term kind
they compile to a term instead and contribute none.
Every use replays that package into the current assertion build. A per-build set
deduplicates transitive packages, and a session set emits each resulting
`TermId` assertion only once into the permanent assertion list. An unused
definition may allocate terms and auxiliaries, but its side assertions are not
asserted until the definition is referenced.

This is an original frontend compilation optimization, not an implementation
of a cited macro-expansion algorithm. Eager construction changes `TermId`
allocation order and can therefore change downstream SAT search. The intended
invariants are semantic: one stable term per nullary definition, lexical name
resolution, result-sort preservation, and replay of every auxiliary constraint.
Unit tests cover long transitive chains, parameter shadowing, compound Boolean
UF arguments, and non-Boolean `ite` bodies, the last of which now compile to a
term that the preprocessing leaf lowers once.

Eager compilation can expose a definition chain as a very deep, shared core
term DAG even though every SMT-LIB declaration body is shallow. Core `TermId`
values are stable, but the process call stack is not an acceptable storage
mechanism for that depth. Downstream whole-formula walks therefore keep their
own explicit work vectors and, where the operation is a DAG computation,
pass-local memo tables keyed by `TermId`. This applies to preprocessing
occurrence summaries and rewrites, formula-feature dispatch, mixed
purification, theory-atom polarity collection, Boolean-argument bridge
materialization, top-level conjunction assertion, and Tseitin encoding.
Rewriters copy a node's child IDs before
calling a `TermManager` constructor because construction may grow term storage
and invalidate a borrowed `Term` reference. The clausifier allocates a parent
SAT variable before scheduling its children and emits the parent's defining
clauses after those children have literals, preserving the existing ownership
boundary: the SMT layer owns the worklist and term-to-literal map, while the SAT
backend receives only variables and clauses.

This stack-safety work is implementation hardening, not a new preprocessing or
clausification algorithm. The expected auxiliary space is linear in reachable
DAG nodes for memoized walks and linear in current DAG depth for a plain
post-order worklist. The regression test compiles a 20,000-definition Boolean
chain, places a non-unit equality at its base, includes both a mixed arithmetic
purification site and the deep Boolean term as a UF argument, and runs through
the complete solver under ASAN.

Parameterized macro applications are not reparsed from text. The parser has
already stored each body as an SMT-LIB syntax tree, and each application walks
that stored body under fresh parameter bindings to build `TermId` values. A
future optimization could compile such bodies once into a frontend template and
instantiate the template by explicit `TermId` substitution, instead of walking
the SMT-LIB body through the general expression builder for every application.

SMT-LIB `Bool` is a first-class sort, so QF_UF inputs may pass Boolean formulas
as arguments to uninterpreted functions. The frontend normalizes compound
Boolean arguments by introducing a fresh Boolean constant and a propositional
equivalence side assertion before constructing the application term. This keeps
the EUF flattening layer first-order while preserving the Boolean meaning of the
argument through SAT clauses.

### SAT and Theory Views of Terms

The SAT layer does not inspect `Term` objects directly. `smt::BooleanClausifier`
maps Boolean structure to SAT clauses. Top-level asserted conjunctions are
asserted child-by-child instead of through an auxiliary Tseitin literal.
Top-level asserted disjunctions are asserted as one clause. `--top-level-or-tseitin`
instead applies the same cached Tseitin encoding as nested disjunctions and
asserts the auxiliary literal by a unit clause; this adds binary support clauses
that can expose branch literals to SAT search, but a 334-row QF_UF SLURM
validation rejected it as an unconditional default because it regressed PEQ and
SEQ rows.

The assertion vector is an implicit conjunction, not a semantic boundary. A
root-polarity clausification prototype confirmed that separate assertions, an
asserted conjunction, and root De Morgan equivalents can share one
required-context treatment without distribution. It was reverted because it
removed too little Boolean structure on the target QF_UFLRA cases. The invariant
and rejected measurements are recorded in
[](root-polarity-clausification.md); there is no current solver option.

The opt-in structural auto-preprocessing selector now enforces that invariant
for profile selection without constructing a synthetic conjunction. It expands
only positive top-level `And` nodes in an analysis-only DAG walk and uses the
result with finite-domain diagnostics. The legacy assertion-count selector is
still the default until evaluation is complete; see
[](structural-auto-preprocessing.md). Optional implication normalization is an
SMT-layer rewrite over `TermId` values and remains separate from both selector
and clausifier.

What survived that rejection is a **guarded automatic rule**, on by default and
disabled with `--no-auto-top-level-or-tseitin`. It is much narrower than
"automatic" suggests -- `should_use_auto_top_level_or_tseitin` requires *all* of:

- the problem has finite-domain at-most-one structure (`choices != 0` and a
  non-empty clause set), so it only ever fires on QF_UF finite-domain shapes;
- between 1000 and 2500 top-level OR nodes;
- at most 700 negation nodes;
- `--top-level-or-tseitin` was not forced on.

Outside that band the effective default is one clause per top-level
disjunction. The size window is what keeps the encoding away from the rows it
regressed: small formulas gain nothing from the extra literals, and large ones
paid for them. Equalities and Boolean-returning predicate applications become theory
atoms with associated SAT literals.

The equality theory receives `TermId` values. Congruence closure uses those ids to inspect term structure through `TermManager::get`, merge equality classes, and test disequalities. EUF equality is therefore a theory relation over term ids, not the same thing as hash-consed syntactic identity.

## Current Implementation Status

For supported logics and result limits, see the
<a href="../../capabilities.html">capability summary</a>.

- <a href="../../implementation/frontend.html">Frontend</a>: parses and type-checks SMT-LIB commands and expands nonrecursive definitions.
- <a href="../../implementation/core.html">Core terms</a>: typed, hash-consed terms with local Boolean and arithmetic simplification.
- <a href="../../implementation/sat.html">SAT integration</a>: Boolean clausification and CaDiCaL solving through the IPASIR-UP adapter.
- <a href="../../implementation/dpllt.html">DPLL(T)</a>: shared observed assignments, explained theory propagation, conflicts, and backtracking.
- <a href="../../implementation/euf.html">Equality and uninterpreted functions</a>: incremental congruence closure with explanations.
- <a href="../../implementation/lra.html">Linear real arithmetic</a>: exact Simplex solving and bound propagation.
- <a href="../../implementation/lia.html">Linear integer arithmetic</a>: GCD checks, cuts, and bounded branching; exhausted budgets return `unknown`.
- <a href="../../implementation/combination.html#the-dedicated-real-route">UF with Real arithmetic</a>: a dedicated coordinator exchanges equalities and reconciles candidate models.
- <a href="../../implementation/combination.html#the-general-component-driver">UF with Int arithmetic</a>: the general component driver coordinates theory checks and model choices.
- <a href="../../implementation/quantifiers.html">Quantified UF</a>: enumerative instantiation, restricted SAT completion, and no printed quantified models.
- <a href="../../implementation/preprocessing.html">Preprocessing</a>: simplification, definition elimination, arithmetic normalization, and UF symmetry breaking.
- <a href="../../implementation/models.html">Models</a>: owning typed interpretations with reconstruction of values removed by preprocessing.

### Scoped SMT-LIB command execution

- <a href="../../implementation/sessions.html#default-engine-replay-the-active-journal">Default rebuild mode</a>: replays active commands into fresh solving state for each query.
- <a href="../../implementation/sessions.html#assumptions-engine-retain-meanings-guard-assertions">Opt-in assumptions mode</a>: retains SAT state and prepared theory structure across queries for `QF_UF` and `QF_LRA`.

### Limits and open work

- <a href="../../capabilities.html#limits">Unsupported features</a>: arrays, bit-vectors, nonlinear arithmetic, quantified arithmetic, general mixed Int/Real input, and proof production.
- <a href="../../leads/index.html">Research leads</a>: remaining investigations, separate from implemented features and optional experiments.

The [original detailed inventory](https://github.com/primo-solver/primo/blob/1f9f116c73520d960278139a0e82dd0dbd4c9b29/docs/architecture.md#L426-L858)
retains the dated experimental and benchmark history.

## Paper Traceability

- IPASIR-UP motivates the SAT-backend interface. `src/sat/` owns the
  backend-neutral propagator/terminator contracts and adapts them to CaDiCaL's
  concrete C++ API.
- Congruence-closure papers ground the incremental flat closure, indexed use
  lists and explanations in `theory/equality/`. The default explanation search
  is BFS; optional proof-forest modes are not a claim that every optimized
  mechanism of the cited papers is implemented. See [](euf.md) and
  [](papers/fast-congruence-closure-implementation.md).
- DPLL(T) papers motivate the SAT/theory loop. Primo has IPASIR-UP callback
  propagation for its pure EUF and LRA paths and for the documented mixed
  online subset. Mixed final checks reuse prepared EUF/LRA structure but still
  rebuild round-local state when generated interface equalities change.
  QF_UFLIA now has a collection-based lifecycle/dispatcher and typed model
  exports; neither route implements the full persistent `DPLL(T^M)` system.
- SMT symmetry-breaking papers motivate the default-on QF_UF preprocessing pass.
  Primo implements the conservative constant-permutation subset described in
  `docs/preprocessing.md`; finite-model value precedence remains background
  rather than an implemented pass.

## Optional Tools

Local optional tools live under `contrib/local` and are installed with:

```bash
make contrib-z3
make contrib-cvc5
make contrib-cadical
```

Dolmen is an external validator for SMT-LIB model response files. It is not
linked into Primo; install the pinned host-compatible build with
`make contrib-dolmen` and run `scripts/validate_model.py`. The dependency is optional for ordinary builds,
but required for independent printed-model qualification across supported logics,
including full-corpus SAT replay separate from timing. The validator owns only
generated problem/response artifacts and durable CSV/JSON metadata; it does not
enter solver control flow or share model objects with Primo.

Downloaded archives and build trees under `contrib/downloads`, `contrib/src`, and `contrib/local` are not committed.
