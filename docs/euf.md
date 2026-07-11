# EUF And Congruence Closure

Primo's first theory target is QF_UF. The default implementation is the
incremental Nieuwenhuis-Oliveras-style congruence-closure engine over flat
terms. The older lazy EUF solver over `core::TermId` values is still available
with `--euf-engine=lazy` for diagnostics.

## Current Solver Path

SMT-LIB assertions are parsed into hash-consed terms. Boolean structure is
clausified into SAT clauses. Equality atoms and Boolean predicate applications
are tracked as theory atoms.

The QF_UF loop in `src/smt/check_sat.cpp` is:

```text
clausify assertions
set the default SAT phase of tracked theory atoms to false
loop:
  call CaDiCaL
  if SAT says UNSAT, return UNSAT
  read assigned theory atoms
  rebuild congruence closure for the assignment
  if all assigned disequalities are consistent, return SAT
  add a blocking theory-conflict clause and repeat
```

The current conflict clause is justified by the explained congruence closure.
Reasons are sound but not guaranteed minimum; the optimized proof-forest
structure from the fast congruence-closure literature is still deferred.

The SAT phase setting is a search heuristic only. It asks CaDiCaL to try the
negative phase for equality and Boolean-predicate theory atoms before the SAT
solver's own activity heuristics override that choice. This is useful for
finite-domain QF_UF encodings with many disequality constraints and value-choice
disjunctions, but it does not assert or propagate any theory fact.

`--euf-engine=incremental|lazy` selects the EUF checker used for final conflicts
and propagation reason queries. `incremental` is the default. The incremental
engine is conservative: it uses the paper-style flat data structures, while
final model checks and model-time propagation still rebuild equality state from
the complete SAT assignment.

`--euf-prop=off|conflict|model|online`, `--euf-prop-budget=auto|unlimited|N`,
and `--euf-prop-delivery=auto|unlimited|N` are the public option shape for
callback and propagation work. The default is `online`; use `conflict` to keep
the assignment/backtrack callback and external conflict clauses while disabling
online theory-literal propagation, and use `off` to force the older post-model
conflict loop. The default delivery budget is `1000`, which matches the
predecessor benchmark command.

For compatibility with the predecessor benchmark command, Primo also accepts
`--prop-delivery-budget N`; `0` means unlimited delivery. `--prop-interval N`
and `--prop-assign-threshold X` are accepted for online propagation experiments.
Their defaults are `32` and `0.25` respectively.

`conflict` connects the CaDiCaL external propagator only for assignment,
backtrack, and external conflict clauses. It maintains the incremental flat
congruence closure along the SAT trail, learns EUF conflict clauses through
CaDiCaL's external-clause callback, and does not deliver theory propagations.
The ordinary final model check remains as a safety net if SAT reports a model.
This mode is intended for benchmarks where rebuilding a full closure after each
candidate SAT model dominates runtime.

The callback path was first changed to `conflict` after the full QF_UF SLURM run recorded in
`results/slurm-qfuf-full-109880/summary.md`: at a 2s timeout it solved 5455 of
7503 files with no wrong answers, compared with 4778 for the previous
phase-negative/default baseline. The result is mixed by family: QG gained 684
solved files, while Goel lost 21 solved files, so family-level reports should
remain part of future tuning.

The legacy preprocessing switches were promoted to defaults after the full
QF_UF 10s comparison in SLURM jobs 109889, 109890, and 109891. The safe
configuration at that point, using `conflict` callbacks plus `--uf-symmetry-breaking`,
`--preprocess-passes 4`, `--eq-bridge`, `--prop-interval 32`,
`--prop-assign-threshold 0.25`, and `--prop-delivery-budget 1000`, solved 6647
of 7503 files with no wrong answers. The previous default solved 6611.

Online propagation was then fixed to cache reason clauses by signed propagated
literal and keep those global clauses across SAT backtracking. Before that fix,
SLURM job 109891 had 18 errors and one wrong answer. After the fix, matching
SLURM job 109913 solved 6684 of 7503 files at 10s with no errors and no wrong
answers, so `online` is now the default.

