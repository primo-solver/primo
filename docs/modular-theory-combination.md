# Modular theory combination

The N-component migration is in progress; see
[](n-way-combination-migration.md) for its frozen boundary, implementation status
and acceptance gates. The production routes described below remain the baseline
until that migration is qualified.

Primo's modular combination layer is based on Nelson and Oppen's arrangement
method and on SAT-mediated delayed theory combination. Its first non-convex
target is `QF_UFLIA`; the established `QF_UFLRA` solver retains its specialized
convex model-reconciliation policy by default and can route its model decisions
through the generic coordinator with
`--experimental-generic-model-combination`.

The primary algorithmic sources are Nelson and Oppen, *Simplification by
Cooperating Decision Procedures* (TOPLAS 1979); Oppen, *Complexity, Convexity
and Combinations of Theories* (TCS 1980); Krstic and Goel, *Architecting
Solvers for SAT Modulo Theories: Nelson-Oppen with DPLL* (FroCoS 2007);
Bruttomesso et al., *Delayed Theory Combination vs. Nelson-Oppen for
Satisfiability Modulo Theories* (LPAR 2006); and de Moura and Bjorner,
*Model-based Theory Combination* (ENTCS 2008). The detailed readings and the
status of the existing convex implementation are recorded in
[](papers/nelson-oppen-closure-methods.md) and
[](papers/model-based-theory-combination.md).

The implementation checklist below uses Bruttomesso et al. Section 3 and
Theorem 2: augment the Boolean abstraction with every interface equality, give
both components the same total interface assignment, and learn a component
conflict set as a SAT clause. Theorem 2 explicitly permits stably-infinite,
non-convex components without complete equality deduction. The retained
QF_UFLRA policy uses de Moura and Bjorner Sections 4, 5.1, and 5.2 for
model-based propagation, opportunistic equality production, and arithmetic
model mutation. It does not claim the paper's complete Appendix A transition
system.

## Component contract

`theory::TheorySolver` is a solve-scoped protocol. Its immutable
`TheoryProperties` declares the component identifier, shared sorts, stable
infiniteness, convexity, and optional model/equality/care capabilities. A check
receives only literals owned by that component and an SMT-owned interface
arrangement. It returns consistency, an explained SAT clause or literal, a
fresh non-convex decision request, or a typed stop. A successful check may
include a `TheoryModelProjection`: an opaque `TheoryModelToken`, containing the
component identity, SAT-assignment epoch, and model revision, plus equality
partitions grouped by shared sort. The component has no SAT-backend handle and
cannot inspect another component's private closure, tableau, or model.

`TheorySolver::mutate` accepts a source token and required interface relations.
It returns a newer compatible projection, a typed fixed point, or a typed stop.
"Newer" is part of the contract, not a formality: the coordinator rejects a
mutation whose token revision does not strictly exceed the source revision, and
reports that as `theory model mutation did not advance its revision` rather
than as a malformed model. A component that builds its mutated projection with
one of the shared projection helpers must therefore pass its own monotone
counter; the helpers take the revision as a parameter and default it to 1,
which is correct only for the first projection of an assignment.
`TheorySolver::care_pairs` optionally identifies the pairs most relevant to the
component. Default implementations report a mutation fixed point and no care
pairs, so components opt into these operations explicitly.

The SMT coordinator owns purification, atom ownership, interface discovery,
equality materialization, SAT scheduling, and combined-model construction.
`sat/` continues to see clauses and literals only. `core/` continues to own all
terms and sorts.

`theory::IncrementalTheorySolver` is the backend-neutral lifecycle for the
next migration step. It adds `reset`, decision-level `push_level` and
`pop_to_level`, assignment-delta ingestion, and explicit `Partial` versus
`FullModel` checks. It deliberately has no SAT-backend handle. The interface
and its checkpoint/effort tests are implemented; the current production EUF
and arithmetic callback classes still use the older SMT-owned CaDiCaL adapter,
so this is an API boundary plus migration target rather than a claim that those
component implementations have already moved into `theory/`.

A clause is deliberately the general inference type. For a convex theory it
can encode an explained equality propagation. For a non-convex theory it can
encode a disjunction of interface equalities without falsely claiming that one
member is individually entailed.

## Invariants and obligations

1. Purification gives every non-equality atom exactly one theory owner.
2. Interface relations join terms of one sort that occur in at least two
   components.
3. Every learned clause is entailed by the reporting component under its
   supplied assignment; candidate-model equality is not entailment.
4. A complete-arrangement policy assigns every same-sort shared pair. A convex
   policy may remain sparse only when its equality-propagation completeness
   argument applies.
5. EUF, rational LRA, and integer LIA are stably infinite on their supported
   interfaces. EUF and LRA are convex; LIA is explicitly non-convex.
6. Backtracking is owned by SAT and the coordinator. Theory-local retained
   state must be trailed or rebuilt before a different assignment is checked.
