# Models: values, function tables, and reconstruction

A model must satisfy the assertions the user supplied, including expressions
that preprocessing removed. A consistent EUF closure supplies equalities;
arithmetic supplies numeric values. Neither alone is the complete interpretation
that `get-model` prints. The SMT layer combines those facts, gives values to
unconstrained symbols, and restores eliminated constants before returning an
owning `core::Model`.

For example, eliminating `x` from `x = f(y + 1)` leaves a reconstruction
obligation. Once the reduced problem has chosen `y = 2` and `f(3) = 7`, the
restored value must be `x = 7`. Giving `x` an arbitrary value, or treating
`f(y + 1)` and `f(3)` as different function entries, would produce an invalid
model. [The worked replay below](#following-a-model-back-through-preprocessing)
traces this example through the actual representation.

This chapter covers models for the supported ground fragments. The
[quantified UF route](quantifiers.md#supported-scope-and-answers) does not export models.
The [combination chapter](combination.md) explains how candidate theory models
are reconciled before construction starts.

## Representation and ownership

| Object | Representation and lifetime |
| --- | --- |
| {source}`ModelValue <src/core/model.hpp#L26-L35>` | A `SortId` and a Boolean, exact rational, or `DomainValue` index. The sort is part of equality: an Int value is not a Real value with the same numeral, and indices from different uninterpreted sorts are not equal. |
| {source}`ModelInterpretation <src/core/model.hpp#L41-L51>` | One symbol's default value and finite vector of argument/result rows. For a constant, the default is its value and there are no rows. |
| {source}`core::Model <src/core/model.hpp#L53-L75>` | Owns copies of sorts and symbols, the interpretations, visible symbol IDs, and tuple lookup tables. Its public operations are read-only. It owns no SAT state or term-manager references. |
| {source}`ModelEvaluator <src/core/model.hpp#L77-L94>` | Borrows a live `TermManager` and a model or construction resolver. Its memo belongs to that interpretation and manager; neither may change beneath it. |
| {source}`ModelConversionChain <src/smt/model_reconstruction.hpp#L85-L115>` | A persistent list of typed preprocessing steps. Appending creates a new shared tail; replay visits the newest step first. Stored term IDs still require their original manager. |
| Construction adapters | `smt/` owns temporary classes, authoritative values, mutable interpretation slots, and evaluation epochs. Those construction objects are discarded after the model is frozen. |

The {source}`Model constructor <src/core/model.cpp#L57-L102>` checks symbol IDs,
value sorts and payloads, argument counts, and function rows. An Int payload
must be integral. Repeating a tuple with the same result is consistent;
repeating it with a different result raises `ModelError`. Explicit visible
symbol lists reject duplicates, missing interpretations, and internal symbols.

Because the signature is copied, printing a saved model does not need the
original frontend. Evaluating a `TermId` still does: the ID names syntax in
one particular live manager. Neither a domain index nor an EUF representative
is a portable identifier across models.

## Constructing the interpretation of the reduced problem

There are two adapters, with the same frozen representation and replay rules:

| Route | Construction entry point |
| --- | --- |
| Boolean, QF_UF, standalone arithmetic, the dedicated mixed-Real route, and preprocessing-only solutions | {source}`build_typed_model <src/smt/typed_model_builder.cpp#L343-L363>` uses its private `ComponentModelBuilder`. Inputs include active reduced assertions, retained theory assignments, exact numeric values, and conversion records. |
| General component route used by QF_UFLIA | {source}`assemble_component_model <src/smt/component_model_assembler.cpp#L494-L582>` validates and combines exported fragments, then uses its private `FragmentModelBuilder`. It also accepts authoritative symbol interpretations from components. |

Despite its name, `ComponentModelBuilder` is the first adapter above, not the
general component collection driver. Both builders use a separate,
direct-`TermId` `IncrementalCongruenceClosure`; they do not modify the
`FlatCongruenceClosure` used during EUF search.

### Retained facts, authoritative values, and completion choices

Construction begins from the **reduced problem**. The
{source}`base-building code <src/smt/typed_model_builder.cpp#L39-L120>` walks
active assertion roots and unfiltered theory assignments, installs their
equalities and disequalities, and keeps `true` distinct from `false`.
Numeric component values and literal constants provide *anchors*: values the
builder must preserve. Purification records also link each application to its
proxy, so an arithmetic value assigned to the proxy reaches the application.

Terms with identical typed anchors are merged before saturation. If one class
then contains incompatible anchors, construction fails. This is essential for
arithmetic arguments: independently supplied `x = 2` and `y = 2` require
`f(x)` and `f(y)` to agree, even if the input contained no equality `x = y`.

After saturation, Boolean constants and applications not yet anchored to
`true` or `false` are completed as false. The
[EUF coverage argument](euf.md#why-no-third-boolean-value-is-needed) explains
why protected Boolean arguments and congruence make that safe on the prepared
ground path. The fragment adapter preserves component-owned interpretations
instead of overwriting them with this choice.

For the remaining classes, {source}`class_value
<src/smt/typed_model_builder.cpp#L188-L217>` uses a domain index for an
uninterpreted sort or a fresh integer-valued rational for a numeric sort,
avoiding numbers already used. Distinct unanchored EUF classes therefore receive
distinct values within their sort. These choices complete a particular model;
they are not values entailed by the input. Symbols outside the relevant roots
receive a sort-correct default: false, zero, or an uninterpreted-domain value.

Only relevant syntax contributes application rows. Terms left in the arena
from earlier preprocessing stages do not become independent constraints merely
because their IDs still exist. In particular, an application appearing only
in an eliminated definition is evaluated later against the established table.
It does not acquire a new arbitrary result.

### Function rows use evaluated arguments

For an interpretation `I`, a row for `f(t1, …, tn)` has key

```text
(SymbolId(f), [value_I(t1), …, value_I(tn)])
```

The builder evaluates the children before inserting the result of the
application's class. The table is indexed by semantic values, not by child
`TermId`s or by printed expressions. The
{source}`insertion check <src/smt/typed_model_builder.cpp#L132-L152>` rejects
different results for one key. Thus if `x` and `y` both evaluate to `2`, rows
`f(x) = 7` and `f(y) = 8` are inconsistent; choosing whichever row was printed
last cannot repair them.

Each function also has a default result for every tuple absent from its finite
table. This makes the function total, including at arguments never encountered
during solving. {source}`Model::apply <src/core/model.cpp#L104-L120>` returns
the explicit row when present and otherwise the default, without inserting
anything or changing the model.

### Combining exported fragments

On the collection route, an accepted component token identifies the component,
assignment epoch, and model revision. Before assembly,
{source}`validate_component_fragment <src/smt/component_model_assembler.cpp#L425-L492>`
checks that the export has that exact token, contains required shared bindings,
preserves the accepted equality partition or arrangement, and only supplies
interpretations of owned symbols.

Uninterpreted-domain labels remain local to each component until shared terms
identify them. Component A's value `0` and component B's value `0` need not be
the same element. Conversely, if both components give a value to shared term
`x`, those two local values must be identified even when their indices differ.
{source}`FragmentDomains <src/smt/component_model_assembler.cpp#L352-L404>`
performs that identification before translating bindings and function rows to
one model. Numeric and Boolean bindings already have concrete meanings;
conflicting values cannot be repaired by renaming.

The fragment builder then applies the same class, evaluated-tuple, and replay
checks. Component-owned tables remain authoritative. Assembly checks the
supplied final assertions against the resulting model before returning it.
Choosing an arrangement and establishing component consistency remain the
responsibility of [the combination driver](combination.md#building-the-combined-model).

## Following a model back through preprocessing

The [preprocessing chapter](preprocessing.md) follows the forward pass order;
[the tactic contract](tactics.md#a-transformation-and-its-model) explains why
each goal carries its own conversion history. This section follows that history
backwards after solving.

Write `F0 → F1 → … → Fn` for successive preprocessing stages. A conversion
record for `Fi → Fi+1` describes how to turn a model of `Fi+1` into one of
`Fi`. Construction starts from a model of `Fn`, so records must be replayed
in reverse execution order. Keeping only the last definition of each symbol
would lose the intermediate interpretations needed by earlier stages.

Consider the assertions

```text
x = f(y + 1)       y = 2       f(3) = 7       p ∨ q
```

with Int constants `x`, `y`, Boolean constants `p`, `q`, and `f : Int → Int`.
For a log that first eliminates `x`, then `y`, the forward and reverse steps are:

| Stage | Assertions or interpretation | Saved or replayed operation |
| --- | --- | --- |
| Original `F0` | The four assertions above | — |
| Eliminate `x` | `y = 2`, `f(3) = 7`, `p ∨ q` | Save `x ← f(y + 1)`. |
| Eliminate `y` | `f(3) = 7`, `p ∨ q` | Save `y ← 2`. |
| Build a base model | Table `f: [3] ↦ 7`, with default `0`; `p = false`, `q = true` | Preserve the numeric anchor and retained Boolean assignment; complete unanchored `p`. Eliminated constants may still have provisional defaults. |
| Replay the newest record | Set `y = 2`. | Evaluate `2` in the base interpretation, then update `y`. |
| Replay the older record | `y + 1` evaluates to `3`, so `f(y + 1)` reads the existing row `7`. Set `x = 7`. | Use a fresh evaluator after the change to `y`. No row is inserted during replay. |
| Freeze and check | `x = 7`, `y = 2`, `f(3) = 7`, `p ∨ q = true` | All four original assertions evaluate to true. |

For the Boolean part, take a SAT candidate with `p = false`, `q = true`.
If pure-theory filtering is enabled, `p`'s false assignment is omitted and
`q`'s true assignment is retained. Here `p` has no role as a function argument,
so completing its unanchored class as false is safe. For the arithmetic part,
purification can supply a proxy for `f(3)`: its authoritative value `7` and
its recorded equality to `f(3)` give that application its result. The numeric
value must survive both construction and replay.

The numeric replay in this log is exercised by the
{source}`staged reconstruction regression <tests/unit/model_tests.cpp#L289-L323>`;
it illustrates the replay contract rather than prescribing the order selected
by preprocessing heuristics. One corresponding serialized interpretation is:

```lisp
(model
  (define-fun x () Int 7)
  (define-fun y () Int 2)
  (define-fun p () Bool false)
  (define-fun q () Bool true)
  (define-fun f ((u Int)) Int (ite (= u 3) 7 0)))
```

### What each conversion does

{source}`The conversion variants <src/smt/model_reconstruction.hpp#L19-L83>`
distinguish elimination from retained facts. The
{source}`replay visitor <src/smt/typed_model_builder.cpp#L249-L285>` handles
them in the recorded order, not in batches grouped by variant.

| Record | Reconstruction obligation |
| --- | --- |
| `DirectedEqualitySubstitutionConversion`, `retained_facts = false` | Evaluate all right-hand sides in the interpretation of the following stage, then update the eliminated same-sort constants simultaneously. |
| `LiaSubstitutionConversion` | The same staged constant restoration for recorded integer substitutions. |
| `DirectedEqualitySubstitutionConversion`, `retained_facts = true` | Ground-unit rewriting kept the facts in the reduced problem. Check the equalities at this replay stage; do not use them to overwrite constants. |
| `BooleanTheoryAssignmentConversion` | Check the retained Boolean facts at this stage. These records are not instructions to assign arbitrary truth values after solving. |
| `PurificationProxyConversion` | Application/proxy equalities constrain base construction. Replay does not change their public function interpretations. |
| `MonotoneEliminationConversion` | Restore each eliminated Real constant beyond its tightest one-sided bound, then check the discharged facts together. |

For a simultaneous substitution step, every right-hand side is evaluated
*before* any target changes. For different steps, the later step's updates
must be visible to the earlier one. Both requirements are enforced by creating
an evaluator for the step, collecting updates, and only then applying them.

For monotone Real elimination, fix the current values of the other variables.
A constraint `a*x + r ≤ b` with `a > 0` gives an upper bound
`x ≤ (b - r)/a`; a negative coefficient reverses the direction. Preprocessing
records whether all required literals push `x` upward or downward, accounting
for their polarity. The {source}`reconstruction loop
<src/smt/typed_model_builder.cpp#L288-L323>` evaluates these thresholds and
chooses one unit below the smallest upper threshold or above the largest lower
threshold. The strict margin also satisfies strict inequalities. It starts a
new evaluation epoch after each variable and checks shared discharged
constraints after the batch, because a later variable can finish repairing
them. This is the one-sided baseline rule documented in Primo's
{source}`elimination design note <docs/papers/monotone-variable-elimination.md>`;
it is not a general quantifier-elimination procedure.

### Dependencies and evaluation epochs

An **authoritative value** comes from accepted component facts or an interpreted
literal. A **completion choice** gives an otherwise unconstrained class or
symbol a value. An eliminated constant's initial default is **provisional**:
its reconstruction record will replace it. A **derived value**, such as the
result of evaluating `f(y + 1)`, depends on the interpretation used for that
evaluation. These are semantic roles; the implementation does not attach a
four-way tag to every value.

Changing `y` invalidates cached evaluations of `y + 1` and `f(y + 1)`.
Primo handles that dependency by discarding the evaluator between changed
stages. Within one epoch, the resolver must never revise a value already
returned. Base construction may add a previously absent function row; replay
only reads those tables and changes eliminated constants between epochs.
There is no selective dependency graph or incremental invalidation algorithm
in these builders.

This also explains why the old application `f(y + 1)` is not given a row
before `y` is restored: such a row could be keyed by a provisional argument
value. Evaluating it during replay instead uses the fixed function at the
newly evaluated tuple. If that tuple has no explicit row, the function's
default supplies its value.

A cyclic equality such as `x = f(x)` is a different matter. It can be
satisfied by choosing a value `v` for their common class and installing
`f(v) = v`; evaluating the argument `x` does not recursively expand a
definition of `x`. The {source}`solved-constant elimination guard
<src/smt/preprocessing.cpp#L4713-L4730>` rejects self-dependent substitution
candidates. Model construction can therefore preserve a cyclic EUF equation
without treating it as a cyclic conversion to replay.

## Freezing, checking, and printing

After replay, the builders construct `shared_ptr<const core::Model>`.
Construction checks its structure; subsequent evaluation and printing cannot
complete or repair it. This prevents output order from changing the meaning
of constants or function rows.

The {source}`core evaluator <src/core/model.cpp#L211-L262>` uses an explicit
stack and a memo over demanded terms. It computes interpreted operators from
their children using exact arithmetic and consults the model only for constants
and uninterpreted applications. For an `ite`, it evaluates the condition and
selected branch. A component cache cannot override the arithmetic meaning of
an expression. Bound variables and quantified terms are outside its supported
ground evaluation contract.

In the rebuilding solver path, {source}`finalize_model
<src/smt/solver_session.cpp#L649-L673>` checks the original, pre-preprocessing
assertions whenever model checking is requested, and also in debug builds when
a model is produced. The {source}`retained-session path
<src/smt/smt_session.cpp#L159-L179>` checks active assertions when `check_model`
is enabled. The component assembler additionally checks its supplied final
assertions. These checks use Primo's own evaluator; independent printed-model
validation is still required by the [correctness guide](../development/correctness.md).

{source}`smtlib::print_model <src/smtlib/printer.cpp#L49-L109>` emits visible
symbols, using nested `ite` expressions for finite rows and the default as
the final branch. It can omit rows equal to the default without changing the
function. It quotes names and chooses parameter names that avoid capture.
Internal auxiliaries are identified by symbol metadata and are not printed;
a user's similar-looking name is not evidence that it is internal.

Retained sessions supply the active visible signature while preserving
interpretations needed for checking. Popping a declaration or reusing its name
does not mutate an earlier model snapshot. The session nevertheless invalidates
its *current* model after relevant state changes: {source}`get-model
<src/smt/smt_session.cpp#L326-L332>` requires both `:produce-models true` and a
current SAT result containing a model.

Contradictory anchors, false retained facts, incompatible tuple rows, and
failed assertion checks raise `ModelError`. They are internal correctness
failures, not an `unsat` proof or an ordinary `unknown` result. Resource limits
in earlier solving or model reconciliation can still yield `unknown` before
construction. Model-free solving avoids postsolve reconstruction and printing.

## Cost and limitations

Within one evaluation epoch, each demanded DAG node is evaluated once; work
is linear in the visited nodes and edges apart from exact arithmetic and tuple
hashing. The explicit stack and memo also require space. A new epoch can revisit
the same DAG, so this is not a linear bound for the whole conversion chain.
Base construction additionally performs congruence closure and stores the
relevant terms, classes, function rows, and signature snapshot.

The two construction adapters currently duplicate much of the replay logic.
Neither provides dependency-directed reevaluation, minimal models, or a proof
certificate. A valid model may use arbitrary defaults and different domain
indices in different runs. `:time.model.reconstruction-us` measures the builder;
the rebuilding path records assertion checking and serialization separately,
while retained sessions report `:time.model.check-and-print-us` for those two
operations together. Independent model-checking time must stay separate from
solver benchmark timing.

## Sources and implementation status

The algorithm and ownership basis is Primo's original
{source}`model-interpretation design contract <docs/model-interpretation.md>`,
together with the {source}`typed conversion contract <src/smt/model_reconstruction.hpp>`.
The implemented design uses staged reverse replay and stable evaluation epochs.
The older contract's dependency-invalidation requirements are satisfied by
fresh evaluators; they do not imply an implemented dependency worklist.
Congruence closure and theory reconciliation have their paper mappings in
[EUF](euf.md) and [combination](combination.md). No additional optimized
model-construction algorithm from another solver is claimed here.

| Obligation | Existing regression coverage |
| --- | --- |
| Typed values, total defaults, and conflicting tuple rejection | {source}`Model table tests <tests/unit/model_tests.cpp#L127-L146>`. |
| Interpreted syntax uses semantic values, exact arithmetic, and the selected ITE branch | {source}`Evaluator tests <tests/unit/model_tests.cpp#L148-L227>`. |
| Deep application DAGs do not require recursive evaluation | {source}`Deep application test <tests/unit/model_tests.cpp#L229-L240>`. |
| Printing survives signature destruction and avoids capture | {source}`Printer lifetime and name tests <tests/unit/model_tests.cpp#L242-L288>`. |
| Reverse replay preserves intermediate versions, fixed tables, and cyclic EUF equations | {source}`Reconstruction tests <tests/unit/model_tests.cpp#L289-L342>` and {source}`conversion order test <tests/unit/tactic_tests.cpp#L191-L216>`. |
| Fragment labels are component-local; exports preserve tokens and shared classes | {source}`Fragment validation and assembly tests <tests/unit/model_tests.cpp#L38-L125>`. |
| Restored Real values satisfy eliminated one-sided constraints | {source}`Monotone reconstruction test <tests/unit/qf_lra_solver_tests.cpp#L68-L96>`. |
| Saved models remain independent across session scopes | {source}`Session lifetime tests <tests/unit/smt_session_tests.cpp#L402-L431>`. |

These are tests to maintain, not new execution results. The earlier model
redesign's qualification remains in its {source}`dated evidence record
<docs/model-interpretation.md#L30-L58>`; subsequent component assembly has its
own {source}`migration evidence <docs/n-way-combination-migration.md>`.
The [review ledger](#model-construction-chapter-checks) records the
documentation checks separately from those historical solver runs.
