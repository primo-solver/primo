# Terms, symbols, and ownership

Primo stores formulas as a shared graph of typed terms. A symbol table owns
the signature, a term manager owns the graph, and other layers refer to nodes
by numeric IDs. The [architecture overview](architecture.md) shows where these
owners fit into a solving session; this chapter explains their storage and
lifetime contracts.

Continue with [Boolean abstraction and SAT integration](sat.md) to see how
Boolean terms become clauses while their meanings remain in the SMT layer.

## Representation

| Type and code | Stored data | Ownership contract |
| --- | --- | --- |
| {source}`Sort <src/core/sort.hpp>` | Name and kind: `Bool`, `Real`, `Int`, or uninterpreted | Stored in the symbol table's sort vector. |
| {source}`Symbol <src/core/symbol_table.hpp>` | Name, argument sorts, result sort, kind, and internal-symbol flag | Stored in the symbol table's symbol vector; sort references are IDs. |
| {source}`Term <src/core/term.hpp>` | Kind, result sort, child IDs, and kind-specific payload | Stored in the term manager's term vector; children are shared through IDs. |
| {source}`SymbolTable <src/core/symbol_table.hpp>` | Sorts, symbols, name lookup maps, and scope trails | Owns the signature storage and controls which names are visible. |
| {source}`TermManager <src/core/term_manager.hpp>` | Terms, structural interning table, and binder metadata | Owns term storage and borrows its symbol table. |

{source}`SortId, SymbolId, and TermId <src/core/ids.hpp>` are aliases of
`std::uint32_t`. Each is an index interpreted by its owner. They contain no
owner identity or generation, and the aliases do not enforce distinct C++
types. An in-range ID from another manager can therefore identify an unrelated
object without failing a bounds check. Callers must preserve the association
between an ID and its owner.

### Sorts, declarations, and visibility

{source}`SymbolTable's constructor <src/core/symbol_table.cpp#L13-L27>` installs
the three built-in sorts. `declare_sort` adds an uninterpreted sort.
Declarations record signatures: constants have no argument sorts; functions
and predicates have argument lists, with predicates returning `Bool`.
{source}`add_symbol <src/core/symbol_table.cpp#L119-L133>` checks that the
referenced sorts exist and rejects a name already visible in the symbol table.

{source}`push_scope and pop_scope <src/core/symbol_table.cpp#L80-L96>` scope
name lookup. Pop removes bindings introduced since the matching push, while
the sort and symbol vectors retain their entries. Redeclaring a popped name
allocates a new ID. This keeps old terms interpretable within the same manager
without making a popped declaration available to new name resolution.

Generated auxiliaries have an explicit `Symbol::internal` flag.
{source}`visible_symbols <src/core/symbol_table.cpp#L98-L106>` checks that flag
and the current binding; it does not classify symbols by a spelling convention.

### Node shapes

The {source}`TermKind declarations <src/core/term.hpp>` determine how to read
each node's payload. Every node has an explicit result sort, including Boolean
formulas. A `Term` record has fields for all payload categories rather than a
separate C++ subclass for each kind.

| Kinds | Representation |
| --- | --- |
| `BoolValue`, `Rational` | A Boolean value or exact rational payload. An `Int` literal must have an integral value. |
| `Constant` | A symbol ID and no children. |
| `Application` | A function or predicate symbol ID and ordered argument IDs. Constants have their own node kind. |
| `Equality`, `Not`, `And`, `Or`, `Implies` | Boolean result and ordered operand IDs. |
| `Plus`, `Times`, `Le`, `Lt` | Arithmetic operands; `Times` has one child and a scalar payload, so it does not represent arbitrary multiplication. Comparisons return `Bool`. |
| `Ite` | Condition, then-branch, and else-branch IDs; the result has the common branch sort. |
| `IntDiv`, `IntMod`, `Abs` | One `Int` operand; division and modulo also store a normalized positive literal divisor. |
| `BoundVariable` | Sort and de Bruijn index, stored in the `symbol` field as an integer rather than a symbol-table entry. |
| `Forall`, `Exists`, `Pattern` | Binder declaration leaves, body, and optional pattern nodes; patterns hold their term IDs. These node kinds have sort `Bool`. |