7. A combined `sat` result requires component models that agree on the complete
   shared equality arrangement.

## Initial operations and complexity

The default `QF_UFLIA` policy starts with input-visible interface equalities,
uses component-model disagreements to add equality choices lazily, and keeps
EUF and rational arithmetic synchronized with the CaDiCaL trail. After 512
model-choice batches, or whenever a component returns a typed unsupported,
unknown, or resource-limit result, it restarts with complete delayed
combination. For `n` shared Int terms this backstop materializes all
`n(n-1)/2` equality choices. Each equality is connected to arithmetic by two
non-strict inequalities and Boolean clauses expressing equality iff both
inequalities hold. A false equality therefore lets SAT select one strict
integer orientation. The backstop is deliberately retained because it supplies
the completeness argument for a non-convex component.

The online components consume assignment deltas and roll back at SAT decision
levels, so EUF and rational conflicts are learned during search instead of rebuilding both
components after every complete Boolean model. Integer feasibility is still a
mandatory final check: patching, cuts, and branch-and-bound run on a private
copy of the accepted rational tableau because LIA is non-convex and those
operations are not backtrackable component state. The option is ignored when
the non-incremental EUF engine is selected. The two mechanisms can be disabled
independently with `--no-qf-uflia-model-combination` and
`--no-qf-uflia-online-components`; the historical `--experimental-*` enabling
spellings remain accepted aliases for reproducible old commands.

`QF_UFLRA` does not use this eager policy. Its existing lazy convex equality
exchange, exact fallback, model mutation and reconciliation remain in its
specialized coordinator; final interpretation construction uses the shared
typed builder below.

## Integer model reconstruction

QF_UFLIA now uses the owning typed interpretation described in
[](model-interpretation.md). The engine passes final component assignments and
exact tableau values to `build_typed_model`; purification aliases bridge the
component terms. The old `qf_uflia_int_values` candidate scans and eight-round
substitution heuristic have been removed.

The base interpretation is built from live roots, not all historical arena
applications. Eliminated constant definitions are then replayed in reverse
conversion order against fixed function tables. A historical application and
its rewritten form therefore use the same function value whenever their typed,
evaluated arguments coincide. Ground-unit equality logs are marked as retained
facts, rather than mistaken for eliminated constant definitions.

`CheckSatOutcome::typed_model`, `SolverAnswer::typed_model`, and
`Goal::typed_model()` carry immutable shared ownership through tactics. The
session checks original assertions in debug builds or with `--check-model`,
then serializes once into the compatibility `CheckSatOutcome::model` string.
Model-free solving skips this work. `:time.model.reconstruction-us` and
`:time.model.check-us` distinguish reconstruction and assertion checking.

This is an original component-adapter and postsolve design, not a new
theory-combination algorithm. Focused tests cover the evaluated-tuple witness,
definition chains, cyclic UF equations, input order, all four preprocessing
modes, and both QF_UFLIA combination settings. Boolean/QF_UF, QF_LIA, QF_LRA,
QF_UFLRA and preprocessing-only paths now share the same typed builder and
evaluator. Their generated and seed-mutation correctness gates are collected;
see [](model-interpretation.md) for the accepted matrix and separately reported
resource limits. This is correctness evidence, not a performance promotion.

The legacy string builders and whole-arena completion have been removed.
Invention counts are
diagnostics, not proof of either validity or invalidity. External model
validation remains the acceptance gate. Evidence and exact commands are in
`docs/run-log.json`, under the 2026-09-07 reconstruction entries.

## Model-guided transition layer

The intended optimization path is de Moura and Bjorner's model-based theory
combination, not convex propagation applied to integers. A component may expose
a candidate partition of its shared terms. The coordinator may use that
partition to choose equalities, test compatibility with other component models,
or ask a component to mutate its model before committing to a split. A
candidate-model equality remains a search choice; only an explained theory
consequence may be learned as an entailed clause.

`smt::ModelBasedCombinationCoordinator` implements the backend-neutral
transition after one satisfying SAT assignment. It checks each component,
validates snapshot identity, epoch, completeness, and compatibility with the
current arrangement, asks a component for a compatible model variant, and
compares the resulting partitions. It derives a linear spanning set from each
model class rather than materializing the quadratic cross-product. That
spanning set is only spanning for the partitions it was built from, so it is
recomputed from the mutated projections before the coordinator accepts: a
mutation is validated against the requested interface relations alone and may
merge or split pairs that no pre-mutation partition made visible. Care pairs
are considered first. If a disagreement remains, the coordinator returns one
or a component-provided batch of equality decisions; the caller creates or
resolves their SAT literals and re-enters CaDiCaL. Entailed
literals are converted to explained clauses, while general non-convex clauses,
fresh decisions, cancellation, resource limits, unsupported cases, and unknown
results remain distinct typed actions. Exceptions are not converted into
solver answers.

