# Architecture

Primo is organized as layered components with explicit data ownership. This page describes the implementation that exists now, not the intended final solver.

## Layers

- `smtlib/` parses SMT-LIB commands into lightweight syntax objects.
- `core/` owns sorts, symbols, and hash-consed terms.
- `sat/` owns propositional variables, literals, clauses, assumptions, the
  backend-neutral IPASIR-UP callback interfaces, and the concrete CaDiCaL
  adapter.
- `smt/` owns conversion of SMT-LIB commands to core terms, preprocessing, Boolean abstraction,
  clausification, tactic composition, QF_UF/QF_LRA solver orchestration, and the
  current separable mixed QF_UFLRA coordinator. The tactic data structures,
  ownership rules, and current migration boundary are described in
  [](tactics.md).
- `theory/` owns equality reasoning, congruence closure, and arithmetic
  tableau solving.

The build enforces the same direction with static-library targets:
`primo_util`; `primo_terms` over utility code; `primo_sat`; `primo_theory` over
terms, SAT interfaces, and utilities; `primo_smtlib`; and `primo_smt` over the
preceding solver layers. `primo_core` is a compatibility interface target for
whole-solver executables and broad tests. New narrow tests should link the
lowest target they exercise. Generated ANTLR code and its warning policy are
confined to `primo_smtlib`, CaDiCaL configuration to `primo_sat`, and GMP to
`primo_util`.

## Boundaries

- SMT-LIB syntax objects are consumed by the pimpl-isolated `smt::Frontend`
  session and converted to `core::TermId` values before reaching SAT or theory
  code. Its iterative `TermBuilder` is private to `frontend.cpp`.
- SAT clauses contain `sat::Lit`, not terms.
- The Boolean clausifier keeps the mapping from Boolean theory atoms to SAT
  literals and memoizes theory-atom polarity collection per hash-consed term and
  polarity.
- Theory solvers reason over `core::TermId` values and never own SMT-LIB syntax.
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
  unsupported-input exceptions have independent SMT modules. Public
  `check_sat.cpp` is a small portfolio coordinator; `solver_session.cpp` owns
  the remaining per-session preprocessing and theory-family orchestration.

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
  sort:      SortId
  symbol:    SymbolId, only meaningful for Constant and Application
  children:  vector<TermId>
  bool_value: meaningful only for BoolValue
```

Terms are not individually heap-allocated. `TermManager` owns all term nodes in a `std::vector<Term> terms_`. The public API returns `TermId`, not pointers or owning handles. A term id is the vector index assigned at insertion time.

### Ownership and Lifetime

`TermManager` owns every `Term` it creates. Other components may store `TermId` values, but they do not own term memory. A `const Term&` returned by `TermManager::get` is a borrowed reference into `terms_`.

Important lifetime rule: `TermId` values are stable for the lifetime of the `TermManager`; `const Term&` references are not guaranteed to remain stable across later term insertions because `terms_` is a vector and may reallocate. Code that calls a `mk_*` method while holding a `const Term&` must reacquire the reference afterwards.

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

`TermManager::intern` first looks up this key. If an identical key already exists, it returns the existing `TermId`; otherwise it appends a new `Term` to `terms_` and records the key-to-id mapping.

The equality notion here is syntactic identity after local simplification. If two constructed terms receive the same `TermId`, they are structurally identical under the current constructors. This is separate from EUF congruence equality, which is computed later by the equality theory.

### Sorts and Symbols

Sorts and symbols are owned by `core::SymbolTable`:

- `SortId`, `SymbolId`, and `TermId` are all `std::uint32_t` aliases from `src/core/ids.hpp`.
- `SymbolTable` stores sorts in `std::vector<Sort> sorts_`.
- `SymbolTable` stores symbols in `std::vector<Symbol> symbols_`.
- names are mapped with `std::unordered_map<std::string, SortId>` and `std::unordered_map<std::string, SymbolId>`.

`SymbolTable` creates the built-in Bool sort in its constructor. `bool_sort()` currently returns `0`.

Function and predicate declarations are represented as `Symbol` records:

```text
Symbol:
  id
  name
  arg_sorts
  result_sort
  kind: Constant | Function | Predicate