The core stores no SMT-LIB `let` node. Name binding and expansion belong to
{source}`Frontend <src/smt/frontend.hpp>`; the resulting graph uses ordinary
term IDs. Parsing, printing, and the detailed frontend binding rules belong
to their own chapters.

## Construction, sharing, and equality

Create terms through `TermManager::mk_*`. These operations return IDs and can
reuse a node or simplify an expression instead of allocating the requested
node shape. Consumers read stored nodes through const accessors.

{source}`TermManager::Key <src/core/term_manager.hpp#L88-L97>` contains kind,
sort, symbol/payload index, the ordered child list, Boolean value, and rational
value. {source}`intern <src/core/term_manager.cpp#L540-L560>` looks up that
structural key. A hit returns the existing ID; a miss appends a term, records
its binder metadata, and inserts the key-to-ID mapping.

Children already exist when a parent is interned. Together with read-only
access to stored nodes, this keeps the graph built through these constructors
acyclic while allowing many parents to share one child.

Hash-consing shares equal *constructed representations*. Within one manager,
`mk_app(f, {a})` repeated with the same arguments returns the same ID.
Asserting `a = b` does not change those IDs or make `f(a)` and `f(b)` the same
stored node. Theory reasoning and model evaluation establish semantic equality
separately. Constructors also preserve operand order: `mk_eq(a, b)` and
`mk_eq(b, a)` can be distinct terms.

### Local simplification

| Constructor and code | Behavior relevant to representation |
| --- | --- |
| {source}`mk_app <src/core/term_manager.cpp#L47-L62>` | Checks arity and argument sorts against the symbol signature; constant symbols use `mk_constant`. |
| {source}`mk_eq <src/core/term_manager.cpp#L64-L79>` | Requires matching operand sorts; equal IDs fold to the Boolean constant `true`. |
| {source}`mk_not <src/core/term_manager.cpp#L81-L94>` | Folds Boolean constants and removes double negation. |
| {source}`mk_and and mk_or <src/core/term_manager.cpp#L96-L158>` | Fold Boolean constants, remove duplicate direct operands, and collapse empty/singleton results. Preserve the order of surviving operands and nested same-kind nodes. |
| {source}`mk_implies <src/core/term_manager.cpp#L160-L181>` | Folds equal operands and constant antecedents/consequents. |
| {source}`mk_ite <src/core/term_manager.cpp#L307-L334>` | Checks the Boolean condition and matching branch sorts; a literal condition selects a branch. Equal branches alone do not trigger a constructor rewrite. |

Preserving nested `And` and `Or` nodes is a memory decision. A shared prefix
such as `and(and(p, q), r)` keeps the inner node as a child. Expanding every
prefix into a fresh flat operand list can turn a small shared graph into large
retained child arrays. Optional bounded flattening belongs to SMT preprocessing;
the {source}`original Boolean-DAG design note <docs/boolean-dag-flattening.md>`
records that boundary and its motivating failure.

The arithmetic constructors perform their own limited normalization and exact
constant folding. For example,
{source}`mk_plus and mk_times <src/core/term_manager.cpp#L197-L262>` can flatten
arithmetic shapes; the Boolean nesting rule is not a promise for every operator.
{source}`Int division/modulo validation <src/core/term_manager.cpp#L350-L389>`
requires a nonzero integral literal divisor. These constructors are not a
complete arithmetic normalizer or theory solver.

`get` reports an out-of-range term ID with `std::out_of_range`; checked arity,
sort, and operator-precondition failures use `std::invalid_argument`.
Callers must supply valid IDs and satisfy constructor preconditions. Checks
protect the constructed representation; short-circuit simplifications do not
promise to inspect every later argument after the result is already determined.
The generic {source}`Rewriter <src/core/rewrite.hpp>` and
{source}`Substitution <src/core/substitution.hpp>` classes are still empty
scaffolds. Their names do not describe a working general rewrite API.

## Binders and cached facts

{source}`mk_quantifier <src/core/term_manager.cpp#L426-L455>` stores the number
of bound variables in `symbol`. Its children are declaration leaves in source
order, followed by the body and any patterns. With two declarations, the first
has index 1 and the second index 0 in the body. Keeping declaration sorts in
the key distinguishes quantifiers even when some variables are unused; source
variable names do not enter that key.