One reconciliation round asks each non-reference component for at most one
mutation, against the first component's partition. It does not iterate the
components to a mutual fixed point, and a component is not re-asked after a
later component mutates. Soundness does not depend on that: any residual
disagreement on the recomputed spanning set becomes an equality decision rather
than an accept. Iterating to a fixed point is future work.

The coordinator owns no SAT backend and no theory-private model. Its caller
owns assignment-epoch advancement, dynamic equality registration, assumptions,
failed-assumption clauses, and re-entry into CaDiCaL. Thus ordinary Appendix A
DPLL operations remain delegated to the production CDCL backend. The detailed
ownership and transition checklist is recorded in
[](papers/model-based-theory-combination.md).

For QF_UFLIA, the default model-combination path projects the checked integer
tableau and EUF closure onto all shared Int terms. A partition
disagreement creates a positive, reversible CaDiCaL assumption. If the
assumption fails, the coordinator learns the negation of CaDiCaL's failed
assumption subset; it never learns a candidate equality merely because one
model selected it. Complete projections that agree on the spanning set
recomputed from those same projections witness a combinable model.
Because LIA is non-convex, the policy does not use convex equality propagation:
`--qf-uflia-model-decision-budget` bounds model-choice batches (default 512),
and zero selects the complete arrangement immediately. Budget exhaustion,
unsupported projections, ordinary unknown results, and guided LIA resource
limits restart the unchanged complete-arrangement solver with no candidate
assumptions. Cancellation still stops immediately.

The model-guided and online components are combined by default. The online
components reject inconsistent partial SAT trails before a full model is
handed to the model-based coordinator. When reconciliation materializes a new
interface pair, the solve-scoped EUF and LIA component adapters extend their
prepared structures with only the newly registered atoms. `PreparedFlatTheory`
adds the equality to its flat index and base closure. `PreparedLraTheory` retains
the term-to-variable and canonical-polynomial-to-slack maps used during initial
construction, so each new arithmetic bound reuses existing variables and rows
or installs its one missing row. The adapters remain cumulative and immutable
with respect to SAT assignments; round-local closure and tableau copies still
start from their prepared bases. This append-only growth is a local engineering
optimization of the already documented Nelson--Oppen encoding, not an
additional paper algorithm.

The live callback adapter is still disconnected and reconstructed over the
enlarged prepared theory before the next SAT solve. That conservative boundary
preserves the arithmetic tableau invariant that every row is installed before
the first decision-level push. `:combination.components.full-builds`,
`:combination.components.incremental-growths`, and the two
`:combination.components.incremental-*-atoms` counters distinguish base
construction from append-only growth;
`:combination.online-components.rebuilds` records the remaining callback
reconstruction. Moving row installation into a trail-safe incremental callback
lifecycle remains future work.

The QF_UFLIA EUF adapter now implements its declared care-pair capability using
the same care graph as QF_UFLRA. Following Jovanovic and Barrett, *Sharing is
Caring*, Sections 3–4, it groups flattened application definitions by the
current equivalence class of their function term and proposes pairs of shared
arguments only within a group. The resulting pairs prioritize model
disagreements; they do not remove the complete fallback or turn a model choice
into an entailed equality. The `:no.interface.euf-care-*` counters expose the
full pair count, selected pairs, function classes, and pruning.

## Implemented subset and limitations

`QF_UFLIA` is runnable through a sequential two-component coordinator. Its EUF
adapter declares a convex, stably-infinite Int interface; its LIA adapter
declares the same interface stably infinite and non-convex. Both receive the
same complete `InterfaceRelation` vector, but enforce it only through their
owned SAT atoms. The LIA adapter reuses Primo's exact-rational tableau, integer
patching, GCD check, Gomory cuts, and bounded branch-and-bound. Reaching the
existing integer node limit returns a typed resource-limit `unknown`.

The default model-guided route grows its component adapters only when a new
equality or arithmetic bridge atom is registered. Repeated atoms are omitted by
the clausifier, so a pair may append fewer than two arithmetic atoms. Snapshot
epochs are advanced for every satisfying SAT assignment, so projections from
older assignments are rejected. The complete route constructs its complete
arrangement once and remains the fallback when sparse reconciliation reaches a
typed stop or its decision budget.

The online-complete route records
`:combination.online-components.enabled` and
`:combination.online-components.integer-completions`. Existing EUF and LRA
online counters show the partial-assignment activity. The accepted integer
model remains in the ordinary LIA adapter so the existing combined-model and
ordered reconstruction code is unchanged.

