# Theory combination

Primo combines uninterpreted functions with arithmetic by making their solvers
agree on the equalities between shared terms. Each solver keeps its own
representation and checks its own constraints. The SMT layer purifies mixed
terms, communicates explained consequences, and coordinates candidate models.
The [DPLL(T) chapter](dpllt.md) describes the surrounding Boolean search;
[EUF](euf.md) and [LRA](lra.md) describe the participating solvers.

There are two production routes:

| Fragment | Implementation |
| --- | --- |
| `QF_UFLRA` | {source}`solve_qf_uflra_engine_impl <src/smt/solver_engines.cpp#L10801-L10847>` uses a dedicated EUF/LRA coordinator, explained equality exchange, model reconciliation, and exact fallback checks. |
| `QF_UFLIA` | {source}`solve_qf_uflia_engine_impl <src/smt/solver_engines.cpp#L10245-L10278>` supplies EUF and integer-arithmetic components to {source}`solve_component_recipe <src/smt/component_solver.cpp>`. Model guidance creates reversible equality choices; complete arrangements provide the fallback. Integer search remains bounded and can return `unknown`. |

## Why the solvers must communicate

Consider these constraints over Real constants `x`, `y` and an uninterpreted
function `f`:

```text
x <= y
y <= x
f(x) != f(y)
```

Arithmetic can satisfy its two inequalities by setting `x = y = 0`. EUF can
satisfy its disequality by giving `x` and `y` different interpretations. Those
two answers do not establish satisfiability of the combined problem.

The arithmetic constraints imply `x = y`. Once EUF receives that equality,
congruence gives `f(x) = f(y)`, contradicting the last constraint. Only the
equality crosses the boundary: EUF does not need the arithmetic inequalities
or tableau rows.

Let `a`, `b`, and `c` denote the true SAT literals selecting the three
constraints, and let `e` name `x = y`. An explained exchange can produce

```text
not a or not b or e       arithmetic explanation
not e or not c            EUF explanation
```

Resolving on `e` gives `not a or not b or not c`. An internal exchange can
compose the explanations without creating a SAT variable for `e`. Either way,
the conflict depends on the selected constraints; it must remain valid when
SAT backtracks or chooses another branch.

## Purification and the shared interface

A theory atom must be understandable by its owner. For example,
`f(x) + 1 <= y` contains both an uninterpreted application and arithmetic.
{source}`purify_mixed_arithmetic_applications <src/smt/solver_engines.cpp#L6765-L6795>`
introduces a fresh constant `p`, gives EUF `p = f(x)`, and gives arithmetic
`p + 1 <= y`.

In the other direction, `P(x + 1)` becomes `P(q)`, with the arithmetic
definition expressed by `q <= x + 1` and `x + 1 <= q`. The
{source}`argument purification code <src/smt/solver_engines.cpp#L6567-L6603>`
also records the original-term/proxy relationship for model reconstruction.
The transformation handles supported Real and Int terms; some helper and
counter names retain the earlier Real-only terminology.

These definitions belong to the transformed query. They are assertions, not
temporary model choices. Memoization by `TermId` reuses proxies and rewritten
subterms. Fresh symbols and terms are created through the solver's purification
scope and term manager, and the conversion record preserves how to interpret
the original expressions after solving.

For the dedicated Real route, shared terms are constants occurring in both
the EUF part and the normalized arithmetic part. An occurrence whose arithmetic
coefficient cancels to zero does not make a constant shared. Constants used by
only one theory remain local. Boolean values are handled through SAT and the
[Boolean argument encoding](sat.md), rather than by an arrangement over an
infinite shared domain.

## The combination conditions