`model` is implemented as a conservative post-model lemma learner. After each
SAT model, Primo rebuilds EUF closures that omit one candidate theory atom at a
time. If the remaining assigned equalities and Boolean predicate facts already
imply the candidate's current value, Primo adds a SAT clause from the explained
premise literals to the candidate literal. With `--euf-engine=lazy`, this uses
the older explained closure. With `--euf-engine=incremental`, it uses the flat
curried incremental closure and exports the `:euf.incremental.*` counters for
that propagation work. Reasons are sound, but not guaranteed minimum.

`online` connects a CaDiCaL external propagator through the SAT adapter. It
observes only EUF-relevant SAT variables: equality atoms and Boolean predicate
applications. The propagator tracks assignments and backtracking notifications,
then asks for entailed unassigned theory literals and their reason clauses.
With `--euf-engine=lazy`, reason queries use the older explained closure. With
`--euf-engine=incremental`, they use a persistent flat curried closure owned by
the external propagator. The propagator pushes a flat-closure snapshot when SAT
enters a new decision level, asserts assigned theory literals as CaDiCaL reports
them, and pops the closure on SAT backtrack. This is still an initial online
subset: it does not choose decisions. Like `conflict`, it can serve EUF
conflict clauses through CaDiCaL's external-clause callback.

## Term Interface

The equality solver receives only `core::TermId` values. It inspects structure
through `core::TermManager::get`; it does not own term memory and it does not
refer to SMT-LIB syntax.

Syntactic equality and EUF equality are distinct:

- equal `TermId` values mean the terms are structurally identical after
  hash-consing and local simplification;
- EUF equality is the congruence relation computed from assigned equality
  literals.

## Congruence Closure

`src/theory/equality/congruence_closure.*` stores:

- a `UnionFind` over term ids;
- `terms_seen_`, the registered terms and their children;
- `seen_`, a term-id-indexed membership bitmap;
- a dirty flag used by saturation.

`add_term(t)` recursively registers `t` and its children. `assert_equal(a, b)`
registers both sides and merges their classes. `are_equal(a, b)` saturates the
closure and then compares representatives.

The saturation algorithm is intentionally simple:

```text
while a merge happened:
  for every pair of seen terms:
    if they are not already equal and are congruent:
      merge them
```

Two terms are considered congruent when they are both constants or applications
with the same symbol and their corresponding children are already equal. This is
quadratic over seen terms and rebuilds from each SAT assignment; it is adequate
for the scaffold but not the final QF_UF engine.

`src/theory/equality/explained_congruence_closure.*` is the first
explanation-capable EUF state. It keeps a union-find plus an explicit equality
edge graph. Explicit assigned equalities add edges labelled by the corresponding
SAT blocking literal. Congruence merges add edges labelled by the union of child
equality explanations. This is not the optimized proof-forest structure from
Nieuwenhuis and Oliveras, but it provides independently checkable reason
literals for the current lazy conflict clauses and propagation lemmas.

`src/theory/equality/incremental_congruence_closure.*` is a direct `TermId`
prototype of the Section 3 data structure:

- representatives in `parent_`;
- class lists and class sizes in `class_members_` and `class_size_`;
- affected-application use lists in `use_lists_`;
- sparse signature lookup in `lookup_`;
- pending explicit and congruence merges in `pending_`;
- disequality conflict checks in `disequalities_`, with per-representative
  index lists in `disequality_lists_`;
- non-minimal explanation paths in `edges_`;
- coarse `push`/`pop` snapshots for backtracking tests.

`src/theory/equality/flat_terms.*` and
`src/theory/equality/flat_congruence_closure.*` are the SMT-facing incremental
path. `FlatTermIndex` curries applications into binary internal apply
definitions and maps original equality atoms to flat endpoints.
`FlatCongruenceClosure` runs the same representative, class-list, use-list,
lookup, pending-merge, disequality, explanation, and coarse backtracking
structure over those flat definitions. Explanation edges are stored both in
insertion order and in per-flat-term adjacency lists so reason BFS visits only
edges incident to the current proof-graph node. A newly asserted disequality is
checked immediately. After a class merge, older disequalities are not scanned
globally; the closure collects the disequality indices attached to the two
merged representatives, sorts those indices to preserve original insertion
order, and checks only that candidate set. This keeps the first reported
conflict deterministic while avoiding scans over disequalities whose endpoint
classes did not change. This is the engine used by
`--euf-engine=incremental`.

