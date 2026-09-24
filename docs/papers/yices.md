# Yices: architecture, dynamic Ackermannization, and lemma reuse

This survey covers all three `sandbox/yices-*.pdf` files. The 2006 paper
explains conflict-driven Ackermann clauses; the 2014 paper gives the library
architecture and theory combination; the 2016 talk explains representation,
lazy explanations, and selective retention of theory lemmas. These are
historical descriptions of Yices, not claims about its current release.

Primo has an optional, restricted Ackermann propagation mechanism. It does
not implement the whole conflict-driven selection policy described in 2006.
For lemma caching, the first question is what CaDiCaL already retains: the
presence of a reason callback alone does not answer that question.

## Sources and reading coverage

| Source | Local reading copy | Sections used |
| --- | --- | --- |
| Bruno Dutertre and Leonardo de Moura, *The Yices SMT Solver*, 2006. [Paper](https://yices.csl.sri.com/papers/tool-paper.pdf) | `sandbox/yices-tool-paper-2006.pdf`, 5 pages | §2: SAT/core/satellite architecture, equality exchange, dynamic Ackermannization (p. 3); §3: contexts and library use. |
| Bruno Dutertre, *Yices 2.2*, CAV 2014. [Paper](https://yices.csl.sri.com/papers/cav2014.pdf) | `sandbox/yices-cav2014.pdf`, 8 pages | §3: terms, contexts, models; §4: Boolean-aware UF and dynamic clauses; §5: preprocessing and model-based combination. |
| Bruno Dutertre, *The Nuts and Bolts of Yices*, SMT Workshop 2016, Coimbra. [Slides](https://yices.csl.sri.com/papers/smt2016-talk.pdf) | `sandbox/yices-smt2016-talk.pdf`, 30 PDF pages | Slides 2–6: architecture/representation; 7–14: DPLL(T), explanations, watches, caching; 15–22: congruence closure; 23–27: preprocessing. |

All three documents were read for this survey. Slide references use the printed
number; PDF page 15 is printed slide 14. The local PDFs are reading material,
not redistributed book assets. This note is maintained in `docs/papers/yices.md`
and included in the replacement book so the two previews share one account.

## The 2006 paper: exposing useful congruence to SAT

The problem is that forward congruence closure and Boolean search see different
opportunities. Equality of arguments implies equality of results, but a
disequality of results can also constrain the arguments. A clause makes both
directions available to SAT propagation.

For a unary uninterpreted function, let `A` denote `x = y` and `R` denote
`f(x) = f(y)`. The congruence implication is the theory-valid clause

```text
¬A ∨ R
```

If `R` is false, Boolean propagation can force `¬A`. For two arguments the
clause is `¬A₁ ∨ ¬A₂ ∨ R`: a false result equality requires *at least one*
argument disequality. It does not imply both argument disequalities. Knowing
`A₁` additionally lets SAT force `¬A₂`.

In §2, p. 3, Yices generates a congruence clause when that congruence step
participates in explaining a conflict. This selects useful application pairs
during search. It avoids eagerly encoding every pair of applications of each
function. The paper also permits deleting these additional clauses: the UF
decision procedure still checks consistency, so completeness does not depend
on retaining every shortcut.

The required state includes applications and their arguments, congruence
explanations identifying the participating pair, an atom-to-SAT mapping, and
the resulting clause. A Primo implementation must keep explanation production
in `theory/`, atom creation and scheduling in `smt/`, and clause storage and
watch handling behind `sat/`.

The paper gives no complexity bound for the selection heuristic. Our cost
inference is that eagerly considering `m` applications of one arity-`k`
function exposes up to `m(m−1)/2` pairs with `k+1` clause literals each, before
simplification or sharing. Dynamic generation reduces what is materialized;
it does not prove a better worst-case pair bound.

### What Primo currently implements

| Mechanism | Status and code |
| --- | --- |
| All-pairs cost diagnostics | **Implemented, diagnostic only.** `--euf-ackermann-diagnostics` estimates expansion costs; it does not install the reduction. See the {source}`diagnostic tests <tests/unit/boolean_solver_tests.cpp#L2147-L2200>`. |
| SAT-visible candidate discovery | **Implemented, restricted.** {source}`build_dynamic_ackermann_candidate_index <src/smt/solver_engines.cpp#L964-L1047>` starts from an existing equality between same-symbol applications and requires SAT atoms for every argument equality. It skips missing atoms. |
| Dynamic Ackermann shadow mode | **Implemented, diagnostic only.** `--euf-dynamic-ackermann-shadow` measures candidate states without adding the corresponding clauses. See {source}`shadow tests <tests/unit/boolean_solver_tests.cpp#L2202-L2274>`. |
| Watched argument-disequality propagation | **Partially implements the paper idea.** Default-off `--euf-diseq-ackermann` queues existing candidates by affected SAT variables and uses the clause as a propagation reason or conflict. {source}`Watch construction <src/smt/solver_engines.cpp#L2160-L2224>` applies width/per-symbol caps; {source}`candidate evaluation <src/smt/solver_engines.cpp#L2448-L2560>` requires the result equality to be false. |
| Selecting congruence steps from conflicts, then retaining their clauses for reuse | **Deferred as a complete policy.** The existing candidate index and assignment queue are not the conflict-explanation selector described by the paper. Missing-atom creation for this mechanism and a deliberate deletion policy also remain design questions. |
| Earlier scan-based dynamic clause experiment | **Not adopted; reverted.** `f83828f` introduced `--euf-dynamic-ackermann`; `160ce4e` reverted it. This old option is not the current `--euf-diseq-ackermann` mechanism. |

`DynamicAckermannCandidate` owns atom indices and a copied `sat::Clause`;
`OnlineEufPropagator` owns the candidate index, per-variable occurrence lists,
queue, and reasons. Repeated negative literals are removed when constructing
a candidate. Backtracking clears queued work and the pending external clause;
the candidate catalogue belongs to that propagator. These occurrence lists
are SMT-side scheduling data, not CaDiCaL's two watched literals.

The {source}`option defaults <src/smt/check_sat.hpp#L256-L266>` keep the
experimental modes off. The {source}`assumptions-mode admission check
<src/smt/smt_session.cpp#L101-L113>` rejects both shadow and disequality
Ackermann modes. Existing tests cover unary propagation, conflict delivery,
skipping a missing argument atom, and the n-ary disjunction invariant:
{source}`Ackermann regressions <tests/unit/boolean_solver_tests.cpp#L2276-L2389>`.
They are code pointers, not a claim of a fresh fuzz campaign.

The rejected scan experiment is recorded as
`2026-07-28-qfuf-dynamic-ackermann-real-f83828f-weak-local` in
{source}`the historical run ledger <docs/run-log.json#L16026-L16136>`.
It inspected about 547 million candidates to add 990 unit clauses on a local
weak manifest. That record is explicitly `needs_revalidation`; it is a warning
about repeated scans, not a current performance comparison or a rejection of
every dynamic Ackermann design.

Other mechanisms in §2 include selective interface equality creation, offset
equalities, simplex/difference logic, and quantified/array reasoning. These
are context for the architecture, not an adoption list. Primo's arithmetic
and combination algorithms need their own primary algorithm notes; this tool
paper alone is not an implementation specification for them.

## The 2014 paper: terms, contexts, and models

The central library lesson in §3 is the separation of three lifetimes. A
shared term/type database supports several configurable contexts. Each context
owns assertion processing and solving state. A returned model is an independent
object that remains usable after its originating context changes or is deleted.
Hash-consing and compact term descriptors reduce duplication in the shared
database. The paper does not establish a particular reference-counting or
garbage-collection algorithm for Primo to adopt.

**Partially matching building blocks; public library design deferred.** Primo
has {source}`TermManager <src/core/term_manager.hpp>` and
{source}`Model <src/core/model.hpp>`, but its
{source}`SmtSession <src/smt/smt_session.hpp>` is a command-oriented facade.
`Model` owns copied sort/symbol/value data; `ModelEvaluator` also borrows a live
term manager. This does not establish a supported shared-term/multiple-context
public API. The library and incremental-memory leads must define that ownership
contract explicitly.

Section 4 explains treating equality itself as a Boolean-valued term in UF.
This permits congruence to transport a false equality through equivalent
endpoints. It also cites dynamic Ackermannization, but provides no replacement
for the 2006 conflict-selection explanation. Section 5 describes symmetry
breaking and model-based theory combination: try to reconcile theory models
before introducing an interface split. Neither statement means all those
Yices mechanisms are implemented together in Primo. Existing separate notes
cover {source}`Boolean/equality propagation <docs/qfuf-disequality-propagation.md>`,
{source}`symmetry breaking <docs/papers/smt-symmetry-breaking.md>`, and
{source}`model-based combination <docs/papers/model-based-theory-combination.md>`.

The article is a tool overview, not a full algorithm listing. Its historical
benchmark plots do not provide evidence about Primo. Arrays, bit-vectors,
and the richer Yices type system are outside this survey's adoption scope.

## The 2016 talk: what lemma caching means

Consider a theory explanation such as

```text
a: x = y       b: y = z       c: x = z
a ∧ b ⇒ c      becomes       ¬a ∨ ¬b ∨ c
```

A theory solver can supply this clause when SAT asks why `c` was propagated.
Conflict analysis may use it as one step in resolution and ultimately learn a
different clause. Using the explanation once does not, by itself, require
storing that exact explanation as a watched SAT clause.

Printed slide 14 proposes retaining selected explanation/conflict clauses in
the SAT clause database. If a later branch sets `a` and `b` true again, the
stored clause can propagate `c` through ordinary Boolean propagation. That can
avoid repeating theory work. This is reuse *inside a solve or surviving solver
context*, not a cache of whole SMT input/output results on disk.

There are three distinct mechanisms:

| Mechanism | What survives and why |
| --- | --- |
| Lazy explanation, slide 11 | A compact reason handle preserves enough information to construct a clause only if it is requested. This saves work on unused explanations. |
| Callback reason storage | The propagator keeps the data needed to answer a pending reason request correctly. Storing a C++ vector does not make it a SAT clause. |
| SAT lemma retention, slide 14 | The Boolean engine keeps a selected theory clause with its normal propagation machinery, allowing reuse on later assignments. |

### Why cache only some lemmas?

Every retained clause costs storage and propagation work. Yices' slide gives
two filters: a configurable upper bound on clause length, and the ability to
install two suitable watches without backtracking just to insert the cached
lemma. The latter concerns optional reuse; it does not license ignoring a
theory conflict that must be reported.

Slides 12–13 explain why insertion during search needs care: the clause's
literals may already be assigned at different decision levels. The stated
watch preference ranks true before unassigned before false literals, preferring
lower levels among true literals and higher levels among false ones. Two
selected non-false literals provide the straightforward watchable case;
unit/conflicting cases may require propagation or backtracking. The slides
are an engineering sketch, not a complete insertion algorithm covering every
case. In Primo, CaDiCaL owns that algorithm.

The talk supplies neither a numerical size threshold nor a cache eviction,
deduplication, or lifetime specification. It gives no asymptotic speedup claim.
Our expected tradeoff is less repeated theory reasoning versus more clause
memory and Boolean work; it needs measurement on Primo.

### What Primo and its backend already do

**Implemented interface/storage; selective Yices policy not established.**
{source}`OnlineEufPropagator::store_reason
<src/smt/solver_engines.cpp#L2262-L2308>` validates a clause against the current
assignment and retains its antecedents. Its {source}`reason callback
<src/smt/solver_engines.cpp#L1925-L1950>` streams the propagated literal and
antecedents when requested. The {source}`LRA reason callback
<src/smt/solver_engines.cpp#L7265-L7297>` similarly streams stored reasons.
Much of this explanation data is already constructed before the callback;
having a callback is not proof of the fully deferred construction on slide 11.

The {source}`CaDiCaL adapter <src/sat/cadical_backend.cpp#L35-L85>` forwards
these callbacks. The installed CaDiCaL 3.0.0 public API,
`contrib/local/include/cadical.hpp`, documents learning external reason
clauses; see its upstream
[reason and external-clause contracts](https://github.com/arminbiere/cadical/blob/rel-3.0.0/src/cadical.hpp#L1248-L1367).
Primo's adapter does not set `are_reasons_forgettable`. The inspected header
initializes that field to false and describes non-forgettable reasons near
the reason callback, but later contains a conflicting statement about the
default and a `reason_forgettable` parameter. Measure the effective backend
configuration before claiming exactly when reasons can be deleted.

For explicit external conflicts, the {source}`EUF callback
<src/smt/solver_engines.cpp#L1952-L1976>` sets `is_forgettable = false` in the
API variant that carries this flag. Thus we must not describe Primo as
discarding all theory clauses, nor add a second cache without establishing
what the backend already keeps. The missing investigation is selection,
retention cost, and deletion safety, not merely the existence of a clause store.

## Other ideas retained from the talk

| Slides | Idea and data/invariant | Primo disposition |
| --- | --- | --- |
| 2–4 | Shared term tables and separately allocated descriptors, addressed through integer IDs. | **Partially matching concept; layout redesign deferred.** Primo interns terms, but {source}`Term <src/core/term.hpp#L77-L85>` stores children and several payload fields in every record. Compare descriptors as one compact-storage candidate; do not assume the Yices layout has already been adopted. |
| 5 | A polarity bit represents negation without allocating an explicit negation node. | **Not adopted at the core-term layer.** Primo has `TermKind::Not`; signed {source}`SAT literals <src/sat/lit.hpp>` are a different layer. |
| 6 | Small exact rationals fall back to GMP when needed. | **Related implemented idea, different representation.** {source}`Rational <src/util/rational.hpp>` uses an inline 64-bit path, rather than the slide's 32-bit pair. This survey does not change its recorded algorithmic provenance. |
| 10–11 | Phase restrictions on theory callbacks; deferred explanation expansion. | **Partial overlap.** Primo follows its IPASIR-UP contract, not Yices' callback API. Deferred reason streaming and deferred reason construction require separate accounting. |
| 15–22 | Boolean-aware classes, congruence representatives, and parent occurrence vectors. Per-argument hooks locate removable occurrences without searching an entire vector. | **Further representation audit deferred.** Compare against {source}`FlatCongruenceClosure <src/theory/equality/flat_congruence_closure.hpp>` and its own paper basis. This survey does not claim equivalence to Yices' signed circular class lists or hooked descriptors. A change would need rollback invariants and occurrence-maintenance counters. |
| 23–27 | Preserve useful ITE structure: auxiliary terms, finite domains from constant leaves, and flatter path-guard encodings can avoid expansion or unnecessary intermediate terms. | **Further mechanism audit deferred.** Compare the existing {source}`preprocessing <src/smt/preprocessing.cpp>` passes with each transformation before declaring a gap. Constant values must be provably distinct; uninterpreted constants do not automatically have distinct interpretations. Sharing, model reconstruction, and clause growth need explicit checks. |

## Open investigations and acceptance conditions

**Dynamic Ackermannization:** investigate a selector driven by congruence steps
in conflict explanations, separately from the existing assignment-indexed
propagator. Specify how absent equality atoms are created and registered, how
clauses are deduplicated and budgeted, and whether they can be forgotten while
the complete EUF checker remains responsible for consistency. Preserve the
n-ary disjunction and stable term/atom identities across allocation and scope
changes. Do not restart the rejected repeated-scan design without addressing
its cost.

**Lemma retention:** first count explanations built, requested, retained, and
reused, with clause sizes and memory over long incremental sessions. Audit
the effective CaDiCaL reason policy and distinguish explanation vectors from
backend clauses. Only then propose size/reuse-based selection or forgettability
controls supported by the adapter. A theory-valid clause includes its required
antecedents; consequences depending on scoped assertions must preserve those
dependencies or guards. A cache must never turn a branch-local fact into an
unconditional unit.

These are open leads, not active implementations or default changes. Before
implementation, complete an operation-by-operation design note, including
data owners, callback phases, lifetime/rollback rules, complexity expectations,
and proof obligations. Tests must check missing atoms, n-ary reasoning,
backtracking, repeated checks, scope retirement, and any deletion/rederivation
policy. Counters must distinguish useful reuse from candidate scans, duplicate
clauses, and retained memory. Any solver change needs the applicable fresh
fuzz/model gates and a reproducible baseline comparison in the run ledger.