The frontend accepts Int-valued uninterpreted functions and predicates, rejects
Real-valued symbols and Real arguments, and retains the existing QF_LIA
operator subset: integer `div` and `mod` remain unsupported. Mixed purification
uses memoized Int proxies in both directions. Models combine the final integer
tableau values with the ground EUF function and predicate tables, then replay
the ordered preprocessing reconstruction chain. Model construction also
evaluates original arithmetic argument terms left in the hash-consed arena by
purification and prefers the constrained purified application when two ground
applications print with the same argument tuple. Without both rules, a stale
unconstrained application could shadow the EUF-constrained table entry; the two
fixed TwoSquares regressions are validated directly by Dolmen.
Solved-constant definitions are captured before purification, so a definition
may still contain an original UF application while the integer tableau owns
only its later proxy. Postsolve reconstruction rebuilds the accepted final EUF
classes, indexes exact tableau values by class, and uses those values when it
evaluates compound Int definitions. This is model-only completion, not theory
propagation: it combines true equalities from the final EUF assignment with
terms that have the same exact value in the accepted LIA model, and it rejects
an EUF disequality that conflicts with those values. The reduced regression
uses `x = -7 + 4*f(a)`; the representative MathSAT/Wisa corpus case that exposed
it is checked directly by Dolmen in the evaluation record.

The existing production coordinator is sequential and currently instantiates
EUF plus LIA only.
`QF_UFLRA` retains its established specialized convex coordinator, options,
counters, and model behavior by default. With
`--experimental-generic-model-combination`, checked LRA and EUF projections are
adapted to `TheorySolver`, and the generic coordinator owns snapshot validation,
partition comparison, mutation dispatch, and model-choice batching. The EUF
adapter deliberately reuses the established checkpointed compatibility merge;
the experiment changes the scheduler boundary, not that component operation.
The complete specialized path remains available while equivalence evaluation is
in progress. The generic coordinator currently assumes that every supplied
shared sort belongs to every participating component; multi-way combinations
with partially overlapping sort interfaces remain future work. Arrays,
bit-vectors, nonlinear arithmetic, finite shared sorts, and quantifiers remain
unsupported.

Protocol tests use scripted convex and non-convex components. They cover stale
and incomplete snapshots, valid and rejected mutation, care-pair ordering,
candidate equality decisions, explained propagation, non-convex decisions and
clauses, batched model choices, cancellation, and escaping component exceptions.
These scripts do not replace a SAT solver or validate CDCL behavior; production
migration tests use CaDiCaL. QF_UFLIA solver tests cover purification, complete
arrangements, reversible model choices, bounded complete fallback, agreeing
projections, the non-convex disjunction case, combined models, empty formulas,
and frontend sort rejection. Smoke models are validated directly by Dolmen. On
implementation commit `4b0ad55`, a local
3,000-case generated QF_UFLIA campaign agreed with both Z3 and cvc5, with no
timeouts, unsupported results, crashes, or frontend errors; Dolmen directly
accepted all 1,686 satisfiable models. The tactic-composition matrix passed all
174 runs across its five logics and eleven applicable profiles, including 28
QF_UFLIA runs and the sequential Boolean-splitting profiles, without
non-timing-statistic drift. A separate 1,000-case QF_UFLRA preservation run
agreed with Z3 and Dolmen accepted all 597 satisfiable models.

The production QF_UFLIA tests also force several sparse interface-pair
materializations and check that the solve-scoped component preparation performs
one full build followed by incremental EUF/LIA atom growth. The online callback
rebuild count remains independently visible, preventing the test from
mistaking prepared-component reuse for a fully incremental SAT callback.

On exact binary commit `3bb19c7`, the experimental QF_UFLIA route passed a
four-shard SLURM campaign of 3,000/3,000 Z3 comparisons; Dolmen directly
accepted all 1,778 SAT models and the other 1,222 cases were unsat. The generic
QF_UFLRA route separately passed 1,000/1,000 comparisons with 595 directly
validated SAT models, plus 100 finite-domain QF_UFLRA and 100 QF_UF controls.
All eight tasks exited successfully with empty stderr logs. The exact commands,
seeds, counts, and artifact paths are recorded in [](run-log.md). This is
correctness evidence for default-off routes, not a performance or default
promotion claim.

The later sparse-online implementation was promoted at `f38446a`. A fresh
four-shard SLURM gate exercised the public default with no enabling options:
3,000/3,000 QF_UFLIA answers agreed with Z3, and Dolmen directly accepted all
1,773 satisfiable models. An isolated 30-second sample run solved 19/20 (9 sat,
10 unsat, one timeout), with no wrong or unsupported result; the previous
complete and model-only controls solved 11/20. On the same exact commit, a
1,000-case general QF_UFLRA preservation gate and two 100-case finite-domain
controls passed, with all 628 satisfiable QF_UFLRA models accepted directly by
Dolmen. The full commands, seeds, family counts, job identifiers, and artifacts
are recorded in [](run-log.md). A subsequent YinYang/typefuzz campaign on
revision `66250a4` (documentation-only changes after the same solver source)
processed all 10 selected real MathSAT QF_UFLIA seeds, generated 20 successful
mutations per seed, made 388 Primo/Z3 solver calls, and found no bug trigger or
bug file.

