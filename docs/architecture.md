# Architecture

Primo is organized as layered components with explicit data ownership. This page describes the implementation that exists now, not the intended final solver.

## Layers

- `smtlib/` parses SMT-LIB commands into lightweight syntax objects.
- `core/` owns sorts, symbols, and hash-consed terms.
- `sat/` owns propositional variables, literals, clauses, assumptions, the
  backend-neutral IPASIR-UP callback interfaces, and the concrete CaDiCaL
  adapter.
- `smt/` owns SMT-LIB command processing, preprocessing, Boolean abstraction,
  clausification, QF_UF/QF_LRA solver orchestration, and the current separable
  mixed QF_UFLRA coordinator.
- `theory/` owns equality reasoning, congruence closure, and arithmetic
  tableau solving.

## Boundaries

- SMT-LIB syntax objects are consumed by `smt::Frontend` and converted to `core::TermId` values before reaching SAT or theory code.
- SAT clauses contain `sat::Lit`, not terms.
- The Boolean clausifier keeps the mapping from Boolean theory atoms to SAT
  literals and memoizes theory-atom polarity collection per hash-consed term and
  polarity.
- Theory solvers reason over `core::TermId` values and never own SMT-LIB syntax.
- All core terms are created through `core::TermManager`.

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
- `mk_and(children)`: requires Bool children, flattens nested `And`, drops `true`, returns `false` if any child is `false`, and returns the single child for singleton conjunctions.
- `mk_or(children)`: requires Bool children, flattens nested `Or`, drops `false`, returns `true` if any child is `true`, and returns the single child for singleton disjunctions.
- `mk_implies(lhs, rhs)`: requires Bool operands and folds trivial cases such as `p => p`, `false => p`, `true => p`, `p => true`, and `p => false`.

The constructors do not currently canonicalize commutative operands. For example, `(= a b)` and `(= b a)` are different syntactic terms unless another layer rewrites them before calling `mk_eq`.

### Interaction with SMT-LIB `let`

SMT-LIB `let` does not create a term node. `smt::Frontend` keeps a stack of local maps from variable names to `TermId`. Binding right-hand sides are built in the outer scope, then the body is built in an extended scope. Because bound names resolve to existing `TermId` values, `let` expansion does not duplicate subtrees beyond the normal constructor calls and hash-consing behavior.

SMT-LIB `define-fun` is supported for the QF_UF macro subset used by benchmark
preludes. Definitions are stored by the frontend and expanded at each use by
binding formal parameters to already-built argument terms before rebuilding the
body through `TermManager`. The implementation checks argument and result sorts
against the declared sorted variables, and deliberately rejects recursive
definitions instead of treating them as uninterpreted functions.

Zero-argument `define-fun` bodies are memoized after their first successful
expansion when there is no active non-empty `let` or macro-parameter scope. The
cache maps the definition name to the resulting `TermId`, after recursive-use
detection and result-sort checking. This is a bounded cache: it contains at most
one entry per nullary definition in the current SMT-LIB session, and it does not
memoize parameterized macro applications. This keeps ordinary argument-dependent
macro expansion simple while avoiding repeated reconstruction of
CPAchecker-style definition DAGs.

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
  the final `LraTableau` assignment, and model reconstruction replays
  preprocessing equalities through the appropriate Real-value or UF-closure
  fragment so solved-away constants are printed for the original problem.
- Minimal QF_UFLRA model printing. For separable mixed formulas,
  `solve_qf_uf_lra` composes the QF_UF model body with the nullary-Real QF_LRA
  definitions. UF symbols may take Real arguments or return `Real` values:
  model reconstruction augments the LRA side with EUF-implied Real-constant
  equalities and strict separations for EUF-distinct Real constants before
  printing concrete Real argument values and Real-valued UF application results
  in the UF function tables. If QF_UFLRA preprocessing removes all remaining
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
  In this final path, LRA-to-EUF equalities are classified through the current
  EUF closure before SAT-visible equality atoms are created. Equalities already
  true in EUF can be ignored. Equalities that EUF can merge, or that EUF
  forbids, use the guarded clause path; the attempted mergeable-skip
  optimization was rejected after the 2026-08-05 full QF_UFLRA run found wrong
  answers.
  `--nelson-oppen-prop=off` disables these guarded interface-equality
  propagation clauses for ablation. `--nelson-oppen-prop=online` is an opt-in
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
  After online mode has materialized at least one paired-bound equality
  candidate, the coordinator skips the older final all-pairs
  `lra_implied_real_equalities` strict-bound mining pass because profiling
  showed that scan can dominate after online propagation adds arrangement
  traffic. If no online equality candidate is materialized, the broad final scan
  is still available to preserve the current final-check completeness fallback.
  If the skip leaves final arrangement validation or model reconstruction unable
  to separate Real arguments, online mode runs that broad scan once as a
  last-mile validation fallback and records
  `:no.online.final-lra-equality-scan-fallbacks`; the resulting equalities are
  used only for the final validation/model step, not as online propagated theory
  consequences.
  The default `final` mode keeps that broad final scan unchanged. The
  experimental `--qf-uflra-candidate-final-scan` mode is the first opt-in
  pruning step for that path: after an LRA check it probes only current EUF care
  pairs, shared Real constants co-occurring in one EUF atom, and pairs whose
  two non-strict LRA bounds are both true. If final arrangement validation or
  model reconstruction later shows that the candidate set missed an
  LRA-implied equality, Primo runs the broad scan once and forces any equality
  found by that fallback through the existing interface-equality SAT clause
  machinery before accepting the final arrangement. More general LRA-implied
  shared equalities remain a documented opportunity, not the next committed
  online extension: discovering them from arbitrary tableau rows could add a
  wider per-bound-change scan, more explanation bookkeeping, and more
  SAT-visible equality traffic. The paired-bound rule is therefore the stable
  online LRA-to-EUF propagation subset for now; this is not yet the full
  model-guided Nelson-Oppen algorithm.
