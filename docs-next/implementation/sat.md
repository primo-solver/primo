# Boolean abstraction and SAT integration

Primo's SMT layer translates Boolean terms into clauses and remembers what the
SAT literals mean. The SAT layer owns propositional search through CaDiCaL;
it receives no core terms or SMT-LIB syntax. This chapter follows that boundary
from an assertion to a SAT answer. The [architecture overview](architecture.md)
places it in a query, and the [core chapter](core.md) explains term identity.
The [DPLL(T) chapter](dpllt.md) continues through theory callbacks and the
checks needed before that SAT answer becomes an SMT answer.

The owning implementation is
{source}`BooleanClausifier <src/smt/clausifier.hpp#L20-L57>`.
The separately named {source}`Abstraction <src/smt/abstraction.hpp>` is an
empty scaffold; it does not own the working term-to-literal map.

## Representation and ownership

| Object and code | Representation | Ownership and validity |
| --- | --- | --- |
| {source}`sat::Lit <src/sat/lit.hpp>` | Signed 32-bit integer: `v` or `-v` for positive variable ID `v` | Contains no term, backend identity, or lifetime guard. Zero is not a clause literal. |
| {source}`sat::Clause <src/sat/clause.hpp>` | Vector of literals, interpreted as a disjunction | `add_clause` submits it to the backend; an empty clause is false. |
| {source}`BooleanClausifier::Implementation <src/smt/clausifier.cpp#L587-L601>` | Dense term-indexed literal/registry-index/polarity arrays, atom vector, and Boolean-domain bookkeeping | Owns the maps; borrows one backend and one term manager, which must outlive it. |
| {source}`TheoryAtom <src/smt/clausifier.hpp#L25-L31>` | Literal, `TermId`, syntactic polarity flags, and a both-polarities relevance flag | Entry in an SMT-owned vector; its name does not imply that every entry requires theory reasoning. |
| {source}`CadicalBackend <src/sat/cadical_backend.hpp>` | CaDiCaL solver, callback adapters, variable accounting, last result, and counters | Owns the solver and adapters; adapters borrow the connected propagator and terminator. |

The mapping is local to a clausifier/backend pair. Repeated encoding of a term
reuses its literal. Negation can reuse the same variable with the opposite
sign, so the relationship is not one fresh SAT variable per `TermId`. A SAT
variable is not a `TermId` and must not be interpreted as one.

{source}`ensure_term_index <src/smt/clausifier.cpp#L543-L549>` grows the dense
arrays as new terms appear. Atom registration can grow the atom vector;
references returned by `ensure_theory_atom` and elements obtained through
`theory_atoms()` must not be kept across operations that can append entries.
Copy the IDs/literals needed, then reacquire entries. The core term-reference
invalidation rules still apply, including when Boolean-domain bridges create
new terms.

### Literal range and exhaustion

`Lit` uses a signed 32-bit value. Positive variable IDs therefore have a
representational upper bound of `2³¹ − 1` (2,147,483,647); negation changes
the sign, not the variable ID. Zero is reserved, and `INT32_MIN` cannot be a
valid literal because its positive counterpart is not representable. This
bounds variable IDs, not the number of clauses or literal occurrences.

The installed CaDiCaL API also requires 32-bit literals and excludes `INT_MIN`.
The representational bound is not a promise that the backend can allocate
that many variables. Widening only Primo's `Lit` would not widen that API.

There is a concrete hardening gap: {source}`new_observed_var
<src/sat/cadical_backend.cpp#L173-L187>` computes
`max(max_var_, solver_->vars()) + 1` in signed arithmetic without an explicit
capacity check. The fallback branches of {source}`new_var
<src/sat/cadical_backend.cpp#L153-L171>` also increment unchecked; the current
preferred branch delegates allocation to CaDiCaL. Signed overflow is not a
valid wraparound strategy. Allocation exhaustion should produce a controlled
resource-limit failure before arithmetic or backend mutation.

