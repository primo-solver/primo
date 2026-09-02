# Modular theory combination

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
`TheorySolver::care_pairs` optionally identifies the pairs most relevant to the
component. Default implementations report a mutation fixed point and no care
pairs, so components opt into these operations explicitly.

The SMT coordinator owns purification, atom ownership, interface discovery,
equality materialization, SAT scheduling, and combined-model construction.
`sat/` continues to see clauses and literals only. `core/` continues to own all
terms and sorts.

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

The default `QF_UFLIA` policy uses delayed combination. For `n` shared Int terms
it materializes all `n(n-1)/2` equality choices. Each equality is connected to
the arithmetic component by two non-strict inequalities and Boolean clauses
expressing equality iff both inequalities hold. A false equality therefore
lets SAT select one strict integer orientation. This quadratic baseline is
intentional: it supplies a simple completeness argument for a non-convex
component. The experimental model-guided policy starts with only input-visible
interface equalities, lazily adds model disagreements, and restarts this exact
complete baseline if its decision budget is exhausted or a component check is
inconclusive.

`QF_UFLRA` does not use this eager policy. Its existing lazy convex equality
exchange, exact fallback, model mutation, reconciliation, and model builder
remain in its specialized coordinator.

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
model class rather than materializing the quadratic cross-product. Care pairs
are considered first. If a disagreement remains, the coordinator returns one
or a component-provided batch of equality decisions; the caller creates or
resolves their SAT literals and re-enters CaDiCaL. Entailed
literals are converted to explained clauses, while general non-convex clauses,
fresh decisions, cancellation, resource limits, unsupported cases, and unknown
results remain distinct typed actions. Exceptions are not converted into
solver answers.

The coordinator owns no SAT backend and no theory-private model. Its caller
owns assignment-epoch advancement, dynamic equality registration, assumptions,
failed-assumption clauses, and re-entry into CaDiCaL. Thus ordinary Appendix A
DPLL operations remain delegated to the production CDCL backend. The detailed
ownership and transition checklist is recorded in
[](papers/model-based-theory-combination.md).

For QF_UFLIA, `--experimental-qf-uflia-model-combination` projects the checked
integer tableau and EUF closure onto all shared Int terms. A partition
disagreement creates a positive, reversible CaDiCaL assumption. If the
assumption fails, the coordinator learns the negation of CaDiCaL's failed
assumption subset; it never learns a candidate equality merely because one
model selected it. Agreeing complete projections witness a combinable model.
Because LIA is non-convex, the policy does not use convex equality propagation:
`--qf-uflia-model-decision-budget` bounds model-choice batches (default 16),
and zero selects the complete arrangement immediately. Budget exhaustion,
unsupported projections, ordinary unknown results, and guided LIA resource
limits restart the unchanged complete-arrangement solver with no candidate
assumptions. Cancellation still stops immediately.

## Implemented subset and limitations

`QF_UFLIA` is runnable through a sequential two-component coordinator. Its EUF
adapter declares a convex, stably-infinite Int interface; its LIA adapter
declares the same interface stably infinite and non-convex. Both receive the
same complete `InterfaceRelation` vector, but enforce it only through their
owned SAT atoms. The LIA adapter reuses Primo's exact-rational tableau, integer
patching, GCD check, Gomory cuts, and bounded branch-and-bound. Reaching the
existing integer node limit returns a typed resource-limit `unknown`.

The experimental model-guided route rebuilds a component adapter only when a
new equality and its two arithmetic bounds are registered. Snapshot epochs are
advanced for every satisfying SAT assignment, so projections from older
assignments are rejected. The default complete route is unchanged and remains
the fallback; the feature is not a default or performance claim.

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

On exact binary commit `3bb19c7`, the experimental QF_UFLIA route passed a
four-shard SLURM campaign of 3,000/3,000 Z3 comparisons; Dolmen directly
accepted all 1,778 SAT models and the other 1,222 cases were unsat. The generic
QF_UFLRA route separately passed 1,000/1,000 comparisons with 595 directly
validated SAT models, plus 100 finite-domain QF_UFLRA and 100 QF_UF controls.
All eight tasks exited successfully with empty stderr logs. The exact commands,
seeds, counts, and artifact paths are recorded in [](run-log.md). This is
correctness evidence for default-off routes, not a performance or default
promotion claim.

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

## Incremental QF_UFLIA implementation checklist

The next QF_UFLIA milestone combines four primary-source obligations rather
than treating online checking as an implementation-only optimization:

- de Moura and Bjorner, *Model-based Theory Combination*, Sections 4--5 and
  Appendix A, supplies the separation between explained `T-Propagate`,
  reversible `M-Propagate`, model mutation, and backtracking. Primo will keep a
  component model private and expose only a revision-tagged equality
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

The planned data ownership is an SMT-owned external-propagator dispatcher, one
theory-owned incremental state per component, and SAT-owned levels and
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
supported non-convex fragment. Unit tests must exercise each obligation before
the online route is benchmarked.

## QF_UFLIA performance diagnosis (2026-09-02)

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
bounds. The guided route avoids the eager pair set initially, but its default
budget of 16 model-choice batches sends seven of these eight rows back to the
same complete route. On the remaining Hash row, rational feasibility fails
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

### Missing techniques, in measured priority order

1. **Incremental `DPLL(T^M)` integration and theory layering.** The current
   coordinator runs after a complete SAT model. The transition system in de
   Moura and Bjorner, and the partial-assignment schedule in delayed theory
   combination, instead retain component state during Boolean search, run cheap
   checks early, and make model equalities first-class reversible decisions.
   Primo still needs trail-synchronized component state, partial-assignment
   checks, and staged EUF/rational/integer checks.
2. **Persistent EUF and LIA contexts.** Component assignments should be
   applied as deltas under push/pop or a trail. Re-querying every atom and
   copying the whole closure/tableau on each round directly accounts for the
   observed tens of millions of operations. This is an infrastructure
   prerequisite for the first item, not a new solving heuristic.
3. **Real EUF care pairs.** *Sharing is Caring* restricts arrangements to
   theory-specific care graphs; for EUF, relevant pairs arise from arguments of
   applications that can still become congruent. `EufComponentAdapter`
   currently declares `supplies_care_pairs = true` but inherits the base method
   that returns an empty vector. The generic coordinator can prioritize care
   pairs, but QF_UFLIA supplies none. Implementing the EUF care graph and its
   invalidation rules should precede any attempt to use care pairs as a
   completeness argument; the complete non-convex fallback must remain until
   then.
4. **Adaptive model-choice scheduling and arithmetic model mutation.** The
   fixed budget 16 is prematurely destructive on three observed rows. A
   progress- or interface-size-dependent budget can be evaluated immediately,
   but the paper-backed endpoint is to mutate component models to reduce
   accidental equalities and postpone model-equality decisions until ordinary
   Boolean/theory propagation is exhausted. Current QF_UFLIA adapters expose
   projections but no live `M-Mutate` implementation.
5. **Integer finite-domain preprocessing and channeling.** The Hash, Wisa, and
   `medium12` inputs encode small numeral domains, permutations, or explicit
   function tables. Primo's finite-domain preprocessing is aimed at QF_UF
   candidate values and does not use the inherent distinctness of integer
   numerals; its counters are zero on this sample. Recognizing numeral domains,
   emitting compact equality definitions/at-most-one constraints, and exposing
   all-different structure could remove much of the Boolean search before
   combination.
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
result are recorded in [](run-log.md). No optimization or default change is
claimed by this diagnosis.

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
