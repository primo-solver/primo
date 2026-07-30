# Architecture

Primo is organized as layered components with explicit data ownership. This page describes the implementation that exists now, not the intended final solver.

## Layers

- `smtlib/` parses SMT-LIB commands into lightweight syntax objects.
- `core/` owns sorts, symbols, and hash-consed terms.
- `sat/` owns propositional variables, literals, clauses, assumptions, and the CaDiCaL/IPASIR backend adapter.
- `smt/` owns SMT-LIB command processing, preprocessing, Boolean abstraction,
  clausification, QF_UF/QF_LRA solver orchestration, and the current separable
  mixed QF_UFLRA coordinator.
- `theory/` owns equality reasoning, congruence closure, and arithmetic
  tableau solving.

## Boundaries

- SMT-LIB syntax objects are consumed by `smt::Frontend` and converted to `core::TermId` values before reaching SAT or theory code.
- SAT clauses contain `sat::Lit`, not terms.
- The Boolean clausifier keeps the mapping from Boolean theory atoms to SAT literals.
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
Top-level asserted disjunctions are asserted as one clause by default. The CLI
flag `--top-level-or-tseitin` enables the same cached Tseitin encoding as nested
disjunctions and asserts the auxiliary literal by a unit clause; this adds binary
support clauses that can expose branch literals to SAT search, but a 334-row
QF_UF SLURM validation rejected it as a default because it regressed PEQ and
SEQ rows. Equalities and Boolean-returning predicate applications become theory
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
  constants. The SMT layer records declared Real constants, the LRA path reads
  their values from the final `LraTableau` assignment, and model reconstruction
  replays Real equalities recorded by preprocessing so solved-away constants
  are printed for the original problem.
- Minimal QF_UFLRA model printing. For separable mixed formulas,
  `solve_qf_uf_lra` composes the QF_UF model body with the nullary-Real QF_LRA
  definitions. UF symbols may take Real arguments or return `Real` values:
  model reconstruction augments the LRA side with EUF-implied Real-constant
  equalities and strict separations for EUF-distinct Real constants before
  printing concrete Real argument values and Real-valued UF application results
  in the UF function tables.
- Read-only Nelson-Oppen interface diagnostics in
  `smt::analyze_nelson_oppen_interface`: the SMT layer counts user-visible,
  non-Boolean constants by sort and already-visible equality atoms over those
  constants after preprocessing when `--nelson-oppen-diagnostics` is enabled.
  It also materializes an opt-in registry of same-sort interface pairs marked
  as `Visible` or `Missing`, and reports purification sites for Real terms in
  EUF atoms and uninterpreted applications in arithmetic atoms. This is
  bookkeeping only; it does not create proxy terms or equality atoms, propagate
  interface equalities, combine theories, or inspect theory-local models.
- Partial Nelson-Oppen-style mixed QF_UFLRA final checks in
  `smt::solve_qf_uf_lra`. The SMT layer accepts one SAT assignment, partitions
  visible theory atoms into EUF and LRA atoms, checks both theories, and learns
  the first ordinary per-theory conflict clause it finds. Arithmetic atoms may
  contain Real-valued UF applications: a post-lowering SMT preprocessing slice
  replaces each such application with an internal Real proxy and adds an EUF
  definition equality. EUF atoms may also contain non-constant Real arguments:
  the same purification slice replaces each such arithmetic term with an
  internal Real proxy and adds the two LRA bounds that define the proxy as equal
  to the arithmetic term. The coordinator creates positive SAT-visible equality
  atoms for shared Real-constant/proxy pairs. For the current SAT assignment,
  generated equality atoms that are true are asserted to both EUF and LRA;
  equality atoms that are false are not treated as Real disequalities. EUF- or
  LRA-implied shared equalities are learned as unit clauses over those generated
  equality literals, after which the receiving theory reports any ordinary
  conflict using that literal as part of its reason. This is intentionally not
  full Nelson-Oppen: fine-grained minimal cross-theory explanations,
  model-equality guesses, false-disequality arrangement enumeration, and
  unbounded arrangement enumeration remain unsupported. Mixed model
  reconstruction uses a bounded search over strict shared-Real separation
  orientations, with a greedy fallback when the candidate set exceeds the
  current model-reconstruction cap.
- Default-on finite-domain UF symmetry breaking, with
  `--no-uf-symmetry-breaking` as an explicit disable switch.

Not implemented yet:

- push/pop assertion stacks.
- proof production.
- model construction for mixed-theory fragments, arithmetic functions beyond
  nullary `Real` constants, and reconstruction through future
  satisfiability-preserving rewrites outside the current preprocessing trail.
- Full Nelson-Oppen theory combination with bidirectional shared-variable
  equality propagation, model-equality propagation, care sets, fine-grained
  cross-theory explanations, and SAT atom creation for missing interface
  equalities.
- explanation-producing congruence closure with proof forests.

## Paper Traceability

- IPASIR-UP motivates the SAT-backend interface, but the current backend is a direct CaDiCaL/IPASIR-style adapter in `src/sat/`.
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