Nelson–Oppen combination applies to purified theories with disjoint
nonlogical signatures, apart from equality and shared constants. Its usual
stable-infiniteness condition means that every satisfiable quantifier-free
constraint set has a model with an infinite domain. This leaves enough values
to realize the required disequalities between shared constants. The
[source mapping](#sources-and-implementation-status) gives both the original
account and the modern formulation used here.

An **arrangement** specifies which shared constants are equal and which are
different. It can be described as a partition into equality classes. Under the
combination conditions, the component models can be combined when they agree
on that partition.

**Convexity** determines whether deducing individual equalities is sufficient.
A theory is convex if a conjunction implying a disjunction of equalities must
imply at least one of those equalities individually. EUF and rational linear
arithmetic have this property. With complete equality deduction, repeated
exchange reaches either a conflict or an equality fixed point sufficient for
the convex combination.

For a fixed interface of `n` constants, each independent new equality merges
two previously distinct classes. At most `n - 1` such merges are possible.
This bounds progress of the convex closure; it does not bound the cost of
finding an equality or the surrounding Boolean search.

Integer arithmetic is not convex. With `0 <= x <= 1`, `y = 0`, and `z = 1`,
it implies `x = y or x = z`, but neither equality alone. Adding
`f(x) != f(y)` and `f(x) != f(z)` makes the combined problem unsatisfiable.
Waiting for one individually entailed equality would miss this contradiction.
Primo's integer route lets SAT explore interface equalities; the
{source}`non-convex arrangement regression <tests/unit/qf_uflia_solver_tests.cpp#L172-L221>`
exercises this example.

## Representation and ownership

The inputs are purified core terms, their Boolean abstraction, and assigned
theory literals. Outputs are explained clauses, candidate equality decisions,
compatible component models, or a typed inconclusive result.

| Representation | Owner and lifetime |
| --- | --- |
| {source}`InterfaceEqualityRegistryEntry <src/smt/theory_combination.hpp#L62-L71>` | A value in the Real route's `TheoryCombinationManager`. It records endpoints, sort, optional equality term/SAT literal, arrangement value, and classifications. |
| {source}`SharedModelState <src/smt/theory_combination.hpp#L121-L132>` | Owned by that manager for the current model round: value classes, explained equalities, accepted internal model equalities, and separation atoms. A new round or rollback invalidates it. |
| {source}`SharedEqualityDecisionState <src/smt/theory_combination.hpp#L138-L144>` | Solve-scoped pending, active, and tried equality choices. The engine applies them as SAT assumptions. |
| {source}`TheoryComponent <src/theory/theory_component.hpp#L90-L140>` | Each instance owns its prepared theory data and mutable solving state. `solve_component_recipe` owns instances through `unique_ptr`; coordinators borrow pointers while those instances live. |
| {source}`TheoryCombinationPlan <src/smt/theory_combination_plan.hpp>` | Owns validated descriptor copies, atom-owner mappings, and shared scopes for one solve. Component registration uses this plan before search. |
| {source}`TheoryModelProjection and TheoryModelToken <src/theory/theory_solver.hpp#L63-L85>` | Owning equality-class vectors plus a component identity, assignment epoch, and model revision. A projection describes a candidate model, not logical entailments. |
| {source}`ComponentModelFragment <src/theory/theory_component.hpp#L81-L86>` | An owning export of typed bindings, retained facts, and symbol interpretations for an accepted token. It is consumed by model assembly. |

Terms are referenced by stable `TermId`s. No component owns the term arena,
and the term manager must outlive uses of those IDs. Registration and
purification can allocate terms and grow vectors; callers must reacquire
borrowed nodes after term creation. A copied registry entry or projection
owns its metadata but does not extend the lifetime of the term manager.

The Real registry canonicalizes an unordered pair by its endpoint IDs and
stores entries sparsely. {source}`ensure_interface_pair <src/smt/theory_combination.cpp#L499-L521>`
checks that both terms belong to the same shared sort. Creating an entry does
not create a SAT variable. Materialization is a separate SMT operation.

SAT assignment truth has one owner at the callback boundary:
{source}`AssignmentCoordinator <src/smt/assignment_coordinator.hpp>`.
The Real manager and theory components read a `sat::TrailView`; the manager's
{source}`assignment query <src/smt/theory_combination.cpp#L625-L649>` reads that
view. Its separate undo log restores **combination arrangement values**.
Theories also retain the undo information needed for their own closure or
tableau operations. These are different responsibilities from recording SAT
assignments; see [assignment ownership](dpllt.md).

## Consequences and model choices

An equality can reach the coordinator for two different reasons:

| Evidence | Permitted action |
| --- | --- |
| The component constraints entail `x = y`, with an explanation | Propagate it, learn a guarded clause, or merge internally while retaining the explanation. |
| One satisfying component model happens to give `x` and `y` the same value | Try a compatible model change or make a reversible SAT decision. This coincidence supplies no theory lemma by itself. |

For example, `x >= 0` and `y >= 0` admit the model `x = y = 0`, but also
models with different values. A failed attempt to use that equality does not
prove `x != y`. A conflict derived under the corresponding SAT assumption can
justify a blocking clause using the failed assumptions.

The component result type makes the distinction explicit:
{source}`TheoryCheckResult <src/theory/theory_solver.hpp#L101-L127>` separates
`EntailedLiteral`, `LearnedClause`, and `FreshDecision`. For an entailed literal
`e`, the reasons are true antecedents `r₁, …, rₙ`; the
{source}`coordinator constructs <src/smt/component_model_coordinator.cpp#L97-L114>`
`not r₁ or … or not rₙ or e`. A learned clause already carries its signs.

## The dedicated Real route

### Explained equality exchange

EUF projects its shared congruence classes and emits a spanning set of
equalities for each class. A class of `k` terms needs `k - 1` such edges:
transitivity supplies the remaining equalities.

LRA's default opportunistic producer uses the `FixedEq`, `EqRow`,
`EqOffsetRows`, and `EqRows` mechanisms of de Moura and Bjørner, §5.1. Their
{source}`tableau implementation <src/theory/arithmetic/lra_tableau.cpp#L1078-L1259>`
retains bound explanations. These rules are sound but do not discover every
entailed equality. The alternative `model-unit` producer is described in
[LRA](lra.md#equalities-for-combination); it remains opt-in.

When the exact fallback is required,
{source}`lra_implied_real_equalities_for_partition <src/smt/solver_engines.cpp#L4907-L4947>`
restricts candidate pairs to equal-valued terms in a checked arithmetic model.
Different model values disprove entailment immediately. For an unresolved pair
with equal values, exact probes test the two alternatives `x < y` and `y < x`.
Only infeasibility of both proves equality. The implementation reuses prepared
arithmetic state and groups already established equalities to avoid redundant
probes; a probe limit produces `unknown`.

Explained equalities can be absorbed into EUF internally. Otherwise the engine
materializes an equality atom and learns the guarded propagation clause before
re-entering SAT. An equality selected false must also be respected: a feasible
arithmetic model that still identifies its endpoints is insufficient.

### Reconciling candidate models

Default QF_UFLRA model reconciliation first uses a feasible arithmetic model,
optionally diversified by freedom-interval updates. These updates change
values while preserving the tableau equations and bounds. They do not establish
new consequences. Required shared disequalities remain obligations throughout
model construction.

{source}`reconcile_lra_candidate_with_euf <src/smt/solver_engines.cpp#L5082-L5202>`
then tries a spanning presentation of the LRA value classes in the EUF closure:

1. Check that equalities already present in EUF agree with the arithmetic
   values of the shared terms.
2. Checkpoint EUF and try each candidate equality. A direct conflict rejects
   that edge.
3. Project the resulting EUF classes again. Congruence may have merged other
   shared terms; every such merge must agree with the arithmetic model too.
4. If an edge is rejected, undo the candidate merges and return canonical pairs
   for lazy SAT assumptions. If all edges succeed, retain the compatible
   equalities for this model round.

The third step is essential. Trying `x = y` also forces `f(x) = f(y)`.
Even if EUF has no explicit disequality between the applications, arithmetic
may already assign them different values. The
{source}`congruence-consequence regression <tests/unit/qf_lra_solver_tests.cpp#L2530-L2553>`
checks that this causes a split rather than a false `sat` answer.

The {source}`outer engine <src/smt/solver_engines.cpp#L11820-L12002>` accepts
successful shared-model compatibility, re-enters SAT with selected choices, or
uses the exact convex fallback. Some conflicts compose explained equalities
directly without making all pairs visible to SAT. Unexplained candidate merges
remain confined to model reconciliation.

## The component collection route

### Planning ownership and shared scopes

{source}`TheoryCombinationPlan <src/smt/theory_combination_plan.cpp#L50-L181>`
validates component identities, interpreted signatures, function-symbol
ownership, and the unique owner of each atom. Components declare their shared
sorts, convexity, and capabilities. An unsupported combination is reported as
unsupported; it is not a proof of unsatisfiability.

For each sort, the planner groups components connected by shared constants.
All participants in a connected group receive its normalized interface. A
component may therefore need to register a constant absent from its original
atoms. This prevents a chain of overlapping interfaces from losing a relation
needed elsewhere in the group. Disconnected groups remain separate.

The many-sorted contract requires stable infiniteness **simultaneously across
all shared sorts** of a component. Bool is excluded from these scopes. Each
group may have at most one component supplying authoritative values for a
shared sort. These normalization and admission rules are Primo's own design;
they are not claimed as a verbatim algorithm from the two-theory papers.

### Materializing an interface equality

For a pair `x`, `y`, a component's `encode_equality` returns an equivalent
Boolean formula built from terms it understands. EUF returns `x = y`.
{source}`UfliaLiaComponent <src/smt/solver_engines.cpp#L10115-L10168>` returns
`x <= y and y <= x`.

The {source}`SMT materializer <src/smt/component_solver.cpp#L126-L181>` links
the equality literal `e` to those two bounds, `a` and `b`:

```text
not e or a
not e or b
not a or not b or e
```

All three clauses are needed. If SAT selects `not e`, the last clause forces
at least one bound false, letting arithmetic enforce the disequality. The
component supplies the meaning of equality; the SMT layer owns clausification
and SAT calls.

New atoms are registered at a detached preparation boundary. The
{source}`installation path <src/smt/component_solver.cpp#L215-L271>` updates
the owning components, rebuilds callback routing, and observes the resulting
SAT variables before search resumes. The dispatcher routes assignment changes
through the shared trail view and preserves explanations across unrelated
backtracking.

### Care pairs: which equality questions matter?

A **care pair** `(x, y)` identifies two shared terms of the same sort whose
equality a component wants the coordinator to examine. The pair asks whether
`x = y`; it supplies neither an answer nor an explanation. A **care graph**
has shared terms as vertices and care pairs as edges.

For example, suppose `x`, `y`, and `z` are shared Int constants and the EUF
part contains applications `f(x)`, `f(y)`, and `g(z)`, where `f` and `g` are
distinct unary function symbols:

| Shared pair | Relevance to these applications |
| --- | --- |
| `(x, y)` | If `x = y`, congruence forces `f(x) = f(y)`. This can merge result classes or contradict a disequality between the results. |
| `(x, z)` | Equating these arguments does not by itself connect `f(x)` and `g(z)`: their function symbols differ. |
| `(y, z)` | The same distinction holds for `f(y)` and `g(z)`. |

Primo's EUF producer therefore proposes `(x, y)` for these applications.
Arithmetic might currently assign `x` and `y` the same value while the EUF
model keeps them distinct. The coordinator can then try the equality through
SAT, causing both components to check that choice. Merely putting `(x, y)`
in the care graph does not justify learning `x = y` as a theory consequence.

The {source}`EUF pair generator <src/smt/solver_engines.cpp#L2969-L3072>`
uses the [curried, flattened applications](euf.md). It collects definitions
whose argument is a shared term, groups them by the current congruence class
of their function term, and returns distinct argument pairs within each group.
For a multiargument application, that function term can represent a partially
applied prefix. The proposed pairs can therefore change when congruence classes
change. The QF_UFLIA EUF component
{source}`refreshes its pairs after a successful full check and returns them for the checked model token <src/smt/solver_engines.cpp#L10071-L10093>`.

Jovanović and Barrett's *Being Careful about Theory Combination*, §3,
Definition 8, gives care functions a stronger contract: for a satisfiable
component, an empty care graph must guarantee that every complete arrangement
extending the propagated equalities and disequalities remains satisfiable. This
allows their combination method to restrict arrangement search. Section 4
derives an EUF care function from the argument positions that can trigger
congruence; the [source table](#sources-and-implementation-status) also links
the earlier *Sharing is Caring* paper.

Primo borrows that EUF insight to prioritize choices. Its pair generator does
not implement all the paper's exclusions for already settled argument pairs
and application results. The general component driver accepts suggestions
through {source}`TheoryComponent::care_pairs <src/theory/theory_component.hpp#L123-L125>`
without requiring the paper's care-function guarantee. It retains the complete
model-partition check described below, even when the care list is empty.

### Comparing complete model projections

{source}`ComponentModelCoordinator::reconcile <src/smt/component_model_coordinator.cpp#L140-L424>`
asks each component for a full check under the assigned interface relations.
In model-guided mode it validates a complete partition of every assigned
scope: every shared term appears exactly once, the sort matches, the component
and assignment epoch are current, and all SAT-selected relations hold.

The coordinator first compares the component models on the suggested care
pairs. A pair is a disagreement when one model puts its endpoints in the same
class and another keeps them separate. The
{source}`selection code <src/smt/component_model_coordinator.cpp#L347-L423>`
chooses a disagreeing care pair before considering other pairs, materializes
its equality literal, and returns a positive equality choice for SAT.

If no care pair disagrees, the coordinator compares spanning edges from every
projected partition. Each partition's spanning equalities must also hold for
the other participants in that scope; checking all of them establishes that
the partitions are identical without enumerating all pairs. A disagreement
found here also creates an equality choice. Agreement on care pairs alone is
insufficient for accepting the combined model.

The protocol also supports model mutation. A successful mutation must advance
the model revision and preserve every requested relation. The coordinator
regenerates comparison edges afterwards, including relations not mentioned in
the request. However, the production EUF and LIA descriptors both declare
`model_mutation = false`: their current combination uses equality decisions.
Generic support for mutation is not evidence that these adapters implement it.

### Search, fallback, and inconclusive results

{source}`solve_component_recipe <src/smt/component_solver.cpp#L270-L451>`
maintains the active equality assumptions. If SAT rejects them, it learns a
blocking clause from the failed assumptions and preserves surviving choices.
That rejection concerns the chosen arrangement, not necessarily the input.

Model guidance has a finite decision budget. Reaching it, or encountering a
recoverable unknown, unsupported, or resource-limited model-reconciliation
result, starts a fresh solve with the complete-arrangement policy. That policy
materializes every pair in each normalized scope and requires each component
to satisfy the selected equalities and disequalities. Missing model-projection
support also selects complete arrangements during planning.

The fallback exhausts the interface choices when the component procedures can
decide their obligations. It does not remove integer-search limits: integer
completion, resource exhaustion, or cancellation can still yield `unknown`.
An online dispatcher failure has its own typed exit; it is not converted into
an equality conflict. Cancellation does not trigger another search.

The QF_UFLIA recipe gives arithmetic priority during model reconciliation and
EUF priority during online checking. Online LIA checking uses rational
arithmetic state, followed by integer completion for full acceptance. These
priorities are explicit recipe data, not theory-name tests in the collection
driver.

## Building the combined model

Agreement on shared equality classes is the combination criterion. Printed
models must additionally preserve concrete arithmetic values, function
interpretations, and the original expressions removed by preprocessing.

The Real route supplies accepted internal equalities and arithmetic values to
{source}`its typed model construction <src/smt/solver_engines.cpp#L11873-L11898>`.
For the component route,
{source}`validate_component_fragment <src/smt/component_model_assembler.cpp#L425-L492>`
checks that every export belongs to the exact accepted token, covers required
shared bindings, respects the accepted arrangement or projection, and only
exports owned symbols.

{source}`assemble_component_model <src/smt/component_model_assembler.cpp#L494-L582>`
keeps uninterpreted domain labels local to each component until shared terms
identify them. Arithmetic supplies authoritative numeric values. Conflicting
bindings, retained facts, or function rows cause an error. Function arguments
are compared by their evaluated values, so two syntactically different tuples
cannot acquire inconsistent outputs after model assembly. Conversion records
restore eliminated expressions, and final assertions are evaluated before the
immutable `core::Model` is returned.

## Cost and observable mechanisms

With `n` shared terms in one scope, a complete arrangement contains
`n(n - 1)/2` pairs. Sparse registration delays that cost; it does not remove
the cost of a fallback. Model-guided selection compares class spanning edges
and care pairs, but component checks, exact arithmetic, model exports, and
repeated SAT calls can still dominate.

| Mechanism | Cost boundary and evidence in counters |
| --- | --- |
| Sparse equality creation | Registry entries and materialized SAT pairs grow on demand. Compare `:no.interface.registry-entries` or `:combination.model.materialized-pairs` with the corresponding total-pair count. |
| Convex exact fallback | Equal-valued unresolved pairs may still require many arithmetic probes. `:no.interface.lra-exact-fallback-equalities` counts discovered equalities; it is not a count of all work. |
| Real model reconciliation | Candidate edges can induce larger congruence closures. `:no.shared-model.compatibility-consequence-disagreements` exposes disagreements created by those consequences. |
| Component model comparison | For a fixed number of participants, the ordinary care/spanning pass is linear in the supplied edges, with expected constant-time partition lookups. `:combination.model.care-pair-agreements` and `.spanning-pair-checks` count visits, including duplicates. This does not bound mutation or component solving. |
| Complete-arrangement restart | `:combination.model.complete-fallbacks` and its reason counters show when sparse guidance gave way to the fallback. |

## Defaults and limitations

The {source}`option definitions <src/smt/check_sat.hpp#L396-L421>` distinguish
enabled behavior from experiments:

| Control | Current setting and scope |
| --- | --- |
| QF_UFLRA model reconciliation | Enabled; `--no-qf-uflra-model-reconciliation` selects the retained exact route. |
| `--nelson-oppen-prop` | `final` by default. `online` is an opt-in paired-bound propagation path. `exact` disables the optional equality producer while retaining required combination checks; legacy `off` is an alias for `exact`. |
| QF_UFLIA model guidance / online components | Both enabled with the incremental EUF engine. `--no-qf-uflia-model-combination` selects complete arrangements directly. The model-decision budget defaults to 512. |
| `--experimental-generic-model-combination` | Off. It selects the older `ModelBasedCombinationCoordinator` for the experimental QF_UFLRA path, not the production collection driver. |

The reusable protocol has tests with three and four components, but the
production arithmetic combinations still pair EUF with one arithmetic theory.
Finite shared domains, overlapping interpreted signatures, and unrestricted
combinations of arbitrary new theories are outside its admitted contract.
The default session rebuilds solving state between
queries; online state within a solve does not imply cross-query persistence.

## Sources and implementation status

| Source and sections | Implemented use and boundary |
| --- | --- |
| Greg Nelson and Derek C. Oppen, *Simplification by Cooperating Decision Procedures*, TOPLAS 1(2), 1979, pp. 245–257. The [1978 Stanford report](https://ftpmirror.your.org/pub/misc/bitsavers/pdf/stanford/Stanford_CS_TR_Collection_2025-12-12/OCR/CS-TR-78-652-ocr.pdf), §§2.2–2.4, is the reading copy. | Purification, equality exchange, and the distinction between convex closure and case splitting. Primo's SAT integration and representation are separate adaptations. |
| [Leonardo de Moura and Nikolaj Bjørner, *Model-based Theory Combination*](https://doi.org/10.1016/j.entcs.2008.04.079), ENTCS 198(2), 2008, pp. 37–49, §§2.1, 4, 5.1–5.2 and Appendix A | Candidate-model partitions, reversible equality choices, opportunistic LRA equalities, and freedom-interval mutation. The dedicated Real path implements these mechanisms with its own reconciliation checks. Neither route implements the full Appendix A `DPLL(T^M)` transition system. |
| [Bruttomesso, Cimatti, Franzén, Griggio and Sebastiani, *Delayed Theory Combination vs. Nelson–Oppen for Satisfiability Modulo Theories: A Comparative Analysis*](https://es-static.fbk.eu/people/griggio/papers/lpar06_dtc.pdf), LPAR 2006, §§2–3, Figure 4 | SAT-visible arrangements and learned clauses permit incomplete equality deduction and non-convex components. Primo does not claim the paper's NO-emulation branching bound or its particular last-UIP/negative-first strategy. |
| [Dejan Jovanović and Clark Barrett, *Sharing is Caring: Combination of Theories*](https://cvc4.github.io/publications/2011/JB11-FroCoS.pdf), FroCoS 2011, pp. 195–210; expanded as [*Being Careful about Theory Combination*](https://theory.stanford.edu/~barrett/pubs/JB13.pdf), Formal Methods in System Design 42(1), 2013, pp. 67–90, §§3–4 | EUF argument pairs that can trigger congruence motivate Primo's care-pair generator. The component driver uses its output to prioritize model disagreements. It retains complete partition comparison and arrangement fallback rather than relying on the paper's care-function contract to omit other relations. |
| [Alberto Griggio, *An Effective SMT Engine for Formal Verification*](https://iris.unitn.it/handle/11572/368265), PhD thesis, 2009, §1.5.1, pp. 28–35 | Detailed purification, shared-variable definitions, arrangement criterion, and the distinction between classical and SAT-mediated combination. This is an explanatory source, not MathSAT implementation code. |
| [Tim King, *Effective Algorithms for the Satisfiability of Quantifier-Free Formulas Over Linear Real and Integer Arithmetic*](https://cs.nyu.edu/media/publications/king_tim.pdf), PhD thesis, 2014, §§1.5.5–1.5.6 | Theory-engine ownership, dispatch, and the distinction between partial and full checks. Its care-graph discussion is context; Primo's care-pair selection does not implement a general care-graph algorithm. |
| [Martin Bromberger, *Decision Procedures for Linear Arithmetic*](https://publikationen.sulb.uni-saarland.de/handle/20.500.11880/28955), PhD thesis, 2019, §5.4 | Equality-basis normalization and explanations motivate a further arithmetic equality producer. This complete basis method remains unimplemented; local tableau rules and exact probes have different costs and completeness mechanisms. |
| {source}`Primo's component design <docs/n-way-combination-migration.md>` | Many-sorted scope normalization, component descriptors, token freshness, detached registration, and typed model exports are original implementation contracts. |

The related [LRA source map](lra.md#sources-and-implementation-status) covers
the simplex papers and the newer optional equality producer. The
[review record](#theory-combination-chapter) records the primary
sections read for this chapter.

## Invariants exercised by existing tests

| Obligation | Regression or mechanism test |
| --- | --- |
| Equality must travel in both directions until the convex problem is settled | {source}`EUF–LRA–EUF fixed-point regression <tests/unit/qf_lra_solver_tests.cpp#L1429-L1452>` |
| A propagated equality remains guarded by the branch that proves it | {source}`assignment-guard regression <tests/unit/qf_lra_solver_tests.cpp#L1553-L1594>` |
| A candidate merge must preserve arithmetic agreement after congruence | {source}`optional-merge consequence regression <tests/unit/qf_lra_solver_tests.cpp#L2530-L2553>` |
| Combination values roll back and SAT truth comes from the shared view | {source}`manager rollback and assignment tests <tests/unit/theory_combination_tests.cpp#L474-L592>` |
| Scope normalization rejects invalid ownership and respects disconnected sorts | {source}`admission and scope tests <tests/unit/component_combination_tests.cpp#L268-L344>` |
| More than two participants agree with an independent partition oracle | {source}`three/four-component cases <tests/unit/component_combination_tests.cpp#L409-L487>` |
| Stale or incomplete model projections cannot be accepted | {source}`malformed-projection tests <tests/unit/component_combination_tests.cpp#L489-L525>` |
| Mutations cannot hide newly created disagreements; non-convex choices remain alternatives | {source}`mutation and disjunction tests <tests/unit/component_combination_tests.cpp#L591-L650>` |
| EUF care pairs follow function classes, including prefixes of multiargument applications | {source}`EUF care-pair regressions <tests/unit/qf_lra_solver_tests.cpp#L2068-L2140>` |
| Care pairs take priority, while agreements still lead to the spanning check and invalid pairs are rejected | {source}`care selection and validation regression <tests/unit/component_combination_tests.cpp#L652-L701>` |
| A surviving propagated literal keeps its explanation across unrelated rollback | {source}`routing and explanation-lifetime test <tests/unit/component_combination_tests.cpp#L527-L589>` |
| A zero guidance budget reaches complete arrangements | {source}`fallback regression <tests/unit/qf_uflia_solver_tests.cpp#L275-L299>` |
| Purified arguments use their arithmetic values in the printed model | {source}`integer argument-model regression <tests/unit/qf_uflia_solver_tests.cpp#L353-L380>` |

## Evidence and open leads

The {source}`component migration record <docs/n-way-combination-migration.md>`
retains the paired QF_UFLIA evaluations, fuzzing and independent model checks.
The {source}`Real combination record <docs/nelson-oppen.md>` and
{source}`shared-assignment qualification <docs/current-status.md>` preserve
their separate revision boundaries and limitations. Exact run provenance is in
{source}`the run log <docs/run-log.json>`; these historical runs are not fresh
qualification of this documentation pass.

The {source}`complete implied-equality basis lead <docs/open-leads.md#L2344-L2401>`
asks whether Bromberger's method can reduce the need for exact pair probes.
Two care-related investigations are recorded separately:

- [More selective and cheaper care-pair generation](../leads/care-pair-generation.md)
  retains the current model checks and evaluates filtering and lazy generation.
- [Reducing combination obligations with care graphs](../leads/care-guided-combination.md)
  requires a stronger component contract, a correctness argument, and a method
  for constructing compatible models from the reduced set of obligations.

Full `DPLL(T^M)` integration and a common Real/Int coordinator remain further
design work. They require explicit mappings of the retained Real behavior,
explanation lifetimes, model acceptance, and fallback guarantees before any
route can be replaced. The {source}`earlier transition checklist
<docs/papers/model-based-theory-combination.md>` preserves that investigation.