**Open, low-priority hardening item; no overflow observed in this review.**
The expected likelihood is low at ordinary workload sizes, but this is not
protected by a guarantee that memory runs out first. Long-lived incremental
sessions can keep allocating auxiliaries and scope selectors; deleting clauses
does not by itself give Primo permission to reuse their IDs. Audit allocation,
invalid externally supplied literals, and derived index/size arithmetic before
declaring a supported limit. Boundary tests should use a testable capacity
check or small artificial limit, rather than allocating billions of variables.
The contributor should preserve 32-bit storage unless a separate backend/API
design establishes a need for wider IDs. No allocation behavior is changed here.

## From terms to clauses

The input is a Boolean root in the borrowed term manager. In the solving
pipeline, frontend translation and lowering have already given Boolean
equality, `xor`, and extended operators their supported representation. The
clausifier does not type-check arbitrary SMT-LIB or supply semantics for an
unlowered operator merely because it has an atom case in a switch.

{source}`assert_formula <src/smt/clausifier.cpp#L29-L35>` checks the root sort,
collects atom polarities, and asserts the root positively. Each call adds to
the same clause database, so an assertion vector denotes a conjunction.
The output is permanent clauses in the backend plus the retained abstraction
maps; this operation does not itself search or decide theory consistency.

### Asserted roots and reusable definitions

{source}`assert_positive <src/smt/clausifier.cpp#L323-L395>` uses an explicit
worklist with a visited set local to the call:

| Asserted root | Clauses emitted |
| --- | --- |
| `true` / `false` | Nothing / an empty clause. |
| `And` | Assert its children; no variable is needed just for this root. |
| `Or`, with `top_level_or_tseitin=false` | One clause over the encoded children. |
| `Implies(a, b)` | One clause `¬lit(a) ∨ lit(b)`. |
| Other supported roots, or `Or` with `top_level_or_tseitin=true` | Encode the root and assert its literal as a unit clause. |

Primo currently gives nested connectives a literal whose meaning survives use
under either polarity. {source}`encode <src/smt/clausifier.cpp#L397-L507>` walks the DAG
iteratively, caches literals, and emits full definitions. Writing `xᵢ` for a
child literal and `v` for the new output literal, the implemented templates are:

| Definition | Required clauses | Owning operation |
| --- | --- | --- |
| `v ↔ And(x₁, …, xₙ)` | `¬v ∨ xᵢ` for each child; `v ∨ ¬x₁ ∨ … ∨ ¬xₙ` | {source}`encode_and <src/smt/clausifier.cpp#L551-L559>` |
| `v ↔ Or(x₁, …, xₙ)` | `¬xᵢ ∨ v` for each child; `¬v ∨ x₁ ∨ … ∨ xₙ` | {source}`encode_or <src/smt/clausifier.cpp#L561-L569>` |
| `v ↔ (a → b)` | `a ∨ v`; `¬b ∨ v`; `¬v ∨ ¬a ∨ b` | {source}`encode_implies <src/smt/clausifier.cpp#L571-L575>` |

`Not(t)` uses `-lit(t)` without a new definition. When an encoded constant
needs a literal, {source}`true_lit <src/smt/clausifier.cpp#L533-L539>` creates a
shared unit-constrained variable; false uses its negation.

The {source}`option defaults <src/smt/check_sat.hpp#L446-L447>` select direct
root-OR clauses, but a
{source}`formula-shape policy <src/smt/formula_analysis.cpp#L142-L152>` can
enable root-OR definitions for a selected goal. The worked example below fixes
the constructor flag explicitly. Its clause counts are not a promise for the
same formula after an arbitrary preprocessing/dispatch path.

Each encoded `And` or `Or` with `n` children contributes `n + 1` clauses and
`3n + 1` literal occurrences. Definitions are cached across calls, and their
size is linear in the encoded DAG's nodes and direct child edges. Root walks
may revisit shared structure across separate assertions; their visited set is
not shared across calls. Dense arrays also consume space for term indices
outside the encoded Boolean DAG. These bounds describe translation, not SAT
search time or total solver memory.

### Polarity-aware definitions: an open lead

Full equivalence is sufficient, but is not always necessary. With a fresh
auxiliary `v` for subformula `F`, **Plaisted–Greenbaum** conversion needs
`v → F` for positive-only occurrences and `F → v` for negative-only
occurrences. Both directions are required when one shared auxiliary serves
both polarities. The root requirement is still asserted.