A crossed-order, full-corpus SLURM comparison evaluated the specialized and
generic QF_UFLRA coordinators twice on all 1,284 archive files with a
120-second per-file cutoff. Each shard ran both arms on the same node, and the
second campaign reversed every shard's arm order. Both arms solved the same
1,272 files (761 sat and 511 unsat), timed out on the same 12 files, and had no
wrong, unsupported, error, recovery, loss, or decisive-disagreement row. After
averaging each arm across both orders, common-solved time was 800.57 seconds
for specialized and 802.71 seconds for generic (+0.27%); PAR2 was 3,680.57 and
3,682.71 (+0.06%). The first campaign alone misleadingly showed +2.08% solved
time because the cold first-arm penalty fell on the heavier generic-first
shards; reversing the order changed the apparent result to -1.57%.

The generic coordinator was active on 755 solved rows. A pinned local gprof
comparison of the largest repeatable active-row delta did not reproduce an
infrastructure slowdown: generic took 17.07 seconds and specialized 17.27
seconds. Both profiles were dominated by LRA tableau checks, exact-rational
operations, pivots, and CaDiCaL propagation; two generic component checks were
below gprof's sampling resolution. The retained active-row watchlist is
`benchmarks/qf_uflra-generic-combination-watchlist.txt`. It is a sensitivity
set for future changes, not evidence of a current generic bottleneck. The
specialized path remains the default pending a separate default-selection
decision.

## Incremental QF_UFLIA implementation checklist and status

The implemented QF_UFLIA milestone combines four primary-source obligations
rather than treating online checking as an implementation-only optimization:

- de Moura and Bjorner, *Model-based Theory Combination*, Sections 4--5 and
  Appendix A, supplies the separation between explained `T-Propagate`,
  reversible `M-Propagate`, model mutation, and backtracking. Primo keeps a
  component model private and exposes only a revision-tagged equality
  projection. A model equality remains a decision, never an entailed clause.
- Bruttomesso et al., *Delayed Theory Combination vs. Nelson-Oppen*, Section 3
  and Theorem 2, supplies the complete non-convex backstop. Until the sparse
  online path has a separate completeness proof, every inconclusive sparse run
  restarts a total SAT-visible arrangement.
- Jovanovic and Barrett, *Sharing is Caring: Combination of Theories*, Sections
  3--4, supplies theory care functions and the EUF application-argument care
  graph. The first implementation uses the graph only to order model choices;
  it does not prune the non-convex fallback.
- Dutertre and de Moura, *A Fast Linear-Arithmetic Solver for DPLL(T)*,
  Sections 3.2--3.3, supplies incremental bound assertion, cheap backtracking,
  feasibility checks, and conflict explanations. Integer patching and cuts are
  run only at full-model effort on a copy because those operations are not part
  of the trailed rational tableau invariant.

The implemented data ownership is an SMT-owned external-propagator dispatcher,
one theory-owned incremental state per component, and SAT-owned levels and
assignments. Assignment callbacks apply only newly assigned theory literals;
level callbacks push or pop component checkpoints. Partial checks may return
only explained propagation/conflict or a typed stop. A full-model check may
additionally return a model projection or request a reversible equality
decision. Components never receive a SAT backend, and the dispatcher never
inspects closure or tableau internals.

The expected costs are one-time preparation plus work proportional to newly
assigned or backtracked literals. EUF care generation may inspect application
definitions within compatible function classes; it must not enumerate every
shared-term pair merely to discard most of them. The retained complete fallback
is still quadratic in the number of shared Int terms.

The proof obligations are: every permanent clause has a component explanation;
candidate equalities remain reversible; component state after pop represents
exactly the retained SAT prefix; integer completion satisfies every asserted
arithmetic literal; accepted EUF and LIA projections agree; cancellation and
resource limits remain typed; and the total arrangement still decides the
supported non-convex fragment. Unit and production-migration tests exercise
these obligations; the complete fallback remains part of the soundness and
completeness boundary.

## Historical QF_UFLIA performance diagnosis (2026-09-02)

This section records the measured preimplementation baseline at exact source
commit `bc4667d`. The sparse-online work described above subsequently addressed
the repeated component reconstruction, care-pair, and fixed-budget items. The
remaining gaps are called out explicitly below; historical counts and option
defaults are not descriptions of the current `f38446a` solver.