{source}`record_binder_facts <src/core/term_manager.cpp#L482-L517>` maintains
`loose_index` and `contains_quantifier` arrays indexed by `TermId`. The former
is zero for a closed term, and otherwise one past its largest index left free
by the term's own binders. The arrays are allocated when the first binder-related
node is interned; earlier entries are initialized as closed and quantifier-free.
Each later new node derives its metadata from older children. Interning an
already existing node does not increment the quantifier or extended-term counts.

Local constructor checks do not establish that every bound occurrence refers
to an enclosing variable of the right sort. The separate
{source}`check_bound_variables interface <src/core/binders.hpp>` checks that
whole-term condition. The same module exposes shifting, instantiation, and
operator rebuilding; those can create terms and require the lifetime discipline
below. Their algorithms and quantified solving are separate chapter work.

## Allocation and borrowed references

The signature must outlive its term manager. The
{source}`frontend's member order <src/smt/frontend.cpp#L503-L504>` makes that
relationship concrete: it constructs the symbol table before the manager, and
destroys the manager first.

An ID remains usable across insertions into its original owner. A reference
returned by `TermManager::get` points into a vector and can be invalidated by
term creation. The same concern applies to references into the symbol and sort
vectors when declarations are appended. Copying an ID does not keep any of
these owners alive.

Before a direct or indirect term-creating call, copy the child IDs and payloads
needed by the operation. After the call, reacquire nodes by ID. This includes
helpers that appear to be reads but invoke constructors internally, and sibling
function arguments whose evaluation order must not be assumed. A `const Term&`
does not prevent another call from growing its backing vector.

| Operation | Allocation and lifetime effect |
| --- | --- |
| `get`, `size`, `symbols`, binder-fact queries | Read existing storage; do not intern terms. Returned references remain borrowed. |
| `mk_*`, binder transformations, preprocessing | May create nodes and grow term/key/metadata storage, even if some calls simplify or hit the interning table. |
| Sort/symbol declaration | Appends signature entries and lookup state; may invalidate borrowed sort/symbol references. |
| `visible_symbols` | Allocates a result vector of IDs; owning that vector does not own the signature. |
| Scope pop | Removes active bindings/state according to the owner; does not imply term reclamation. |

For a node with `k` direct children, building and hashing its key visits those
children; ordinary interning does not recursively inspect the descendants.
Expected hash-table behavior is not a worst-case constant-time guarantee, and
vector growth, rehashing, and exact-number operations add costs. Boolean
deduplication uses a bounded small scan or a hash set. The first binder metadata
allocation also initializes entries for the terms already present.

Storage includes both the `Term` and its retained hash key. In particular,
{source}`intern's copies <src/core/term_manager.cpp#L546-L557>` retain child
lists and rational payloads in both records. Memory estimates must include
nodes, edges, keys, container capacity, and numeric payload storage. Counting
only `sizeof(Term)` per node misses part of that cost.

[Compact term storage](../leads/term-storage.md) tracks possible union or
separate-payload representations, including the effect of future leaf types.
The existing record layout remains in use; that redesign is deferred.

## Queries, pop, and reset

The term manager has no per-term release, tracing collector, or scope-pop
operation. The surrounding session decides when to destroy it.

| Session operation | Effect on the term owner |
| --- | --- |
| Rebuild-mode `check-sat` | {source}`Transfers the frontend <src/smt/smt_session.cpp#L225-L232>` into one solving attempt. A later query recreates translation from the active journal; IDs from the old manager do not carry over. |
| Assumptions-mode `check-sat` | {source}`Reuses the frontend and engine <src/smt/smt_session.cpp#L84-L96>` across checks. Persistent IDs belong to that retained manager. |
| Assumptions-mode `pop` | {source}`Pops solver/frontend scopes and active assertion roots <src/smt/smt_session.cpp#L295-L326>`. Signature bindings are removed, but the retained manager still owns its previously created terms. |
| Nonzero rebuild-mode `pop` | Removes journal entries and discards translation; remaining commands are replayed when needed. |
| `reset-assertions` | {source}`Clears the journal and translation <src/smt/smt_session.cpp#L287-L292>`, including user declarations under Primo's scoped-declaration policy; retains the selected logic and session option state. |
| `reset` | {source}`Also clears the selected logic and session model/print-success flags <src/smt/smt_session.cpp#L243-L251>`. Neither reset operation makes old term IDs usable in the new manager. |