In the example below, `q ∧ ¬r` occurs only positively, so a directional encoder
could omit `v ∨ ¬q ∨ r`, reducing four clauses to three. Primo currently emits
all four. Its atom-polarity metadata does not perform this optimization.

The [literature note](../papers/polarity-aware-cnf.md) includes direct SMT
references and an implementation checklist. General PG encoding remains an
open lead: incremental opposite-polarity uses require completing definitions,
and a one-direction auxiliary cannot automatically stand for the exact value
of a Boolean term in a model or function argument. The earlier rejected
root-polarity prototype did not evaluate general PG conversion.

### Atom registry and polarity

{source}`theory_atom_for_term <src/smt/clausifier.cpp#L311-L321>` reuses an
existing term/literal association or appends a registry entry. Boolean
constants and predicates are registered as well as equality and arithmetic
atoms. Quantified roots are opaque atoms for their separate solver route;
their bodies are not traversed as ordinary ground Boolean structure.

{source}`collect_theory_atom_polarities <src/smt/clausifier.cpp#L233-L309>`
tracks positive/negative occurrence through connectives: `Not` reverses the
sign and an implication reverses it for the antecedent. These flags describe
syntax, not the current SAT assignment. They accumulate in the retained
clausifier and are not undone by scope pop. They support downstream policies;
they do not turn the full definitions above into a polarity-only encoding.

{source}`ensure_generated_theory_atom_with_lit <src/smt/clausifier.cpp#L53-L75>`
records a caller-supplied literal and rejects a conflicting existing mapping.
The caller still owns the obligation to allocate/observe the variable and
establish the generated atom's semantics. Registration alone does not prove
a theory lemma.

Boolean arguments to uninterpreted functions need additional care: Bool has
two values, so a propositional assignment must agree with the EUF value of an
argument. The default registration mode
{source}`marks the relevant Boolean constants <src/smt/clausifier.cpp#L140-L194>`
for both-polarity theory relevance. This reuses their existing literal when
present; an argument not previously encoded still needs its first SAT variable.
The {source}`alternative bridge encoding <src/smt/clausifier.cpp#L196-L231>`
adds explicit equality atoms and clauses. The
{source}`mode dispatch <src/smt/solver_engines.cpp#L10521-L10532>` selects these
mechanisms; their EUF reasoning is separate chapter work.

## Worked example: one nested conjunction

Construct Boolean constants `p`, `q`, and `r`, then assert
`p ∨ (q ∧ ¬r)` with direct root-OR clauses. Let `v` name `q ∧ ¬r`.
The nested definition and asserted root give:

```text
¬v ∨ q
¬v ∨ ¬r
 v ∨ ¬q ∨ r
 p ∨ v
```

There are four SAT variables and four submitted clauses. If `p` is false,
the last clause forces `v`, hence `q` and `¬r`. If `p` is true, the definition
still makes `v` agree with `q ∧ ¬r`, without constraining `q` or `r` otherwise.
Existentially eliminating `v` therefore recovers exactly the asserted formula.