```

`TermManager::mk_constant` accepts only constant symbols. `TermManager::mk_app` accepts function and predicate symbols and checks arity and argument sorts against the symbol declaration.

### Term Constructors and Simplification

All constructors validate sorts before interning. Invalid input throws `std::invalid_argument`; an unknown term id throws `std::out_of_range`.

Implemented constructors:

- `mk_bool(value)`: creates or reuses the Boolean constant.
- `mk_constant(symbol)`: creates or reuses an uninterpreted or Boolean constant.
- `mk_app(symbol, args)`: creates or reuses a function or predicate application.
- `mk_eq(lhs, rhs)`: requires matching sorts and returns `true` immediately when `lhs == rhs`.
- `mk_not(child)`: requires Bool, folds Boolean constants, and removes double negation.
- `mk_and(children)`: requires Bool children, preserves nested `And` children, drops `true`, removes duplicate direct children, returns `false` if any child is `false`, and returns the single child for singleton conjunctions.
- `mk_or(children)`: requires Bool children, preserves nested `Or` children, drops `false`, removes duplicate direct children, returns `true` if any child is `true`, and returns the single child for singleton disjunctions.
- `mk_implies(lhs, rhs)`: requires Bool operands and folds trivial cases such as `p => p`, `false => p`, `true => p`, `p => true`, and `p => false`.

The constructors do not currently canonicalize commutative operands. For example, `(= a b)` and `(= b a)` are different syntactic terms unless another layer rewrites them before calling `mk_eq`.

### Interaction with SMT-LIB `let`

SMT-LIB `let` does not create a term node. `smt::Frontend` keeps a stack of local maps from variable names to `TermId`. Binding right-hand sides are built in the outer scope, then the body is built in an extended scope. Because bound names resolve to existing `TermId` values, `let` expansion does not duplicate subtrees beyond the normal constructor calls and hash-consing behavior.

SMT-LIB `define-fun` is supported for the QF_UF macro subset used by benchmark
preludes. `smt::Frontend::Definition` has two representations. A nullary
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

The compiled representation also owns `compiled_side_assertions`. These are
the Boolean equivalences for compound `Bool` UF arguments and guarded
equalities for non-Boolean `ite` auxiliaries created while compiling the body.
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
UF arguments, and non-Boolean `ite` bodies.

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

Implemented now:

- SMT-LIB command parsing through `smtlib::Lexer` and a local S-expression
  parser for the supported command subset. Quoted symbols, string metadata,
  comments, and multiline `|...|` metadata are tokenized without constructing an
  ANTLR parse tree. Command-level validation still rejects unsupported commands
  and malformed supported commands before the frontend builds `core::TermId`
  values.
- QF_UF `define-fun` macro expansion for non-recursive definitions.
- Sort and symbol tables for Bool and uninterpreted sorts.
- Hash-consed term construction with local Boolean simplification.
- CaDiCaL-backed SAT solving through the IPASIR-compatible backend.
- Boolean clausification for assertions.
- Lazy QF_UF checking over equality atoms and Boolean predicate applications.
- Non-incremental congruence closure for the current SAT assignment.
- QF_LRA solving for linear Real arithmetic through a Simplex tableau with
  CaDiCaL/IPASIR-UP integration, same-variable bound propagation, and default
  sparse pivot heuristics.
- Conservative QF_UF SMT-LIB model printing for `check-sat` followed by
  `get-model` when `:produce-models true` is set. `smt::ModelBuilder` rebuilds
  a ground EUF closure over `core::TermId` values, prints definitions for
  user-declared constants, functions, and predicates, consumes preprocessing
  reconstruction assignments for unit facts and substitutions, and omits
  Primo-generated auxiliary symbols from the response.
- Minimal QF_LRA SMT-LIB model printing for user-declared nullary `Real`
  constants, with shared support from the QF_UF model builder for non-Real
  declarations that survive only as preprocessing reconstruction facts. The SMT
  layer records declared Real constants, the LRA path reads their values from
  the final `LraTableau` assignment, and `smt::ModelConversionChain` replays
  typed preprocessing conversions in reverse tactic order through the
  appropriate Real-value or UF-closure fragment so solved-away constants are
  printed for the original problem. The chain is goal-owned and immutable after
  installation; solver builders borrow it only for the duration of model
  construction.
- Minimal QF_UFLRA model printing. For separable mixed formulas,
  `solve_qf_uf_lra` composes the QF_UF model body with the nullary-Real QF_LRA
  definitions. UF symbols may take Real arguments or return `Real` values:
  model reconstruction augments the LRA side with EUF-implied Real-constant
  equalities and strict separations for EUF-distinct Real constants before
  printing concrete Real argument values and Real-valued UF application results
  in the UF function tables. `complete_euf_real_model_values` assigns the same
  concrete rational to every Real constant and Real-valued application in one
  purified EUF class. `smt::ModelBuilder` then prefers those exact application
  assignments when a stale pre-rewrite term and an active purified term have
  the same printed arguments; this prevents the stale default row from
  shadowing the constrained function row. If QF_UFLRA preprocessing removes all remaining
  EUF atoms and dispatches the simplified problem to the QF_LRA solver, the same
  composed model builder is used so original UF constants and solved UF
  equalities remain defined in `get-model` output.
- Read-only Nelson-Oppen interface diagnostics in
  `smt::analyze_nelson_oppen_interface`: the SMT layer counts user-visible,
  non-Boolean constants by sort and already-visible equality atoms over those
  constants after preprocessing when `--nelson-oppen-diagnostics` is enabled.
  It also records visible same-sort interface pairs in a sparse registry and
  reports the full theoretical pair count arithmetically. Missing entries are
  created only by an explicit `ensure_interface_pair` request. It reports purification sites for Real terms in
  EUF atoms and uninterpreted applications in arithmetic atoms. This is
  bookkeeping only; it does not create proxy terms or equality atoms, propagate
  interface equalities, combine theories, or inspect theory-local models.
- Partial Nelson-Oppen-style mixed QF_UFLRA final checks in
  `smt::solve_qf_uf_lra`. The SMT layer accepts one SAT assignment, partitions
  visible theory atoms into EUF and LRA atoms, checks both theories, and learns
  the first ordinary per-theory conflict clause it finds. Arithmetic atoms may
  contain Real-valued UF applications, including applications whose `Real`
  arguments are arithmetic terms or nested Real-valued UF applications: a
  post-lowering SMT preprocessing slice replaces each such application with an
  internal Real proxy and adds an EUF definition equality over an already-flat
  UF application. Any non-constant Real argument needed for that EUF-side
  application receives its own Real proxy and two LRA bounds defining the proxy
  as equal to the arithmetic term. EUF atoms may also contain non-constant Real
  arguments, handled by the same proxy mechanism. The coordinator creates
  positive SAT-visible equality
  atoms lazily for shared Real-constant/proxy pairs when one theory first
  implies that equality. For the current SAT assignment, generated equality
  atoms that are true are asserted to both EUF and LRA; equality atoms that are
  false are asserted to EUF as disequalities, while LRA disequality is still
  handled indirectly by rejecting false atoms when LRA later entails equality
  and by model-reconstruction separation. EUF- or LRA-implied shared
  equalities are learned as guarded clauses over interface equality literals;
  the guards are the blocking literals for the current theory assignment, so an
  equality learned in one Boolean branch does not leak into another. The
  default is `--nelson-oppen-prop=final`: equality propagation is performed
  after a complete SAT assignment and before the model is accepted.
  The coordinator repeats this process to a convex equality fixpoint. EUF emits
  a deterministic spanning set for each equality class of shared Real terms.
  LRA first asks the feasible tableau for explanation-carrying `FixedEq` and
  `EqRow` entailments. Mixed QF_UFLRA enables fixed-variable elimination by
  default so fixed basic rows are pivoted into the shape consumed by `EqRow`;
  `--no-qf-uflra-fixed-elimination` is the ablation switch. The global pure-LRA
  `--lra-fixed-elimination` option remains off by default. If either direction learns a guarded clause, SAT is
  re-entered so both theories see the equality on the next round. Once these
  cheap producers are quiescent, the default shared-model path validates a
  candidate component-model agreement. When reconciliation is disabled or a
  candidate invariant fails, the exact path tests both strict orders only for
  still-unresolved shared Real pairs. This exact fallback is the
  completeness mechanism for the supported convex EUF+LRA fragment; it does
  not preallocate the pair cross product as SAT variables. For each pair, a
  check-local context normally copies the already feasible LRA tableau,
  expresses an unbounded difference row in its current nonbasic basis, and tests
  the two strict orientations with tableau checkpoints. The context is discarded
  after the pair. Dense difference rows and bases with high prior pivot-term cost
  conservatively copy the prepared sparse base and replay the current assignment
  once instead. Missing endpoint mappings use the older augmented-theory
  reconstruction oracle. This avoids repeated atom normalization and usually
  avoids assignment replay, but still performs logical quadratic pair space, a
  tableau copy per pair actually consumed by the complete fallback, and any
  required simplex work.
  Equal-valued model classes are stored once with a term-to-class index. Sparse
  care/candidate inputs are filtered by class membership, while complete scans
  use a deterministic cursor and do not retain the quadratic pair expansion.
  The default QF_UFLRA model-reconciliation path gives
  `TheoryCombinationManager` ownership of a round-scoped projection of the
  component models onto the shared Real interface. `SharedModelState` records
  a generation, LRA model-value classes and membership, deterministic spanning
  pairs, a canonical explained-equality worklist, internally absorbed
  equalities, and selected strict LRA separations. It does not own the EUF
  closure or LRA tableau. `SharedEqualityDecisionState` separately owns lazy
  equality decisions that survive SAT re-entry: tried variables, one pending
  batch, active assumptions, and rejection metadata. Rebuilding the manager
  clears this solve-scoped state. A new SAT assignment invalidates only the
  round projection; the theory-local objects remain owned by their solvers.

  In this profile, explained LRA equalities are batched into the live EUF
  closure with their blocking literals. An immediate EUF contradiction returns
  the composed combined-theory conflict directly, without creating a shared
  equality atom. For a consistent batch,
  `FlatCongruenceClosure::equivalence_classes()` projects the shared interface
  from its eager representatives. SAT-false shared equalities are mandatory
  LRA candidate disequalities. Freedom-interval mutation tries to satisfy them;
  remaining violations receive a strict-orientation search over only those
  pairs and a rebuilt scratch candidate.

  After explained propagation is quiescent, the coordinator checkpoints EUF
  and merges each deterministic spanning edge of the LRA value classes. It
  projects EUF classes before and after optional merges. A direct conflict or a
  congruence consequence joining two different LRA classes rejects the causal
  edge. The scan continues after a rejection, rolls back its temporary model
  merges, lazily materializes the canonical rejected-edge frontier, and tries
  the whole frontier positively in one IPASIR-UP assumption batch. Failed
  assumptions produce blocking clauses. Successful merges
  are model choices, not entailed theory lemmas. If all edges are absorbed, the
  no-model path returns SAT without the broad exact scan, and model production
  concretizes the same scratch LRA assignment and supplies the internal
  spanning equalities to EUF. Concretization preserves the ordering of the
  distinct shared `DeltaRational` values: the coordinator sorts them, derives
  the Dutertre-de Moura Section 3.3 crossing bound for each adjacent pair, and
  chooses a smaller positive delta. This costs `O(S log S)` for `S` shared
  variables and prevents model printing from collapsing EUF-distinct Real
  arguments after reconciliation. It is mixed-coordinator postprocessing; the
  LRA tableau and pure QF_LRA path are unchanged. Shared-interface discovery uses the variables of
  each normalized LRA polynomial, not every syntactic Real constant in an
  arithmetic atom. A constant cancelled by normalization is therefore EUF-only
  and uses model completion rather than becoming an unmapped interface term.
  Unexpected mapping failures or failed candidate invariants use the old
  explained exact scan. This is a convex QF_UFLRA reconciliation
  subset, not generic `DPLL(T^M)`; the Section 5.2 mutation remains
  scratch-only and pure QF_UF/QF_LRA do not enter this path.

  `MixedLraModelEpoch` retains one augmented LRA component preparation and its
  checked tableau inside `solve_qf_uf_lra`. The exact ordered set of true
  interface equality atoms is the structural epoch key. An unchanged key reuses
  the preparation and tableau; a changed key rebuilds both. Before checking a
  different LRA assignment in the same epoch, `LraTableau::pop(0)` removes all
  prior bounds and reasons, then the coordinator asserts the current bounds on
  the retained feasible basis. An exact assignment match may reuse the complete
  check. This is mixed-only component state: `TheoryCombinationManager` still
  owns shared equality identity and decisions, while LRA owns tableau semantics.
  Pure QF_LRA and the base mixed online tableau are unchanged.
  `--no-qf-uflra-model-reconciliation` restores the explained exact
  equality-sharing fallback for ablation and diagnosis.

  The earlier class/worklist prototype was measured before this complete
  reconciliation step. Its fresh
  `4a93256` SLURM gate found no wrong answer in 4,000 generated cases or the
  tracked-seed mutation campaign, and the full 1,284-file run preserved all
  1,156 baseline solves while reducing PAR2 from 1412.285 to 1407.514 seconds.
  A later exact 30-second profile of the full 1,284-file corpus retained this
  mode and removed two implementation costs outside the arithmetic solver:
  checked-tableau reuse for exact interface probes and DAG-memoized
  preprocessing equality-query registration. At revision `9b9a035`, Primo
  solves 1,234 files with 50 timeouts and no wrong answers, versus 1,174 solves
  and 110 timeouts at `541af76`; PAR2 falls from 7,055.421 to 3,588.436. The
  solved count matches the reused Z3 run on this corpus, but not instance for
  instance, and Z3 remains faster on common solves. These are implementation
  efficiency results, not additional model-guided transitions. A subsequent
  reference run added Yices 2.7.0: at five seconds it solves 1,275 files with
  nine timeouts, and at 30 seconds it solves 1,276 with eight timeouts. Yices
  has no answer disagreement with either Primo or Z3 on jointly solved rows;
  it solves every row decided by their stored evaluations and exposes 42 more
  30-second Primo gaps. Matching Z3's solved count therefore did not close the
  performance gap to the strongest measured reference.
  The completed convex reconciliation profile at `bbd43b9` subsequently solves
  1,257 files at 30 seconds, with 27 timeouts and PAR2 2,092.497309, versus
  1,238, 46, and 3,306.995737 at `1f3d81c`. It has no losses or decisive-answer
  disagreements and skips the broad exact scan on 739 rows, but increases SAT
  decisions and solve calls. Split-frontier batching and the retained LRA epoch
  at `ccdd71a` later raise this to 1,264 solves, 20 timeouts, and PAR2
  1,692.677561, with no loss against either earlier profile. This later result
  promotes reconciliation to default; Yices remains ahead at 1,276 solves.
  When reconciliation is disabled or falls back, the exact final path classifies
  LRA-to-EUF equalities through the current
  EUF closure before SAT-visible equality atoms are created. Equalities already
  true in EUF can be ignored. Equalities that EUF can merge, or that EUF
  forbids, use the guarded clause path; the attempted mergeable-skip
  optimization was rejected after the 2026-08-05 full QF_UFLRA run found wrong
  answers.
  `--nelson-oppen-prop=exact` retains this mandatory bidirectional closure and
  exact fallback, but disables the optional LRA `FixedEq`, `EqRow`,
  `EqOffsetRows`, and `EqRows` producer.
  The older spelling `--nelson-oppen-prop=off` is retained as an alias for
  `exact`; it does not disable combination because doing so is unsound for
  mixed QF_UFLRA. `--nelson-oppen-prop=online` is an opt-in
  mixed-theory callback path: the SMT layer connects a single
  `MixedTheoryPropagationDispatcher` through the SAT backend's
  `sat::ExternalPropagator` interface, delegates ordinary arithmetic
  propagation to `OnlineLraPropagator`, and consumes LRA-published interface
  equality candidates. The implemented LRA producer publishes a shared Real
  equality when both non-strict bounds `(<= a b)` and `(<= b a)` over registered
  shared Real constants/proxies are true on the SAT trail. The dispatcher then
  maps that theory-owned candidate to a SAT-visible interface equality literal
  and streams the LRA-provided reason literals back through the SAT backend.
  If the equality literal does not already exist, the dispatcher creates the
  equality term, registers it with `TheoryCombinationManager`, allocates a fresh
  observed SAT variable through `sat::IpasirUpBackend::new_observed_var()` from
  `cb_propagate()`, and records the mapping before returning the literal. This
  is still a candidate-first path, not all-pairs Delayed Theory Combination:
  the full shared Real pair count remains diagnostic, while only published
  paired-bound candidates become observed SAT variables.
  With the additional default-off `--qf-uflra-online-euf` option, the same
  dispatcher also owns the existing `OnlineEufPropagator` as a second child.
  It broadcasts SAT trail callbacks to both children and records which child
  owns each propagation reason or external clause stream. A lazily created
  interface equality is registered in the EUF child's flat assignment state
  before the backend observes its SAT variable, provided both endpoint terms
  already exist in that state. Registration failures are counted and prevent
  the EUF child from observing the atom. This is an experimental IPASIR-UP
  integration path, not a new EUF algorithm and not full model-based theory
  combination. The option no longer requires
  `--nelson-oppen-prop=online`: in final propagation mode the dispatcher keeps
  the LRA paired-bound producer disabled but still maintains the EUF trail and
  exposes it to final reconciliation.
  Online mode retains a complete final strict-bound fallback over candidate
  pairs not already resolved by EUF or lazy entailment. The checked LRA
  tableau's model-value partition first removes pairs with unequal current
  values: an LRA-entailed equality must hold in that feasible model. Retained
  model-equal pairs still require both strict probes. A previous optimization
  skipped the complete fallback after the online
  producer materialized a paired-bound candidate and reran it only when final
  separation failed. Full-corpus evaluation found that criterion unsound:
  separation can succeed while an unscanned LRA-implied equality exposes a
  congruence conflict in EUF. Lazy online atom creation remains enabled, but the
  exact final fallback is required until a complete shared-model validator can
  certify the candidate-only path.
  The experimental `--qf-uflra-candidate-final-scan` mode is the first opt-in
  pruning step for that path: after an LRA check it probes only current EUF care
  pairs, shared Real constants co-occurring in one EUF atom, and pairs whose
  two non-strict LRA bounds are both true. If final arrangement validation or
  model reconstruction later shows that the candidate set missed an
  LRA-implied equality, Primo runs the broad scan once and forces any equality
  found by that fallback through the existing interface-equality SAT clause
  machinery before accepting the final arrangement. Final-check LRA sharing
  additionally consumes `FixedEq`, `EqRow`, `EqOffsetRows`, and `EqRows`
  entailments from the checked tableau. These rules and the model-value filter
  are deliberately polled at
  quiescence rather than on
  every bound change, keeping their map/vector work off the SAT callback hot
  path. `EqOffsetRows`, `EqRows`, and Section 5.2 freedom-interval mutation are
  implemented for the default reconciliation path. The paired-bound rule is therefore still the
  stable online LRA-to-EUF propagation subset; this is not the full model-guided
  transition system.
  The optional online EUF child exposes a move-only, assignment-local
  `IncrementalFlatAssignmentState::ModelScope` after SAT. The scope protects a
  closure checkpoint and returns a `FlatEufModelView` containing only the flat
  term index and mutable closure needed by care-pair classification,
  reconciliation, explanation, and model completion. Temporary candidate-model
  merges are rolled back when the scope is destroyed. CaDiCaL can issue a
  synchronous backtrack while the coordinator adds a learned clause; callbacks
  received during a scope are queued by `OnlineEufPropagator` and replayed in
  order before the next solve. `cb_check_found_model` synchronizes observed EUF
  values omitted from ordinary trail notifications using IPASIR-UP's complete
  reconstructed model vector before the view is acquired. Incomplete
  registration or unavailable incremental state retain the old
  `PreparedFlatTheory` closure rebuild as a measured fallback. The option
  remains default-off. At commit `540b847`, fresh generated and mutation fuzzing
  found no disagreements, while the five-second 1,284-file QF_UFLRA A/B gained
  one net solve and removed every measured final closure rebuild. The same run
  increased decisions and common-solved time slightly, so the evidence supports
  the ownership/lifetime mechanism but not default promotion.
- **Single-theory invariant:** the combination dispatcher is used only by the
  mixed QF_UFLRA solver when online Nelson-Oppen propagation or live mixed EUF
  state is explicitly selected. Pure
  QF_UF keeps its direct EUF propagator path, and pure QF_LRA keeps its direct
  `OnlineLraPropagator` path, so single-theory runs do not pay for shared
  equality registration, dispatcher callbacks, or arrangement bookkeeping. The
  propagation path first reuses an existing interface equality literal when one
  is registered and lazily creates a missing literal only for the equality being
  forced; it is not an exhaustive arrangement-enumeration pass. The statistics
  split producing-theory candidates, existing or missing atoms, created atoms,
  learned clauses, reason literals, and solver-loop rechecks by direction. After
  the receiving theory sees a forced equality, it reports any ordinary conflict
  using that literal as part of its reason. The optional
  `--model-equality-branching` mode
  adds a default-off model-driven SAT-assumption loop for LRA-model-equal shared
  Real pairs. Before creating a guessed equality atom, the mixed coordinator
  calls `LraTableau::diversify_shared_model()` once for the feasible round and
  removes pairs split by that valid scratch model. The default mutation scope
  visits all shared Real variables. The experimental
  `--model-equality-mutation-scope=care` scope visits only current EUF care-pair
  endpoints, then reconstructs and validates the complete shared partition in
  the coordinator; a result that increases the global candidate-pair count is
  discarded. Guessed equality atoms that survive SAT
  and theory checks remain active as assumptions while the coordinator tries
  additional model-equal pairs; if SAT later proves the active assumption set
  inconsistent, Primo learns a blocking clause over the failed assumptions and
  drops them from the active trail. These guesses are optional branches, not
  permanent unit clauses. A pair that remains coincident after the greedy sweep
  has no entailment status: it is still only an assumption candidate. Explained
  equality clauses remain the responsibility of the independent LRA/EUF
  propagation paths. This is intentionally not full Nelson-Oppen: generic
  `DPLL(T^M)` state, a mutable shared theory-model
  object, native false-`Eq` LRA atoms, complete model-equality propagation for
  arbitrary theories, and unbounded arrangement enumeration remain unsupported.
  Ordinary SMT-LIB Real disequality is handled before LRA theory
  assertion by lowering Real equality to linear inequalities and letting the
  Boolean layer split negated equalities. Mixed model reconstruction uses a
  bounded search over strict shared-Real interface separation orientations only
  for pairs whose equality is forbidden by the current EUF closure. Pairs that
  are in different EUF classes but can still be merged without an EUF conflict
  are not strict-separation candidates, because Nelson-Oppen final arrangement
  only needs to separate equalities rejected by a component theory. A greedy
  fallback remains when the EUF-forbidden candidate set exceeds the current
  backtracking cap. EUF-side Real constants that are not common EUF/LRA
  interface terms are reported by diagnostics but are not arrangement variables.
  The optional
  `--nelson-oppen-arrangement-probe-limit` is a separate soft work budget over
  temporary LRA consistency probes, including the conservative final LRA-to-EUF
  equality scan that feeds arrangement validation; hitting it returns `unknown`
  rather than using an incomplete equality/arrangement check. The same bounded
  separation check is also run before returning a non-model QF_UFLRA `sat`
  result, so the reported assignment has at least a checked shared-interface
  Real arrangement for the documented purification subset.
  When default-off model-equality branching is enabled, the mixed coordinator
  also computes EUF care-pair counters for shared Real arguments under flat EUF
  function prefixes that are equal in the current EUF closure. Default solving
  skips this scan because there is no active consumer for the care-pair ranking.
  Default equality propagation enumerates the shared Real pair set after the
  EUF spanning and four LRA Section 5.1 producers, but strict LRA probes are
  issued only for pairs that remain unresolved. The
  `--qf-uflra-candidate-final-scan` option narrows that exact fallback further.
  Final arrangement validation scans the shared Real interface
  pair set to classify pairs as EUF-mergeable or EUF-forbidden, and only the
  forbidden pairs are passed to the strict LRA separation search. The default
  `first`
  model-equality branch
  policy also uses the full pair order; the opt-in `ranked` and `adaptive`
  policies use care-pair diagnostics only to rank guesses, with full-pair
  fallback. The `care-queued` policy is the first pruning policy:
  `ModelEqualityCandidateQueue`
  is owned by the mixed coordinator, rebuilt from the current EUF care pairs,
  bounded by `--model-equality-branch-budget`, and consumed before creating a
  fresh equality atom. The optional branch state is owned by
  `TheoryCombinationManager::SharedEqualityDecisionState`: a selected
  LRA-model-equal pair is queued, then activated by the coordinator as an
  IPASIR assumption on the next SAT call. SAT-level failed
  assumptions are blocked by a learned clause over their negations and removed
  from the active assumption trail; ordinary EUF/LRA theory conflicts under
  active assumptions are learned by the normal theory-conflict path. Guesses are
  not exposed as entailed theory propagations. Before reconciliation or branch
  selection, the coordinator may call
  `LraTableau::diversify_shared_model()` once on the checked feasible tableau.
  This Section 5.2 scratch sweep returns a valid, more diverse LRA model without
  changing live tableau state. The default sweep receives every shared Real
  variable. The opt-in care scope receives only current EUF care-pair endpoints,
  but the coordinator still derives the candidate classes and model values from
  the complete returned assignment. Its post-mutation classes are used as candidates;
  a pair that remains equal is not thereby entailed. Explained equality
  propagation continues to come only from the cheap `FixedEq`, `EqRow`,
  `EqOffsetRows`, and `EqRows` rules or the exact convex fallback.
  In default mixed QF_UFLRA solving, the original LRA atoms are observed by the
  same trail-synced online LRA propagator used by pure QF_LRA, so arithmetic
  conflicts and bound propagations can be delivered inside the SAT search. Final
  checks reuse that online state while no generated interface equality is true,
  and rebuild an augmented prepared LRA theory only when true generated
  interface equalities must be included. The experimental
  `--model-equality-branching` path keeps the older final-check-only LRA path so
  its branch-ranking diagnostics remain comparable.
  Mixed EUF preparation is also append-only. `PreparedFlatTheory` owns a stable
  map from `(signed SAT literal, TermId)` to each cached flat atom and uses it to
  realign the current round's assignments before checking a copied closure.
  This avoids coupling persistent preparation order to the round-dependent
  true-then-false ordering of generated interface equalities.
- Default-on finite-domain UF symmetry breaking, with
  `--no-uf-symmetry-breaking` as an explicit disable switch.

Not implemented yet:

- push/pop assertion stacks.
- proof production.
- full model construction for mixed-theory fragments beyond the documented
  QF_UFLRA subset, arithmetic functions beyond nullary `Real` constants and
  Real-valued UF tables, and reconstruction through future
  satisfiability-preserving rewrites outside the current preprocessing trail.
- Generic model-based theory combination with a mutable shared model,
  non-convex arrangement enumeration, generic model-equality propagation,
  care-set-driven default search, live-model mutation, theory-independent model
  transitions, and minimal cross-theory explanations. The supported
  EUF+LRA subset now has a complete convex shared-Real equality fixpoint, while
  the default-off model-equality branch remains an experiment rather than part
  of that completeness argument.
- explanation-producing congruence closure with proof forests.

## Paper Traceability

- IPASIR-UP motivates the SAT-backend interface. `src/sat/` owns the
  backend-neutral propagator/terminator contracts and adapts them to CaDiCaL's
  concrete C++ API.
- Congruence-closure papers motivate the EUF direction. The current implementation has congruence closure and disequality checking, but it does not yet implement explanation-producing union-find or the full fast algorithms described in the literature notes.
- DPLL(T) papers motivate the SAT/theory loop. Primo has IPASIR-UP callback
  propagation for its pure EUF and LRA paths and for the documented mixed
  online subset. Mixed final checks reuse prepared EUF/LRA structure but still
  rebuild round-local closure state when generated interface equalities change;
  this is not a generic persistent multi-theory transition system.
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
but required by the standard QF_UFLRA merge gate. The validator owns only
generated problem/response artifacts and durable CSV/JSON metadata; it does not
enter solver control flow or share model objects with Primo.

Downloaded archives and build trees under `contrib/downloads`, `contrib/src`, and `contrib/local` are not committed.