{source}`discard_translation <src/smt/smt_session.cpp#L77-L82>` destroys the
engine before the frontend whose terms it borrows. The
{source}`frontend pop implementation <src/smt/frontend.cpp#L240-L262>` removes
scoped definitions and lookup state without shrinking term storage.

An owning model has a different lifetime. Its
{source}`constructor <src/core/model.cpp#L57-L105>` copies the signature and
stores typed interpretations. A retained `CheckSatOutcome::typed_model` can
therefore survive destruction of the frontend. Evaluating an old `TermId`
still requires its live manager: {source}`ModelEvaluator <src/core/model.hpp>`
borrows it, and the model-based constructor also borrows the model. Its memo
belongs to that interpretation.

The copied signature includes the table's stored declarations; the model's
visible-symbol list selects what is exposed, rather than shrinking those
signature copies. Retaining a small printed model can therefore retain more
signature data than appears in its output.

Retained terms, clauses, caches, and model snapshots all contribute to memory.
The [incremental-memory lead](../leads/incremental-memory.md) tracks profiling,
reference counting, and possible collection policies. The
[library lead](../leads/library-api.md) tracks public handle and lifetime
guarantees. Neither is implemented by documenting the current representation.

## Worked example

This complete program uses the internal core headers. It shows repeated
construction sharing an ID, followed by name removal without term reclamation.
It is an illustration of the current internals, not a stable public library API.

```cpp
#include <cassert>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"

int main() {
  primo::core::SymbolTable symbols;
  primo::core::TermManager terms(symbols);
  const auto u = symbols.declare_sort("U");
  const auto a = terms.mk_constant(symbols.declare_constant("a", u));
  const auto f = symbols.declare_function("f", {u}, u);
  const auto fa = terms.mk_app(f, {a});
  assert(terms.mk_app(f, {a}) == fa);
  const auto reflexive = terms.mk_eq(fa, fa);
  assert(reflexive == terms.mk_bool(true));

  symbols.push_scope();
  const auto old_symbol = symbols.declare_constant("temporary", u);
  const auto old_term = terms.mk_constant(old_symbol);
  const auto retained_count = terms.size();
  symbols.pop_scope();
  assert(!symbols.lookup_symbol("temporary"));
  assert(terms.size() == retained_count);
  assert(terms.get(old_term).symbol == old_symbol);

  const auto new_symbol = symbols.declare_constant("temporary", u);
  assert(new_symbol != old_symbol);
  assert(terms.mk_constant(new_symbol) != old_term);
}
```

The old term is still readable because its manager and signature remain alive.
The second declaration has the same spelling but a different identity. Returning
`old_term` to a caller after these local owners are destroyed would not transfer
ownership or make the ID valid elsewhere.

## Tests and sources

Existing regression entry points connect the explanation to executable checks:

- {source}`Hash-consing and sort checks <tests/unit/core_tests.cpp#L65-L106>`;
  {source}`Boolean sharing <tests/unit/core_tests.cpp#L148-L169>`;
  {source}`ITE constructor boundary <tests/unit/core_tests.cpp#L379-L422>`.
- {source}`Binder construction and metadata <tests/unit/binder_tests.cpp#L71-L132>`.
- {source}`UF scope growth <tests/unit/smt_session_tests.cpp#L179-L210>` and
  {source}`LRA scope growth <tests/unit/smt_session_tests.cpp#L367-L389>`;
  {source}`models retained across rebuilds <tests/unit/smt_session_tests.cpp#L404-L432>`.
- {source}`Model ownership and printing <tests/unit/model_tests.cpp#L242-L263>`.

The original Primo notes on
{source}`Boolean DAG storage <docs/boolean-dag-flattening.md>`,
{source}`binder representation <docs/quantifiers.md>`, and
{source}`model ownership <docs/model-interpretation.md>` record the design
decisions behind these boundaries. Those notes also contain historical plans
and evaluations; this chapter describes the linked implementation. It does not
claim a new paper algorithm, a complete rewrite system, memory reclamation, or
library readiness. Detailed binder, model-construction, and preprocessing
algorithms remain separate chapters.
