# Quantifiers: binders and instantiation

Primo handles quantified formulas in logic `UF` by generating instances and
sending them back to SAT and EUF. It uses enumerative instantiation: candidate
terms are grouped by sort, and tuples of those terms replace bound variables.
It does not currently use E-matching or model-based quantifier instantiation.

{source}`solve_quantified_uf <src/smt/quantifiers/quantified_solver.cpp#L97-L157>`
configures the [general component driver](combination.md#the-general-component-driver)
with EUF and a `QuantifierComponent`. The latter owns quantified atoms and
produces formulas; the SMT layer prepares and clausifies those formulas.
The quantifier component has no shared equality interface and does not inspect
EUF's congruence classes. This route uses the driver's lifecycle and lemma
queue, without model-guided arrangement search.

## Supported scope and answers

The {source}`routing gate <src/smt/solver_session.cpp#L624-L649>` sends asserted
formulas containing quantifiers to this route before ordinary ground
preprocessing. They are supported only under declared logic `UF`. The route
requires the IPASIR backend and does not export quantified models.

| Result | Current condition |
| --- | --- |
| `unsat` | SAT and EUF refute the abstraction together with generated instances and witness constraints. |
| `sat` | The ground candidate is consistent and every assigned quantifier atom behaves existentially and is covered by its Skolemization. No universal obligation remains. |
| `unknown` | Universal obligations remain without a new instance, the instance budget is reached, or solving is cancelled or otherwise resource-limited. |
| Unsupported | Quantified assertions occur outside the supported logic, the required backend is absent, or a quantified model is requested. |

The {source}`final check <src/theory/quantifiers/quantifier_component.cpp#L106-L122>`
is deliberately conservative. Exhausting a finite pool, including the two
Boolean values, does not by itself authorize `sat`. The implementation lacks
the entailment-based completion argument needed to accept general universal
formulas. This is also why a quantifier-free SAT assignment is only a candidate.

## A first refutation

```lisp
(set-logic UF)
(declare-sort U 0)
(declare-fun a () U)
(declare-fun p (U) Bool)
(assert (forall ((x U)) (p x)))
(assert (not (p a)))
(check-sat)
```

Let `Q` be the SAT atom for `forall x. p(x)`. Initially SAT can satisfy
`Q` and `not p(a)` because it does not inspect the quantified body. The
quantifier component finds `a` in the `U` term pool, instantiates the body,
and queues `not Q or p(a)`. Once the driver installs that formula, the next
SAT solve finds the contradiction. The quantified atom guards its instance:
an instance of an unasserted universal cannot become an unconditional fact.

The {source}`single-instance regressions <tests/unit/quantifier_solver_tests.cpp#L218-L224>`
exercise this path, including equality reasoning in the ground component.

## Representation and ownership

All syntax lives in the session's `core::TermManager`. Components store
`TermId`s; they do not own or copy term nodes. New instances, witnesses, and
lemmas are constructed through that manager.

| State | Representation and lifetime |
| --- | --- |
| Bound occurrence | `BoundVariable` stores its sort and a de Bruijn index in `Term::symbol`. |
| Quantifier | `Forall` or `Exists`, of sort Bool. `symbol` stores the number of bound variables; children are declaration leaves in source order, then the body and any patterns. |
| Per-sort candidates | `QuantifierComponent::pools_` stores append-only vectors of term IDs for one solve. |
| Enumeration | A state per `(quantifier atom, assigned value)` stores tried tuples and resumable ordering state. |
| Redundancy records | Raw-instance and normalized-instance caches include the atom, polarity, and instance ID. A separate set records queued lemma IDs. |
| Pending output | A deque of formulas belongs to the component until `take_lemmas` hands them to the driver. |
| Assignment truth | `ComponentAssignmentView` reads the shared SAT trail. Enumeration records do not duplicate SAT truth. |

The concrete owners are
{source}`TermManager::mk_quantifier <src/core/term_manager.cpp#L426-L455>` and
{source}`QuantifierComponent <src/theory/quantifiers/quantifier_component.hpp#L109-L179>`.
Enumeration, witness, and lemma caches survive SAT backtracking: retained
formulas are guarded and witnesses belong to their recorded atom and polarity.
They are not a cross-query instantiation cache. The default session rebuilds
solving state for each query.
Destroying the component does not reclaim its generated terms from the
manager; they remain subject to the [core lifetime rules](core.md#allocation-and-borrowed-references).

### What a de Bruijn index means

```{admonition} TODO: Explain why indexed variables were chosen
:class: note

Summarize the design tradeoff against named variables and variables with
identity: sharing under renaming, where capture-avoidance obligations live,
and parser/core complexity. Revisit the
{source}`recorded choice <docs/quantifiers.md#L871-L899>` against the implemented
operations; keep abandoned proposals and prototype measurements historical.
```

Index 0 names the innermost bound variable, index 1 the next outer variable,
and so on. A list of declarations counts as nested binders in declaration
order. Thus the body of

```lisp
(forall ((x U) (y U)) (exists ((z U)) (r x y z)))
```

contains `r(%2, %1, %0)`, where `%i` is explanatory notation for an index,
not SMT-LIB syntax. The names `x`, `y`, and `z` do not enter the term key.
Renaming those binders preserves the representation; sorts, bodies, and
patterns still distinguish nodes. The [core chapter](core.md#binders-and-cached-facts)
describes the cached `loose_index` and `contains_quantifier` facts. A closed
term has no index referring outside its own binders; it may still contain
quantifiers.

{source}`dispatch_quantifier <src/smt/frontend.cpp#L798-L876>` translates
lexical names into indices. Inner declarations shadow outer names; the last
occurrence of a repeated name in one declaration list wins, as specified by
SMT-LIB §3.6.1. `:pattern` annotations are retained as `Pattern` nodes, but
the current instantiator does not use them as matching triggers.

### Substitution without variable capture

An open term can change its indices when moved under another binder.
{source}`shift <src/core/binders.cpp#L192-L207>` adjusts those outer references;
closed terms need no change. For example:

```lisp
(forall ((x U))
  (let ((t x))
    (forall ((x U)) (r t x))))
```

The inner body is `r(%1, %0)`. The `let` value refers to the outer `x`, so
moving it under the inner binder must shift its index from 0 to 1. Otherwise
the substitution would silently change the formula to `r(%0, %0)`.

{source}`instantiate <src/core/binders.cpp#L209-L237>` opens one quantifier.
It checks replacement count and sorts, takes replacements in declaration
order, substitutes the removed variables, and lowers indices that referred
past that quantifier. Replacements are shifted when carried under binders
inside the body. Instantiating the outer `x` in `forall x. exists y. r(x,y)`
with a constant `a` therefore produces `exists y. r(a,y)`. The removed
quantifier's patterns are not part of the returned body.

{source}`rewrite_loose <src/core/binders.cpp#L86-L153>` uses an explicit stack
and memoizes by both term ID and binder depth. The same shared subterm can
mean different things at different depths, so a term-only memo would be
incorrect. It snapshots node data before constructing terms that can grow
the manager's storage. Its work depends on visited `(term, depth)` pairs,
not just distinct term IDs. `check_bound_variables` independently checks
scope and sorts; the generic `core::Substitution` scaffold is not this API.

## Universal instances and existential witnesses

SAT's assignment determines how a quantifier behaves: the component classifies
the pair **(quantifier kind, assigned truth value)**. Here `Q` denotes the
quantified formula itself, without any surrounding negation. Write `B[t]` for
its body instantiated with a tuple `t`, and `k` for fresh witness constants.

If `Q` has SAT literal `q`, the {source}`clausifier
<src/smt/clausifier.cpp#L335-L340>` encodes `not Q` as `not q`.
Asserting `not Q` therefore assigns `Q` false. The quantifier component then
applies the false row below, implementing
the familiar dualities:

```text
not (forall x. B(x))   ≡   exists x. not B(x)
not (exists x. B(x))   ≡   forall x. not B(x)
```

The negation changes both the effective quantifier and the required truth of
its body. This handling does not require rewriting the stored `Forall` node
into an `Exists` node, or vice versa.

| Quantified atom `Q` | SAT value | Work required | Formula installed |
| --- | --- | --- | --- |
| `forall x. B(x)` | true | Enumerate candidate tuples | `not Q or B[t]` |
| `forall x. B(x)` | false | Choose witnesses to its negation | `Q or not B[k]` |
| `exists x. B(x)` | true | Choose witnesses | `not Q or B[k]` |
| `exists x. B(x)` | false | Enumerate candidate tuples | `Q or not B[t]` |

The {source}`classification <src/theory/quantifiers/quantifier_component.cpp#L98-L108>`
and {source}`formula construction <src/theory/quantifiers/quantifier_component.cpp#L260-L286>`
implement these four cases. Universal instances are logical consequences.
Witness constraints instead extend the signature with fresh constants while
preserving satisfiability: `exists x. p(x)` does not imply `p(k)` for an
arbitrary existing constant. `skolemize` creates witnesses once per atom and
existential polarity, and retains them across backtracking.

### Following a negated quantifier

For an assertion `not (forall x. p(x))`, let `Q` stand for `forall x. p(x)`.
SAT must assign `Q = false`. The component creates a fresh constant `k` and
installs `Q or not p(k)`. Under that assignment the formula requires
`not p(k)`: `k` is the counterexample witnessing why the universal is false.

For `not (exists x. p(x))`, now let `Q` stand for `exists x. p(x)`.
That atom is also false, but the obligation is universal.
For each candidate term `t`, the component installs
`Q or not p(t)`. An additional assertion `p(a)` is contradicted by the instance
at `a`. Exhausting the current candidates still does not establish this
universal obligation, so the usual `unknown` boundary applies.
The {source}`polarity regressions <tests/unit/quantifier_solver_tests.cpp#L226-L236>`
cover both negated forms.

Within a larger Boolean formula, an occurrence under `not` need not force the
atom false: `not Q or s` also permits `Q = true, s = true`. The component uses
the current SAT value, not a once-for-all classification of the occurrence.
If backtracking changes that value, it changes the obligation. The guards make
previously generated formulas conditional: `Q or not B[k]`, for example, imposes no
condition on the witness when `Q` becomes true.

The current component also processes assignments that the Boolean formula
does not need. For `X or Q` with `X = true`, that assertion imposes no
condition on `Q`, yet Primo still handles its assigned polarity. An unnecessary
universal obligation can therefore leave the final answer `unknown`.
[Relevance filtering](../leads/quantifier-relevance.md) is an open lead:
it would require support for every active assertion and every required nested
obligation before omitting an atom from generation or final checking.

### Nested obligations

Primo does not first convert the whole input to prenex Skolem normal form.
For `forall x. exists y. r(x,y)`, an outer instance at `a` produces a new
atom `exists y. r(a,y)`. If assigned true, that atom gets its own constant
`k_a` and witness constraint. Another outer instance can introduce a different
atom and witness. The current component uses constants for these already
instantiated obligations; it does not construct a global Skolem function
of `x`. Nested quantified bodies become further atoms as their containing
instances are installed.

The same process carries negation through nesting. For
`not (forall x. exists y. r(x,y))`, the false outer universal gets a witness
`k` and requires `not (exists y. r(k,y))`. That inner existential becomes an
atom assigned false, so it generates `not r(k,t)` obligations for candidate
terms `t`, each guarded by the inner atom. This realizes
`exists x. forall y. not r(x,y)` one instantiated level at a time.

## When instantiation runs

The route's {source}`recipe <src/smt/quantifiers/quantified_solver.cpp#L134-L153>`
checks EUF before the quantifier component and provides preparation hooks for
new lemmas. One cycle proceeds as follows:

1. SAT searches with the currently installed clauses. The quantifier component
   declares `check_on_full_model`, so the
   {source}`dispatcher <src/smt/component_dispatcher.cpp#L132-L174>` waits for
   a complete SAT candidate before invoking its instance-generation check.
   Assigning only the quantifier atoms is insufficient to trigger enumeration.
2. {source}`QuantifierComponent::check <src/theory/quantifiers/quantifier_component.cpp#L74-L160>`
   first reports any existing queue. Otherwise it classifies the assigned
   quantifiers and creates missing witness constraints. If those queue work,
   it hands control back before scanning universal tuples.
3. With no witness formulas waiting, it enumerates universal obligations.
   The per-round allowance is divided among them, with a rotating starting
   obligation. Queued formulas produce `LemmasPending`.
4. The {source}`outer driver <src/smt/component_solver.cpp#L310-L334>` drains
   a bounded batch, detaches the dispatcher, prepares and clausifies the
   formulas, registers new atoms, and reconnects assignment routing. SAT
   then solves again. The component and its enumeration records persist.
5. If no formulas remain to install and ground checks succeed, the coordinator
   asks for `FullModel`. That check generates no new instances: it decides
   whether the narrow `sat` condition holds or returns an inconclusive result.

The name `Partial` in the generation API describes check effort. The dispatcher
still invokes that check at a complete SAT candidate for this component.
It must not repeatedly interrupt ground search as soon as quantified root
units are assigned; the
{source}`complete-assignment regression <tests/unit/quantifier_solver_tests.cpp#L394-L424>`
covers this scheduling requirement.

### Preparing generated terms

An instance may introduce `ite`, a compound Boolean function argument, or a
fresh Boolean witness. These require the same preparation as input terms.
The recipe's
{source}`name_boolean_arguments <src/smt/quantifiers/quantified_solver.cpp#L20-L93>`
and `lower_extended_terms` prepare newly ground formulas while leaving nested
quantifiers opaque. `prepare_boolean` completes Boolean argument registration
at every atom-installation boundary. EUF must keep a Boolean witness within
the two Boolean values; treating it as an unconstrained third value is unsound.

The {source}`generated-Boolean regressions <tests/unit/quantifier_solver_tests.cpp#L31-L101>`
cover that failure and repeated preparation. The route also disables the
pure-theory assignment filter: new lemmas can introduce a previously absent
polarity of an existing atom, invalidating a filter based on earlier occurrences.

## Candidate terms and growing pools

{source}`add_pool_terms <src/theory/quantifiers/quantifier_component.cpp#L178-L195>`
walks the prepared assertions and later the formulas handed over by
`take_lemmas`. It collects closed, non-Boolean constants and applications,
visiting each term ID once. Quantifiers and pattern nodes stop the traversal;
their open bodies do not supply ground candidates. The pool is syntactic:
EUF-equivalent terms are not merged.
The {ref}`open lead on enumeration modulo current equalities
<quantifier-equality-enumeration>` asks whether representatives can reduce
this pool safely, and records the completeness and revisitation obligations
when the assignment or candidate model changes.
It connects this reduction to {ref}`filtering enumerated instances against the
current model <quantifier-model-refinement>`, distinguishing that policy from
filtering instances already entailed by the ground assignment.
The connected {ref}`fail-mask lead <quantifier-fail-masks>` generalizes a
justified rejection to a family of tuples and tracks when that justification
must be invalidated.

Each sort has one append-only candidate sequence, shared by every quantified
variable of that sort. Bool always uses `[false, true]`. When another sort has
no candidate, {source}`pool <src/theory/quantifiers/quantifier_component.cpp#L198-L206>`
creates one fresh constant, using SMT-LIB's nonempty-domain semantics.
That fallback constant can later be discovered by the walk and appear again
in the vector. Candidate vectors are therefore not sets; the tried-tuple
cache compares term IDs, so repeated positions do not repeat instantiation.

Terms can appear only after earlier instantiations. For example, with an
initial candidate `a`, instantiating `forall x. p(f(x))` produces `p(f(a))`.
Once that lemma is handed over, `f(a)` joins the pool. It can then instantiate
`forall y. not p(y)`, completing a refutation. The
{source}`growing-pool regression <tests/unit/quantifier_solver_tests.cpp#L246-L251>`
checks this mechanism. Primo does not generate the entire Herbrand universe
up front.

The completeness basis is Reynolds, Barbosa and Fontaine, TACAS 2018, §3,
Theorem 1 and Corollary 2 {cite:p}`reynolds2018enumerative`. For ground
literals and universally quantified clauses in first-order logic with equality,
with a ground term of each sort, Theorem 1 guarantees a finite refutation of
an unsatisfiable input using existing terms and terms generated by earlier
instances. Corollary 2 requires fair instance generation. We refer to the
paper for the argument.

Primo collects candidates from all prepared assertions and delivered lemmas,
handles nested quantifiers by their assigned polarity, and imposes resource
limits. These implementation choices require a separate argument before
claiming unbounded refutational completeness. The paper's Corollary 1 permits
`sat` when the ground assignment entails every relevant instance; exhausting
Primo's candidate pools does not check that condition. Its
[answer boundary](#supported-scope-and-answers) therefore remains conservative.

## Tuple orders and duplicate instances

For `n` quantified variables with pool sizes `m₁, …, mₙ`, the candidate
product contains `m₁⋯mₙ` tuples. An `IndexTuple` selects one position in each
variable's pool. The
{source}`tuple generators <src/theory/quantifiers/tuple_orders.hpp#L18-L113>`
implement these choices; `sum` is the default.

| Option | Enumeration order |
| --- | --- |
| `sum` | Increasing sum of indices; descending lexicographic order within a stage. Starts `(0,0), (1,0), (0,1), (2,0), (1,1), (0,2)`. |
| `max-colex` | Increasing maximum index; last position most significant within a stage. Starts `(0,0), (1,0), (0,1), (1,1)`. |
| `max` | Original maximum-index grouping. Starts `(0,0), (1,0), (1,1), (0,1)`, so it differs from `max-colex`. |
| `leximax` | Compare descending-sorted tuples, then enumerate their permutations. Starts `(0,0), (0,1), (1,0), (1,1), (0,2), (2,0)`. |
| `deepening` | Depth-first traversal with an increasing bound on index sum. |
| `random` | Seeded selection from a frontier of tuples; the stream also depends on quantifier ID and polarity. |

These adapt the orders in Janota et al., FMCAD 2021, §III. Every order needs
to revisit newly available tuples when pools grow. Staged orders retain the
first unfinished or pool-clipped stage; the walk orders retain traversal
state. {source}`enumerate <src/theory/quantifiers/quantifier_component.cpp#L288-L375>`
snapshots the candidate vectors for one scan and records each tried tuple.
Coverage of a fixed finite product is tested separately from fairness over
growing pools. In the paper's unbounded fair procedure, no required instance
is postponed forever; balanced treatment of tuple positions alone does not
establish that property. The completeness result is the cited Corollary 2,
subject to its assumptions, rather than a guarantee for every selectable
order or Primo's bounded run.

```{admonition} TODO: Document the evidence for the default tuple order
:class: note

Extract the rationale for the `sum` default from the
{source}`enumeration note <docs/papers/fair-adventurous-enumeration.md>`:
give the dated Primo comparison, tested configurations and qualification
limits. The cited enumeration results explain the method; they do not by
themselves justify Primo's default.
```

{source}`add_instance <src/theory/quantifiers/quantifier_component.cpp#L212-L246>`
then applies three filters: the tuple's instantiated body may already have
been seen; its normalized form may duplicate another instance; or its guarded
formula may be trivial or already queued. Cache keys include the quantified
atom and its polarity.

{source}`InstanceSimplifier <src/theory/quantifiers/instance_simplifier.cpp>`
performs equivalence-preserving Boolean normalization: it flattens and orders
conjunctions/disjunctions, removes duplicates and complementary operands,
canonicalizes equalities, and simplifies conditionals. Quantifiers remain
opaque. For `forall x,y. p(x) or p(y)`, tuples `(a,b)` and `(b,a)` can therefore
yield the same normalized instance. This simplifier does not ask whether EUF
or the current SAT assignment entails that instance. Fail masks, E-matching,
relevant-domain restrictions, and model-based instantiation are absent.

## Budgets, costs, and observability

The {source}`solver options <src/smt/check_sat.hpp#L471-L487>` and shared
{source}`defaults <src/theory/quantifiers/tuple_orders.hpp#L43-L60>` distinguish
instance generation from queue delivery:

| Control | Default and meaning |
| --- | --- |
| `--quantifier-instances-per-round` | 128 newly queued enumerative instances across universal obligations. |
| `--quantifier-max-instances` | 100,000 distinct enumerated instances per solve. Duplicate bodies do not spend it; distinct instances that simplify to a trivial value can still count. |
| `--quantifier-stall-tuples` | 65,536 tuple visits without another queued formula, once a formula is already waiting. Zero disables this yield. |
| `--quantifier-tuple-order` | `sum`. |
| `--quantifier-deepening-increment` | 2. |
| `--quantifier-random-seed` | 0, mixed with the atom and polarity. |
| `CombinationRecipe::lemma_batch` | 64 formulas drained per driver iteration. This is an internal queue limit, not a requirement to fill the queue. |

The stall cutoff prevents a useful instance from waiting behind a long
redundant scan. With 26 unused Boolean variables, a body independent of those
variables has `2^26` candidate tuples but only one distinct instance. Once
that formula is queued, the cutoff yields so SAT can use it. A yield preserves
enumeration state; it neither skips an untried tuple nor establishes `sat` or
`unknown` by itself.

The cutoff does not bound the search for the first useful formula, and the
inner enumeration scan has no cancellation check. Pools, tried tuples, and
memoized terms also consume memory as the solve progresses. The product size
and repeated traversal costs remain relevant even when very few lemmas are
eventually delivered.

| Counter | What it distinguishes |
| --- | --- |
| `:quant.instances` | Previously untried tuples instantiated; this is not the distinct-instance budget counter. |
| `:quant.instances.raw-duplicates`, `.simplified-duplicates`, `.trivial` | Work discarded before useful lemma delivery. |
| `:quant.pool-terms` | Non-Boolean terms discovered by the pool walk. |
| `:quant.rounds`, `:quant.stalls` | Checks returning queued work and scans yielding under the stall cutoff. |
| `:quant.skolem-lemmas`, `:quant.lemmas` | Queued witness formulas and all formulas handed to the driver. |
| `:combination.lemmas.batches`, `.formulas` | Actual installation batches and their formula counts. |
| `:quant.exhausted` | Final checks with unresolved universal obligations and no new instance. |

## Sources and implementation status

| Primary source | Role in Primo |
| --- | --- |
| Barrett, Fontaine and Tinelli, [*The SMT-LIB Standard, version 2.6, revision 2021-05-12*](https://smt-lib.org/papers/smt-lib-reference-v2.6-r2021-05-12.pdf), §3.6 | Sorted binders, lexical scope, sequential quantifier declarations, and parallel `let` bindings. The frontend implements these scope rules within its admitted fragment. |
| Arthur Charguéraud, [*The Locally Nameless Representation*](https://www.chargueraud.org/research/2009/ln/main.pdf), JAR, §§2.3 and 3.3 | Explanation of de Bruijn indices, shifting, and local closure. Primo uses indexed bound variables; it does not implement the paper's Coq development. |
| Reynolds, Barbosa and Fontaine, [*Revisiting Enumerative Instantiation*](https://hanielbarbosa.com/papers/tacas2018.pdf), TACAS 2018, §§2–4.1, Theorem 1 and Figure 2 | Enumerative instance loop and growth from existing terms. Primo uses guarded instances and syntactic caches, with the pool, Skolemization, entailment, and answer limitations described above. |
| Ge and de Moura, *Complete Instantiation for Quantified Formulas in Satisfiability Modulo Theories*, CAV 2009, §6 {cite:p}`ge2009complete` | Primary source for MBQI. This supplies a comparison with the proposed model-based filter on enumerated candidates; neither that filter nor MBQI is implemented. |
| Janota, Barbosa, Fontaine and Reynolds, [*Fair and Adventurous Enumeration of Quantifier Instantiations*](https://people.ciirc.cvut.cz/~janotmik/fmcad21.pdf), FMCAD 2021, §§III–IV | Tuple orders and duplicate filtering. The original `max` order is a Primo variant; entailment filtering and fail masks are unimplemented. |
| Andrew Reynolds, [*Finite Model Finding in Satisfiability Modulo Theories*](https://homepage.divms.uiowa.edu/~ajreynol/thesis.pdf), PhD thesis, University of Iowa, 2013, §§3.1.1–3.1.2 | Context for pattern-based versus complete instantiation and the distinction between finding refutations and accepting quantified models. Its finite-model-finding algorithms are not implemented here. |
| Haniel Barbosa, [*New Techniques for Instantiation and Proof Production in SMT Solving*](https://hanielbarbosa.com/papers/phd.pdf), PhD thesis, 2017, §5.1 and §§7.1.1–7.1.2 | Hash-consed terms and capture-safe formula processing. Its named-variable renaming and contextual Skolemization are comparison points; Primo uses indices and assignment-driven witness clauses, without that proof-production framework. |

The {source}`original binder design <docs/quantifiers.md>`,
{source}`enumerative-instantiation checklist <docs/papers/enumerative-instantiation.md>`,
and {source}`tuple-order checklist <docs/papers/fair-adventurous-enumeration.md>`
retain earlier decisions and paper mappings. Their historical proposals and
default descriptions must be read with their dates; this chapter describes
the current code.

## Invariants exercised by existing tests

| Obligation | Regression or independent check |
| --- | --- |
| Names, shadowing, and expanded `let` values preserve binding | {source}`frontend binder tests <tests/unit/quantifier_frontend_tests.cpp#L73-L139>` |
| Shifting and instantiation avoid capture, including across nested binders | {source}`operation examples <tests/unit/binder_tests.cpp#L134-L212>`; {source}`independent named-variable oracle <tests/unit/binder_tests.cpp#L453-L516>` |
| Deep nesting does not require recursive C++ traversal | {source}`deep binder test <tests/unit/binder_tests.cpp#L518-L539>` |
| Both existential polarities and nested obligations are handled | {source}`witness and nesting tests <tests/unit/quantifier_solver_tests.cpp#L226-L244>` |
| Later instances can use terms introduced by earlier ones | {source}`pool-growth test <tests/unit/quantifier_solver_tests.cpp#L246-L251>` |
| Generated Boolean terms receive the correct two-element domain | {source}`preparation tests <tests/unit/quantifier_solver_tests.cpp#L31-L101>` |
| Exhaustion, budgets, and model requests have distinct outcomes | {source}`answer-boundary tests <tests/unit/quantifier_solver_tests.cpp#L284-L316>` |
| Every tuple in tested finite products remains reachable after pool growth | {source}`tuple-order tests <tests/unit/tuple_order_tests.cpp#L115-L207>` |
| Normalization preserves Boolean meaning and reduces duplicate instances | {source}`semantic simplifier check <tests/unit/instance_simplifier_tests.cpp#L232-L293>`; {source}`instance-cache tests <tests/unit/quantifier_solver_tests.cpp#L318-L345>` |
| Stalls deliver useful work and resume without losing tuples | {source}`stall tests <tests/unit/quantifier_solver_tests.cpp#L103-L147>`; {source}`resume tests <tests/unit/quantifier_solver_tests.cpp#L181-L216>` |
| Ground search reaches a complete assignment before enumeration interrupts it | {source}`scheduling regression <tests/unit/quantifier_solver_tests.cpp#L394-L424>` |

## Evidence and external checking

The historical {source}`quantifier record <docs/quantifiers.md>` and
{source}`run log <docs/run-log.json>` retain generated-term correctness gates,
UF corpus evaluations, tuple-order comparisons, and the measurements behind
the current round and stall defaults. Those records qualify their named
revisions; this documentation rewrite does not rerun or extend them.

```{admonition} TODO: Extract the quantifier evidence and checking argument
:class: note

Replace broad historical pointers with a curated evidence summary for tuple
orders, instance-round and stall defaults, correctness repairs, and remaining
unconfirmed answers. Preserve exact revisions, configurations, and run IDs.
Explain why the pool-guided cvc5 check preserves satisfiability despite using
Primo-generated terms and witness information, including top-level linking,
skipped exports, and negative controls. Verify the trust argument in the
{source}`original checker discussion <docs/quantifiers.md#L88-L149>` against
the script; a reference solver invocation alone is not that argument.
```

`--quantifier-export-pools FILE` records printable witness constants, witness
constraints, and tuples that produced lemmas through
{source}`write_pool_export <src/smt/quantifiers/pool_export.cpp>`.
{source}`quantifier_pool_check.py <scripts/quantifier_pool_check.py>` uses this
information to guide a separate cvc5 solve with pools, including explicit
handling of top-level Skolemizations. It can confirm a refutation even when
an unguided reference run is inconclusive. It is a separate solver check,
not a Primo proof certificate or quantified-model validation. Exported items
may be skipped when they contain unsupported internal symbols; the export
counters and checker outcome must be retained. A timeout or `unknown` is
inconclusive. The script also supports negative controls on inputs Primo did
not refute.

## Related open leads

- [Quantifier instantiation efficiency](../leads/quantifier-instantiation.md)
- [Optional NNF through quantifiers](../leads/quantifier-nnf.md): compare
  normalization while retaining support for general formulas and both SAT values.
- [Exploiting directly asserted quantifiers](../leads/asserted-quantifiers.md):
  use fixed root obligations to investigate earlier witnesses and less bookkeeping.
- [Skipping irrelevant quantified obligations](../leads/quantifier-relevance.md):
  avoid work on don't-care atoms while preserving a sufficient support for the input.