The fixed 20-file reference sample exposed eight rows that Z3, cvc5, and
OpenSMT all solve in under five seconds while both Primo policies reach the
30-second limit. The watchlist is
`benchmarks/qf_uflia-reference-gap-watchlist.txt`. Counters over those eight
default-policy runs total 91,070 complete SAT assignments, 112,955 component
checks, and 91,070 learned component-conflict clauses. The inputs have from 45
to 216 shared Int terms. The complete policy consequently creates from 990 to
23,220 interface equalities, plus three arithmetic bridge clauses per pair.
This is repeated full-assignment rejection, not one expensive final integer
branch-and-bound search.

Pinned, conflict-bounded gprof runs on exact source commit `bc4667d` separate
the costs:

| Instance and policy | Shared pairs | Bounded rounds | Wall / peak RSS | Dominant measured work |
| --- | ---: | ---: | ---: | --- |
| `wisas/xs_7_7`, complete | 990 | 5,000 | 4.38 s / 15 MiB | CaDiCaL propagation, then exact-rational pivots and updates |
| `wisas/xs_27_37`, complete | 7,750 | 1,000 | 5.30 s / 47 MiB | CaDiCaL model search/propagation, 31.8 million full-model value queries, and rebuilt EUF closure work |
| `Hash/hash_sat_09_05`, complete | 1,653 | 3,000 | 2.16 s / 18 MiB | CaDiCaL propagation; every round is rejected by the first EUF check |
| `EufLaArithmetic/medium/medium12`, complete | 23,220 | 200 | 4.23--8.68 s / 122 MiB | CaDiCaL propagation and copying/updating/destructing a fresh arithmetic tableau |
| Certora `65782...`, complete | 12,090 | 1,500 | 11.99 s / 69 MiB | CaDiCaL propagation (82.9% of samples) and 74.6 million model-value queries |
| `Hash/hash_sat_09_05`, model-guided | 0 materialized | 1,000 | 4.56 s / 13 MiB | A freshly copied rational tableau and exact arithmetic; LIA rejects every Boolean model before a combination choice exists |

The `medium12` row includes the original profile and a fresh exact-command
recheck; the roughly twofold wall-time variation reinforces that these local
runs identify mechanisms rather than establish comparative timing. The exact
percentages are sampling data, not portable predictions. The common
mechanism is nevertheless clear from both counters and profiles. The complete
route pays quadratic SAT structure and then asks for every component atom's
value after each complete Boolean model. EUF reconstructs a congruence closure
for the assignment, while LIA copies the prepared tableau and reasserts all
bounds. The then-guided route avoided the eager pair set initially, but its
default budget of 16 model-choice batches sent seven of these eight rows back
to the same complete route. On the remaining Hash row, rational feasibility fails
before EUF projection or an integer model choice, so sparse combination alone
cannot help.

A local classification probe raised the experimental budget without changing
the algorithm. Budget 256 solved three previously timed-out watchlist rows
without complete fallback: `xs_7_7` was sat in 0.03 seconds after materializing
32/990 pairs, MathSAT Wisa `xs-15-09-3-5-5-3` was sat in 1.84 seconds after
216/2,926 pairs, and Wisa `xs-07-11-1-1-1-1` was unsat in 0.14 seconds after
64/990 pairs. Dolmen directly accepted both satisfiable models. Five rows still
reached their 10- or 15-second local limits. These are diagnostic, single-run
local results: they motivate an adaptive budget experiment but do not justify
changing the default or claiming sample-level performance.

### Technique queue and current status

1. **Deeper `DPLL(T^M)` integration and theory layering (partially
   implemented).** The promoted path now retains EUF and rational LIA state
   during Boolean search and performs explained partial-assignment checks. The
   model-based coordinator still runs after a complete SAT model. The
   transition system in de Moura and Bjorner, and the partial-assignment
   schedule in delayed theory combination, still motivate moving model
   reconciliation itself into the search and separating cheap rational checks
   from full integer completion.
2. **Persistent growth of EUF and LIA contexts (partially implemented).**
   Assignment deltas and push/pop are incremental while the interface is
   fixed. Materializing a new sparse equality still rebuilds both prepared
   components because the tableau cannot yet grow rows after its first SAT
   level. Supporting delta row and atom registration is the next infrastructure
   target.
3. **Real EUF care pairs (implemented as an ordering heuristic).** Following
   *Sharing is Caring*, the EUF adapter now groups compatible applications and
   proposes shared argument pairs. Care pairs prioritize sparse model choices;
   they are deliberately not a pruning or completeness argument, so the
   complete non-convex fallback remains.
4. **Adaptive model-choice scheduling and arithmetic model mutation
   (partially implemented).** The promoted 512-choice budget fixes the measured
   premature fallback without claiming a universally adaptive policy. Current
   QF_UFLIA adapters expose projections but no live arithmetic `M-Mutate`
   implementation; progress-sensitive scheduling remains open.