- **Single-theory invariant:** the combination dispatcher is used only by the
  mixed QF_UFLRA solver when `--nelson-oppen-prop=online` is selected. Pure
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
  Real pairs. Before creating a guessed equality atom, its narrow mutation filter
  asks whether either strict separation of the two terms is still LRA-consistent
  under the current Boolean assignment; if so, the equality is treated as an
  avoidable model artifact and skipped. Guessed equality atoms that survive SAT
  and theory checks remain active as assumptions while the coordinator tries
  additional model-equal pairs; if SAT later proves the active assumption set
  inconsistent, Primo learns a blocking clause over the failed assumptions and
  drops them from the active trail. These guesses are optional branches, not
  permanent unit clauses. The branch selector now routes LRA model-equality
  decisions through a narrow theory-model query result:
  `theory::ModelEqualityEvaluation` is the generic theory-layer status object,
  while the mixed coordinator's `LraTheoryModelView` is the current LRA-only
  adapter that reads tableau model values and classifies equalities as
  mutation-unqueried, mutation-avoidable, or not avoided by the strict
  separation filter. A pair not avoided by this filter has LRA evidence: both
  strict separations conflict with the current arithmetic assignment. In the
  default-off model-equality mode, such a pair is handled by learning a guarded
  equality clause from the two strict-separation conflict reasons and re-entering
  SAT, not by enqueueing a model-equality assumption. Equalities whose current
  model value is accidental or mutation-avoidable still stay out of entailed
  propagation. This is intentionally not full Nelson-Oppen: generic
  `DPLL(T^M)` state, freedom-interval mutation, a mutable shared theory-model
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
  Equality propagation still uses the full shared Real interface pair set
  unless `--qf-uflra-candidate-final-scan` is enabled for the final LRA-to-EUF
  equality scan. Final arrangement validation scans the shared Real interface
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
  fresh equality atom. The optional branch path is owned by
  `ModelEqualityArrangementState`: a selected LRA-model-equal pair is queued,
  then activated as an IPASIR assumption on the next SAT call. SAT-level failed
  assumptions are blocked by a learned clause over their negations and removed
  from the active assumption trail; ordinary EUF/LRA theory conflicts under
  active assumptions are learned by the normal theory-conflict path. Guesses are
  not exposed as entailed theory propagations; only mutation-forced LRA equalities
  with strict-separation conflict reasons use the guarded equality-clause path.
  In default mixed QF_UFLRA solving, the original LRA atoms are observed by the
  same trail-synced online LRA propagator used by pure QF_LRA, so arithmetic
  conflicts and bound propagations can be delivered inside the SAT search. Final
  checks reuse that online state while no generated interface equality is true,
  and rebuild an augmented prepared LRA theory only when true generated
  interface equalities must be included. The experimental
  `--model-equality-branching` path keeps the older final-check-only LRA path so
  its branch-ranking diagnostics remain comparable.
- Default-on finite-domain UF symmetry breaking, with
  `--no-uf-symmetry-breaking` as an explicit disable switch.

Not implemented yet:

- push/pop assertion stacks.
- proof production.
- full model construction for mixed-theory fragments beyond the documented
  QF_UFLRA subset, arithmetic functions beyond nullary `Real` constants and
  Real-valued UF tables, and reconstruction through future
  satisfiability-preserving rewrites outside the current preprocessing trail.
- Full Nelson-Oppen theory combination with a mutable shared equality model,
  unbounded arrangement enumeration, generic model-equality propagation,
  care-set-driven default search, and fine-grained minimal cross-theory
  explanations. The current QF_UFLRA path has guarded bidirectional shared-Real
  equality propagation for equalities reported by EUF/LRA final checks, plus a
  default-off guarded clause for LRA model-equal pairs whose strict separations
  both conflict.
- explanation-producing congruence closure with proof forests.

## Paper Traceability

- IPASIR-UP motivates the SAT-backend interface. `src/sat/` owns the
  backend-neutral propagator/terminator contracts and adapts them to CaDiCaL's
  concrete C++ API.
- Congruence-closure papers motivate the EUF direction. The current implementation has congruence closure and disequality checking, but it does not yet implement explanation-producing union-find or the full fast algorithms described in the literature notes.
- DPLL(T) papers motivate the lazy SAT/theory loop. Primo currently implements a simple lazy loop that rebuilds/checks theory consistency from each SAT assignment rather than an incremental propagating theory solver.
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

Dolmen is used as an optional external validator for SMT-LIB model response
files. It is not vendored under `contrib/`; install it separately and run
`scripts/validate_model.py`.

Downloaded archives and build trees under `contrib/downloads`, `contrib/src`, and `contrib/local` are not committed.