The executable {download}`example <../examples/boolean-abstraction.cpp>`
constructs these terms directly, calls the real clausifier and CaDiCaL adapter,
checks the clause counters before search, and checks all eight assignments to
`p`, `q`, and `r`. It also checks the guarded version described next. Using the
internal API here isolates clausification from parser and preprocessing effects;
it is not a public library API example.
The [review guide](../review.md#sat-chapter-checks) gives the build/run commands.

## Permanent clauses, assumptions, and scope guards

{source}`add_clause and assume <src/sat/cadical_backend.cpp#L189-L222>` have
different lifetimes. A submitted clause persists; an assumption applies to
the next solve. Repeated queries must resubmit their active assumptions.
An empty clause makes the permanent database inconsistent, while contradictory
assumptions can make only the current query UNSAT.

{source}`assert_guarded_formula <src/smt/clausifier.cpp#L44-L51>` emits full,
unconditional definitions for the formula and only guards its root:

```text
definitions of F
¬s ∨ lit(F)
```

Assuming selector `s` activates `F`. Setting `s` false disables the root
requirement, while the definitions remain satisfiable extensions of any
assignment to the original atoms. Full definitions also make the auxiliary
literal safe to reuse under either polarity; a one-way implication alone
would not guarantee that equivalence.

The opt-in assumptions engines implement scopes on top of this SAT interface.
For example, {source}`IncrementalUfEngine <src/smt/solver_engines.cpp#L12306-L12348>`
creates a selector when a scope first gains an assertion and permanently adds
`¬s` when that scope is popped. Its
{source}`query loop <src/smt/solver_engines.cpp#L12389-L12419>` resubmits active
selectors on every solve, including theory-refinement retries. No clause or
term deletion follows from this scheme. Default rebuild sessions have a
different owner lifetime, as explained in the [architecture chapter](architecture.md).

## The backend and callback contract

The [Yices literature note](../papers/yices.md#the-2016-talk-what-lemma-caching-means)
explains how a requested theory explanation can also become a reusable SAT
clause. Reason storage in a propagator, deferred construction, and retention
by the backend are separate mechanisms; their remaining policy questions are
recorded as [open work](../leads/theory-lemmas.md).

{source}`CadicalBackend::new_var <src/sat/cadical_backend.cpp#L153-L171>` uses
the available dependency API to declare ordinary variables between solves.
{source}`new_observed_var <src/sat/cadical_backend.cpp#L173-L187>` is the
separate path for fresh variables during callbacks. It accounts for variables
already created inside CaDiCaL and observes the new variable. The
{source}`capability flags <src/sat/cadical_backend.cpp#L290-L300>` describe what
this backend supports; a caller must not assume another adapter supports it.

The {source}`ExternalPropagator interface <src/sat/ipasir_up.hpp#L39-L60>`
provides the following exchanges. The connected SMT propagator owns the term
mapping and theory state behind them.

| Exchange | Contract at the boundary |
| --- | --- |
| `notify_assignment`, `notify_new_decision_level`, `notify_backtrack` | Maintain theory state for the notified SAT trail and undo changes above the reported backtrack level. |
| `cb_propagate` | Return an observed literal, or zero when there is no propagation; retain a valid explanation. |
| `cb_add_reason_clause_lit` | Stream the requested propagation's reason clause, ending with zero. |
| `cb_has_external_clause`, `cb_add_external_clause_lit` | Announce and stream a theory conflict/lemma clause, ending with zero. |
| `cb_check_found_model` | Check the candidate Boolean assignment against the external constraints; a rejection needs a valid continuation such as a new lemma. |
| `cb_decide` | Optionally suggest a decision literal; zero leaves the choice to SAT. |

Zero is a callback sentinel or clause-stream terminator, not an ordinary
`sat::Lit` to put in a `Clause`. A reason such as `¬a ∨ b` must actually justify
propagating `b` from `a`; the SAT adapter cannot establish that theory fact.
Theories must preserve explanations for the backend's reason requests.

Only one external propagator connects to a backend at a time. A theory
dispatcher handles multiple components behind that connection. Relevant
variables must be observed after connection; callback propagation and external
clauses must respect the observed-variable contract. The adapter exposes no
operation for removing individual observed variables.

The {source}`callback adapter <src/sat/cadical_backend.cpp#L35-L85>` forwards
to borrowed Primo objects. Disconnect before destroying those objects, including
on cancellation or exceptions. Between queries,
{source}`reconnect_external_propagator <src/sat/ipasir_up.cpp#L7-L12>` disconnects,
connects the new callback, and re-observes variables. The old callback must
remain alive through disconnection; the new one must handle replayed root
assignments. Connection is not ownership transfer. The
{source}`terminator guard <src/smt/solver_runtime.cpp#L29-L35>` supplies scoped
disconnection for cancellation callbacks.

### Results and limits

{source}`solve, value, and failed <src/sat/cadical_backend.cpp#L224-L276>`
separate SAT, UNSAT, unknown/interrupted, and a build without the backend.
`value(v)` is queried after SAT using a positive variable ID; the caller applies
the sign of its literal. `failed(lit)` concerns an assumption after UNSAT.
Query these in the appropriate backend state, before changing the formula or
assumptions; the wrapper's last-result field is not a full state-machine guard.

The {source}`Boolean-only solve path <src/smt/boolean_solver.cpp#L20-L64>`
shows the complete composition, including cancellation, counters, and typed
model reconstruction. A Boolean assignment is not an SMT interpretation;
the theory engines have further obligations before returning a solver answer.
Builds without CaDiCaL return `Unsupported` from the adapter, not a fallback
SAT search. Proof-producing integration is not implemented by this interface.

## Sources and implementation status

This section is the local paper-to-code checklist for the chapter. The
clauses and lifetimes above describe the implemented subset; they do not claim
the full mechanisms or results of the cited papers.

- **G. S. Tseitin, “On the Complexity of Derivation in Propositional
  Calculus” (1968; English reprint in *Automation of Reasoning*, 1983,
  pp. 466–483), §1, p. 467.**
  [Primary text](https://yurichev.com/mirrors/SAT/Tseitin/Tseitin1968_en.pdf).
  The relevant construction assigns variables to subformulas and constrains
  their meanings. Primo implements ordinary definitional clauses for its
  Boolean DAG, with n-ary connective templates and direct positive-root rules.
  The paper's validity reduction negates its root; Primo asserts the desired
  formula positively for satisfiability. No proof-complexity algorithm or
  complete polarity-sensitive clausifier is claimed.
- **Plaisted–Greenbaum conversion and SMT:** the
  [dedicated note](../papers/polarity-aware-cnf.md) records Masina, Spallitta,
  and Sebastiani (JAIR 2025), §§2.1 and 3.2, and SMTInterpol's primary
  documentation. General directional encoding is **open work**, not the
  implemented full definitions described above.
- **K. Fazekas, A. Niemetz, M. Preiner, M. Kirchweger, S. Szeider, and A. Biere,
  “IPASIR-UP: User Propagators for CDCL,” SAT 2023, §§2.1–2.4.**
  [Primary text and publication record](https://doi.org/10.4230/LIPIcs.SAT.2023.8).
  Primo implements the callback boundary through its own interfaces and a
  CaDiCaL C++ adapter. Its assignment notification is vector-shaped; the
  compatibility branch wraps a scalar notification and discards its fixed flag.
  Primo omits the paper's `remove_observed_var`, `is_decision`, and `unphase`
  operations. This is not a complete implementation of every proposed API or
  of the paper's proof-producing applications.

| Obligation | Implemented owner and verification |
| --- | --- |
| Definitions preserve Boolean meaning and sharing with linear clause output | `BooleanClausifier` caches, worklists, and templates above; the worked truth table and {source}`reconvergent DAG regression <tests/unit/boolean_solver_tests.cpp#L825-L847>`. |
| Popped roots cease constraining the problem while definitions remain valid | Guarded-root construction and selectors; {source}`UF scope tests <tests/unit/smt_session_tests.cpp#L61-L122>` and {source}`LRA scope tests <tests/unit/smt_session_tests.cpp#L277-L325>`. |
| Bool-valued arguments have only their two semantic values | Registration and bridge contracts; {source}`Boolean-domain regressions <tests/unit/boolean_solver_tests.cpp#L2875-L2931>`. |
| Trail changes, reason lifetime, cancellation, and fresh observation respect the backend protocol | {source}`backend regressions <tests/unit/sat_backend_tests.cpp>` and {source}`component explanation tests <tests/unit/component_combination_tests.cpp>`. Callback costs depend on the chosen theory; the interface promises no constant-time theory operation. |

Original Primo notes explain the
{source}`guarded-scope design <docs/assumption-incremental-uf.md>` and
{source}`Boolean sharing contract <docs/boolean-dag-flattening.md>`.
The {source}`root-polarity prototype <docs/root-polarity-clausification.md>`
was rejected; its signed root worklist is not current behavior. Clause/search
counters are diagnostics, not evidence of a newly measured speedup. The
[review ledger](../review.md) records this chapter's checks; future solver
changes still require the [fresh correctness gates](../development/correctness.md).