5. **Integer finite-domain preprocessing and channeling (experimental subset
   implemented).** The Hash, Wisa, and `medium12` inputs encode small numeral
   domains, permutations, or explicit function tables. Explicit `strong` now
   recognizes disjunctive domains of distinct Int numerals and emits the
   existing equality definitions and pairwise at-most-one clauses. It excludes
   numeral meanings from UF symmetry and exposes separate activation counters.
   Compact cardinality encodings, all-different recognition, and default/auto
   promotion remain open and depend on the evaluation below.
6. **Stronger explained propagation and smaller conflicts.** Tens of thousands
   of complete assignments currently produce one clause each. Opportunistic
   fixed arithmetic equalities, EUF equalities, bounds, and conflicts at partial
   assignments should prevent the same bad region from being rediscovered.

The remaining arithmetic waterfall gaps documented in
[](papers/arithmetic-solving-in-z3.md)—extended bounded GCD, stronger cube and
bound reasoning, CDCL-visible integer branches, broader Gomory eligibility,
cut selection, and HNF cuts—remain important for QF_LIA completeness and hard
integer models. They are a second line for this particular watchlist: the
profiles are dominated before those stages, usually by SAT/interface work,
EUF rejection, or rational-tableau feasibility. Dynamic Ackermannization is
also not the first general remedy here: the model-combination paper reports
that it did not help its Wisa family and could add overhead, although a
separate Hash-specific experiment may still be worthwhile.

The reproducible commands, exact bounds, artifacts, and model-validation
result are recorded in [](run-log.md). This diagnosis did not itself justify a
default change; the later promotion used the independent correctness,
model-validation, preservation, mutation, and sample gates summarized above.

## Full-corpus consensus-gap profiles (2026-09-03)

The first full-corpus run identified 54 inputs that Primo timed out on while
Z3, cvc5, and OpenSMT all returned the same decisive answer. The fixed
`benchmarks/qf_uflia-full-consensus-gap-profile-4.txt` sample represents four
different observed paths. Reference times below come from the 60-second SLURM
comparison at exact solver commit `61cd9ee`; Primo phase counts are from that
same run. The bounded local gprof profiles use solver-equivalent commit
`03ed5cb`, whose intervening changes affect only result-reporting scripts,
tests, and documentation.

| Input | Reference times: Z3 / cvc5 / OpenSMT | Primo's measured path |
| --- | ---: | --- |
| `medium11` (unsat) | 0.025 / 0.034 / 0.037 s | The 512 sparse decisions do not settle the formula. Complete fallback materializes all 17,578 shared pairs into 70,980 SAT variables and 106,155 clauses; 57.1 of 60.1 seconds are in that fallback, with 36.0 million SAT decisions. |
| `xs_33_33` (sat) | 0.039 / 0.076 / 1.700 s | The sparse phase learns two component clauses without proposing a model equality, then complete fallback creates 11,026 pairs. SAT/online callbacks consume 59.4 seconds and perform 11.0 million decisions, 18,774 EUF theory conflicts, and 6.28 million online LRA checks. |
| `xs_40_40` (sat) | 0.043 / 0.075 / 5.154 s | Sparse search remains active but materializing 289 pairs causes 290 component rebuilds, 54,582 SAT conflicts, 343,819 pivots, and 203.0 million pivot-term operations. SAT and its online callbacks account for 58.2 seconds. |
| Certora `3106...40` (sat) | 0.197 / 0.286 / 1.067 s | Only 141 of 1,619,100 possible pairs are materialized, but they cause 142 rebuilds. Integer completion consumes 30.3 seconds and the complete LIA check 33.9 seconds; the ten-second profile attributes 27.1% inclusive time to integer completion and 24.6% to patch-candidate collection. |

The gprof runs add two actionable infrastructure observations. On `xs_33_33`,
`FlatCongruenceClosure::drain_changed_terms` accounts for 59.6% of samples and
is called 1.56 million times in ten seconds. Each call currently resets the
changed-term watermark in every active checkpoint, so the operation is linear
in SAT checkpoint depth even when few terms changed. On `xs_40_40`, CaDiCaL
propagation and subsumption account for 34.7% of samples, followed by tableau
pivots and exact-rational bound maintenance. On the complete-fallback
`medium11` profile, CaDiCaL propagation leads the flat profile while online EUF
and LRA callbacks execute millions of times.

Preprocessing was cheap in the recorded baseline, but it was also intentionally
narrow. At commit `61cd9ee`, QF_LIA and QF_UFLIA forced the same
substitution-only profile regardless of `off`/`basic`/`strong`/`auto`, so the
controlled preprocess-only runs produced identical structural counters. The
mandatory equation pass accepted 1/2 candidates on `medium11`, zero on both
wisas inputs, and 13/95 on the Certora input. Arithmetic normalization and
purification increased reachable terms from 645 to 1,088, 1,503 to 2,331,
1,803 to 2,799, and 50,054 to 63,356 respectively. Optional arithmetic-atom
canonicalization did not reduce the resulting SAT instance or improve
five-second search progress; it slightly enlarged both wisas DAGs and added
about 36 ms of preprocessing on Certora.