## EufSolver

`src/theory/equality/euf_solver.*` is a batch checker:

- `assert_equal(lhs, rhs)` records an equality;
- `assert_distinct(lhs, rhs)` records a disequality;
- `is_satisfiable()` rebuilds a `CongruenceClosure`, applies equalities, and
  rejects the assignment if any recorded disequality becomes equal.

The diagnostic lazy SMT loop uses `ExplainedCongruenceClosure` so conflict and
propagation clauses carry reason literals rather than all assigned theory
premises.

## Preprocessing Interaction

The CLI default `--qf-uf-preprocess=auto` pass runs before QF_UF solving. On
single-assertion formulas it runs equality bridging and unit-equality
simplification but skips finite-domain clause expansion. On multi-assertion
formulas it uses the `strong` pipeline, which appends bridge equalities,
finite-domain at-most-one clauses, finite-domain equality definitions, and
value-precedence assertions when their recognizers can prove the required local
conditions. `--qf-uf-preprocess=basic` keeps only the unit-equality
simplification subset, `strong` always enables the full finite-domain pipeline,
and `off` disables this loop.
`--ground-congruence` additionally enables bounded ground-congruence clauses.
These generated assertions are then handled by the same Boolean clausifier and
congruence-closure loop as ordinary user assertions. `--uf-symmetry-breaking`
and `--eq-bridge` remain available as narrower experimental switches.

See [](preprocessing.md) for the finite-domain symmetry-breaking design.

## Implemented And Deferred

Implemented:

- union-find over `TermId`;
- recursive term registration;
- congruence closure for constants and applications;
- equality and disequality checking;
- explanation graph for explicit and congruence equality reasons;
- deterministic Curry-style flat definitions at the equality-theory boundary;
- opt-in use-list, signature-lookup, pending-queue congruence closure for
  final conflict checks;
- order-preserving flat disequality scans over per-class candidate lists;
- model-time propagation using the flat incremental closure when
  `--euf-engine=incremental` is selected;
- online propagation with a SAT-callback-owned persistent flat closure when
  `--euf-engine=incremental` is selected;
- incremental Boolean/EUF integration with CaDiCaL, with the older lazy engine
  kept as a diagnostic option;
- Boolean predicate applications participating in EUF by equality to
  `true`/`false`;
- model-time EUF propagation lemmas with deterministic inspection and delivery
  budgets;
- default online EUF propagation through CaDiCaL observed-variable callbacks;
- auto QF_UF preprocessing by default;
- strong preset with finite-domain AMO clauses, equality-definition clauses,
  default-on UF symmetry breaking, and opt-in bounded ground-congruence clauses.

Deferred:

- optimized explanation-producing proof forests;
- richer online conflict-clause injection;
- minimal theory-propagation reasons;
- proof output for preprocessing-generated lemmas;
- model construction.

## Paper Traceability

Nieuwenhuis and Oliveras, *Fast Congruence Closure and Extensions*, motivates
the EUF direction and the eventual data structures. Primo now implements the
Section 3 use-list, lookup, pending-merge shape as an opt-in conflict and
propagation checker over deterministic Curry-style flat definitions. Online
incremental propagation owns a persistent flat closure and uses SAT
decision-level push/pop callbacks. Final conflict checks and model-time
propagation still rebuild flat closures from complete assignments. Primo does
not yet implement the Section 4-5 proof-producing union-find.

Deharbe et al., *Exploiting symmetry in SMT problems*, and Reger and Suda,
*Using SAT and SMT Solvers for Finite Model Finding with Sorts*, motivate the
default-on finite-domain symmetry-breaking preprocessing pass described in
[](preprocessing.md).