The measured priority order is therefore: make changed-term draining
amortized rather than checkpoint-depth-linear; expose integer-safe Boolean and
local-context preprocessing for the nested wisas implications and guarded
counter recurrences; recognize explicit numeral domains/function tables before
constructing complete arrangements; and avoid rebuilding and rescanning the
integer tableau for every sparse equality. These are profiling leads, not
implemented improvements or performance claims.

The first preprocessing part of that lead is now the evaluated QF_UFLIA
default. QF_UFLIA `auto`, plus explicit `basic` for either integer logic, adds a
non-distributing, two-polarity NNF
pass and the integer-safe Boolean/value-propagation round; the generic Real and
UF flags remain disabled. The complete 659-file comparison improved from 549
solves and 15,146.0 PAR2 on the same revision's substitution-only path to 571
solves and 11,735.7 PAR2. Pure QF_LIA `auto` remains substitution-only, and
integer numeral-domain recognition remains an explicit `strong` experiment.

### Amortized changed-term notification draining

`FlatCongruenceClosure` now separates rollback of undelivered notifications
from the fact that a notification batch has already been consumed. Each
checkpoint records the changed-term vector size and a monotonic drain
generation. A pop restores the saved vector prefix only when its generation is
still current; a drain advances the generation in constant time instead of
rewriting every active checkpoint. If a descendant drained the batch, popping
cannot resurrect either ancestor or descendant notifications. The generation
counter has an explicit overflow rebase, whose checkpoint scan is reachable
only after `2^64` drains.

The vector remains owned by the theory closure, while the online EUF
propagator remains the sole consumer. No SAT literals or SMT terms enter the
closure. Unit tests cover undrained parent prefixes, nested drains, and pop
after a descendant drain. `:euf.incremental.changed-term-drains`,
`:euf.incremental.changed-terms-delivered`,
`:euf.incremental.changed-term-generation-rebases`, and
`:euf.incremental.max-checkpoint-depth` expose the mechanism. The diagnostic
timers `:time.qf-uflia.component-rebuild-us` and
`:time.qf-uflia.online-component-rebuild-us` support the later decision about
incremental component growth. This is an infrastructure correction to the
existing Nieuwenhuis--Oliveras-based closure, not a new congruence-closure
algorithm or a performance claim by itself.

## Statistics

`:solver.qf-uflia` selects the new leaf. `:combination.components`,
`:combination.shared-terms`, `:combination.arrangement-pairs`, and
`:combination.arrangement-bridge-clauses` describe the static combination.
`:combination.rounds`, `:combination.component-checks`,
`:combination.learned-clauses`, and `:combination.resource-limits` describe the
sequential search. Existing QF_UFLRA statistic names and meanings are unchanged.
The experimental generic QF_UFLRA path additionally publishes cumulative
`:combination.model.*` counters for component checks, snapshots, mutations,
fixed points, care pairs, disagreements, learned clauses, model decisions,
fresh decisions, stale snapshots, and rejected mutations.
For QF_UFLIA, `:combination.model.total-pairs`, `.materialized-pairs`, and
`.complete-fallbacks` distinguish the sparse phase from the complete backstop;
`.fallback-budget`, `.fallback-unsupported`, `.fallback-unknown`, and
`.fallback-resource-limit` classify why that backstop was entered. The shared
assumption-lifecycle counters use the existing
`:no.interface.model-equality-*` namespace.

`:time.qf-uflia.sat-us`, `.assignment-extraction-us`, `.euf-check-us`,
`.lia-check-us`, `.lia-rational-us`, `.lia-integer-us`,
`.model-reconciliation-us`, and `.complete-fallback-us` are cumulative local
phase timers. They are diagnostic and excluded from non-timing-statistic
compatibility comparisons.

## Known rough edges in the coordinator

Recorded so the next theory to plug into this layer is not surprised by them.
None affects a reported answer today.

- `pair_key` packs two `TermId`s into 64 bits with a 32-bit shift. Nothing
  currently checks that `TermId` stays below 2^32, and if it ever exceeds that
  the pair set silently starts aliasing distinct pairs.
- `statistics_.mutations` is incremented before `mutate` is called, so it
  counts mutation *requests*. A component that answers `FixedPoint` is counted
  as both a mutation and a fixed point.
- One reconciliation round asks each non-reference component for at most one
  mutation and never re-asks a component after a later one mutates. That is
  sound -- any residual disagreement becomes an equality decision -- but it is
  not a mutual fixed point.
- `EufComponentAdapter` and `LiaComponentAdapter` do not override `mutate`, so
  on the default QF_UFLIA path the whole mutation block is inert and
  combination proceeds by the split loop alone. The mutation path is live only
  under `--experimental-generic-model-combination`.
