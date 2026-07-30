# Nelson-Oppen Closure Methods

## Status

This is a literature and design-intake note for the
`nelson-oppen-closure-methods` branch. It now also records the first
implemented scaffold: read-only interface bookkeeping. It does not claim that
Primo implements Nelson-Oppen combination yet.

The intended project is to add theory-combination closure mechanisms based on
Nelson-Oppen-style equality sharing, with de Moura and Bjorner's
model-based theory-combination paper as the first implementation target. Before
any source change claims to implement one of these algorithms, this note must be
expanded into an implementation checklist mapping the paper operations to Primo
C++ types, invariants, tests, and counters.

## Current Primo Baseline

Primo currently has a QF_UF-focused DPLL(T) path and EUF congruence closure.
There is no documented general Nelson-Oppen combination layer in `docs/`, and a
repository search for "Nelson" or "Oppen" only finds retrospective or
literature references, not a current implementation note.

Primo now has a conservative QF_UF SMT-LIB `get-model` path for scripts that
set `:produce-models true`: `check-sat` can return a model string, and
`smt::ModelBuilder` prints definitions for user-declared UF constants,
functions, and predicates from a ground EUF closure over the accepted SAT
assignment. This is still narrower than the model object needed for
model-guided Nelson-Oppen combination. `core::Model` remains a placeholder, the
model builder is QF_UF-specific, and satisfiability-preserving preprocessing
must emit explicit reconstruction assignments when it removes user-visible
facts. The current QF_UF path reconstructs Boolean/forced unit facts,
solved-constant substitutions, and ground-unit substitutions before printing the
model.

This conservative model printer is a prerequisite for model-guided combination:
without a concrete model object, "model equality" can easily degrade into an
unexplained heuristic over the SAT assignment instead of the paper's operation
over candidate theory models.

The initial closure-method work therefore should not be framed as replacing an
existing general Nelson-Oppen module. It should introduce the minimum common
interfaces needed for future multi-theory cooperation while preserving the
existing ownership boundaries:

- `smt/` owns Boolean abstraction, theory atoms, assertion stacks, and DPLL(T)
  orchestration.
- `theory/` owns per-theory consistency, equalities, disequalities,
  propagation candidates, explanations, and model fragments.
- `sat/` sees only SAT literals and clauses.
- `core/` owns terms and sorts.

As of the current branch, `src/smt/theory_combination.cpp` implements only the
first bookkeeping slice. `analyze_nelson_oppen_interface` scans the current
post-preprocessing assertions, records user-visible non-Boolean constants by
sort, records already-visible equality atoms whose two sides are such
constants, and reports the same information through `check_sat` statistics:

- `:no.interface.terms`
- `:no.interface.sorts`
- `:no.interface.candidate-pairs`
- `:no.interface.registry-entries`
- `:no.interface.visible-equalities`
- `:no.interface.missing-visible-equalities`

This deliberately does not implement equality propagation, model-equality
guessing, care sets, purification, mixed-theory solving, or missing equality
atom creation. It is a measurement and API-shaping step while LRA remains on a
separate development branch. The scan is guarded by the explicit
`--nelson-oppen-diagnostics` CLI flag so ordinary benchmark runs do not pay for
extra interface traversal.

## Primary Base Paper

Leonardo de Moura and Nikolaj Bjorner. *Model-based Theory Combination.*
Electronic Notes in Theoretical Computer Science 198(2), 37-49, 2008.
DOI: <https://doi.org/10.1016/j.entcs.2008.04.079>.
SMT 2007 program entry:
<https://www.cs.upc.edu/~oliveras/smt07/home.html>.

### Relevance

The paper contrasts traditional Nelson-Oppen equality propagation with a
model-based alternative. Traditional combination either asks theory solvers to
produce all implied interface equalities or exposes the cross-product of shared
variable equalities to the SAT search. The paper's key implementation idea is to
inspect candidate theory models and propagate equalities that hold in those
models, then rely on backtracking if another theory disagrees.

For Primo this is relevant because:

- EUF can already build equivalence classes over ground terms.
- Future arithmetic and array solvers will need a shared-variable equality
  contract with EUF.
- Exhaustively enumerating shared-variable pairs risks creating too many SAT
  atoms before there is evidence that the equalities matter.
- Model-based guesses can be default-off while counters show whether they reduce
  Boolean search or only add churn.

### Implementation Questions For Primo

- Which terms are shared variables after purification: only named interface
  constants, or all purified proxy variables produced by the SMT layer?
- Which equalities must become SAT-visible atoms before they can be propagated?
- Can a theory solver request creation of a missing equality atom, or must
  closure initially be restricted to already-visible equality atoms?
- What model object is produced after `sat`, how does it reconstruct eliminated
  constants and preprocessing auxiliaries, and how is it printed as SMT-LIB?
- What is the explanation for a model-equality propagation: a theory-implied
  reason, a model-choice lemma, or a branch-causing guess that must be
  represented differently?
- How should model mutation be exposed: as a theory-local operation, a
  combination-layer heuristic, or a SAT phase/assumption strategy?
- What counters distinguish entailed equalities from model equalities, returned
  propagations, conflicts caused by disagreement, and discarded candidates?

### Likely First Subset

The first implementable subset should be split into two tracks. Track A is a
prerequisite for any model-guided closure claim:

1. Keep expanding explicit model construction beyond the current QF_UF subset.
2. Validate SMT-LIB `get-model` responses with Dolmen before relying on them in
   combination experiments.
3. Add model validation tests against the asserted formulas, including formulas
   affected by preprocessing passes.
4. Continue expanding reconstruction state for satisfiability-preserving
   rewrites beyond the current QF_UF unit and substitution subset.

Track B is the first closure scaffold, default-off and deliberately narrower
than the full paper:

1. Add a documented combination-interface skeleton that can list shared terms by
   sort and identify equality atoms over those terms.
2. For convex theories, support entailed equality propagation over existing
   SAT-visible interface equalities only.
3. Add counters and traces before adding model-equality guesses.
4. Add model-equality generation only for a theory solver that can provide a
   stable candidate model and a sound disagreement response.
5. Keep atom creation for missing interface equalities as a separate milestone,
   because it changes the Boolean abstraction contract.

Track B can begin with the current QF_UF model printer if it is limited to
entailed equalities and interface bookkeeping. Track B must not implement or
advertise model-equality generation until Track A has produced a theory-facing
model object that theories can inspect, mutate, and validate. Even then, the
subset is not a complete implementation of the model-based theory combination
paper; it is the smallest scaffold that can be fuzzed and benchmarked without
claiming default readiness.

### Implemented Subset: Interface Bookkeeping

The implemented subset is intentionally below the algorithmic level of the
papers above. It is not de Moura/Bjorner model-based combination and not classic
Nelson-Oppen equality propagation. It provides the common data extraction needed
to make those future mechanisms measurable:

- **Data structures:** `InterfaceTerm`, `InterfaceEquality`,
  `InterfaceEqualityRegistryEntry`, and `NelsonOppenInterfaceSummary` in
  `src/smt/theory_combination.hpp`.
- **Owner:** `smt/`, because this layer owns post-preprocessing assertions and
  the Boolean/theory atom boundary.
- **Terms included:** user-visible constants of non-Boolean sorts that are
  reachable from the current assertions. Primo-generated `$primo_` auxiliaries
  and Boolean constants are excluded.
- **Equality atoms included:** existing equality terms whose two sides are
  distinct included interface constants of the same sort. Reversed duplicate
  pairs are canonicalized for counting. Polarity is intentionally ignored at
  this layer: an asserted disequality still makes the corresponding interface
  equality SAT-visible.
- **Registry entries:** every distinct same-sort interface-constant pair is
  represented as either `Visible` with the existing equality `TermId`, or
  `Missing` with an invalid atom id. Later theory-combination code can inspect
  this registry without guessing whether an equality atom already exists.
- **Complexity:** when diagnostics are enabled, one traversal over reachable
  assertion terms plus per-sort sorting of discovered constants, followed by
  materializing the same-sort interface-pair registry. This remains opt-in
  because the pair registry is quadratic in the number of interface constants
  per sort.
- **Invariant:** no term or SAT atom creation occurs here, so the analyzer is
  behavior-preserving and cannot change solver search.
- **Runtime status:** the analyzer is opt-in through
  `CheckSatOptions::nelson_oppen_diagnostics` and
  `--nelson-oppen-diagnostics`.
- **Tests:** `tests/unit/theory_combination_tests.cpp` checks sort isolation,
  visible equality detection, internal-symbol exclusion, Boolean exclusion,
  self-equality exclusion, reversed-pair deduplication, and a mock theory
  consumer that distinguishes visible equality atoms from missing pairs without
  creating atoms.

Limitations:

- no purification ownership yet;
- no theory ownership labels beyond sort partitioning;
- no care-pair API;
- no explanations or learned clauses;
- no arithmetic model fragments, because LRA is intentionally not part of this
  branch yet.

## Core Nelson-Oppen Sources

Greg Nelson and Derek C. Oppen. *Simplification by Cooperating Decision
Procedures.* ACM Transactions on Programming Languages and Systems 1(2),
245-257, 1979. DOI: <https://doi.org/10.1145/357073.357079>.

Derek C. Oppen. *Complexity, Convexity and Combinations of Theories.*
Theoretical Computer Science 12(3), 291-302, 1980.
ScienceDirect entry:
<https://www.sciencedirect.com/science/article/pii/0304397580900596>.

Cesare Tinelli and Mehdi T. Harandi. *A New Correctness Proof of the
Nelson-Oppen Combination Procedure.* FroCoS 1996, pp. 103-120.
DOI: <https://doi.org/10.1007/978-94-009-0349-4_5>.

### Relevance

These are the correctness baseline: purification, disjoint signatures, shared
variables, arrangements, stable infiniteness, convexity, and equality sharing.
They define the soundness obligations that any optimized or model-based closure
method must preserve.

### Primo Checklist Items

- Define purification ownership in `smt/` and ensure theory solvers never see
  mixed-theory terms.
- Maintain a sorted shared-variable set and avoid equality sharing across
  incompatible sorts.
- Document whether each theory is stably infinite and convex for the fragment
  being combined.
- For non-convex theories, do not claim classic deterministic Nelson-Oppen
  completeness unless disjunctions of interface equalities or an equivalent SAT
  search mechanism are implemented.

## DPLL(T) Integration

Sava Krstic and Amit Goel. *Architecting Solvers for SAT Modulo Theories:
Nelson-Oppen with DPLL.* FroCoS 2007, LNCS 4720, pp. 1-27.
DOI: <https://doi.org/10.1007/978-3-540-74621-8_1>.

### Relevance

This paper gives a transition-system view of SMT solvers that combine a SAT
engine with multiple disjoint theories. It is directly useful for Primo because
Primo already has a CDCL backend through CaDiCaL and a DPLL(T)-style outer SMT
loop. The paper helps specify which events belong to SAT assignment, literal
dispatch, equality dispatch, theory inference, explanation, learning, and
backjumping.

### Primo Checklist Items

- Represent theory ownership and shared-literal dispatch explicitly instead of
  letting one theory inspect another theory's atoms.
- Keep learned combination clauses as ordinary SAT clauses with theory-owned
  explanations.
- Add tests where equality inferred by one theory must be visible to another
  before the SAT model can be accepted.

## Delayed And SAT-Mediated Combination

Roberto Bruttomesso, Alessandro Cimatti, Anders Franzen, Alberto Griggio, and
Roberto Sebastiani. *Delayed Theory Combination vs. Nelson-Oppen for
Satisfiability Modulo Theories: A Comparative Analysis.* LPAR 2006, LNCS 4246,
pp. 527-541. PDF:
<https://es-static.fbk.eu/people/griggio/papers/lpar06_dtc.pdf>.

Diego Caminha B. de Oliveira, David Deharbe, and Pascal Fontaine.
*Combining Decision Procedures by (Model-)Equality Propagation.* Science of
Computer Programming 76(12), 1070-1082, 2011. PDF:
<https://people.montefiore.uliege.be/pfontain/Caminha4.pdf>.

### Relevance

Delayed theory combination shifts some or all interface-equality search to the
SAT solver by making the relevant equality choices Boolean. Model-equality
propagation is close in spirit to de Moura/Bjorner: it exchanges equalities
deduced by theories or generated by theory models, with SAT responsible for
resolving bad guesses.

These papers are implementation-relevant because Primo already has a SAT-backed
theory atom abstraction. The risk is that adding all shared-pair equalities
eagerly may dominate parsing, clausification, and SAT preprocessing, exactly the
kind of overhead seen in earlier QF_UF experiments when a mechanism was active
but had little delivered propagation.

### Primo Checklist Items

- Separate candidate generation from SAT atom creation.
- Measure shared-pair count, created equality atoms, propagated interface
  equalities, and conflicts caused by interface disagreement.
- Keep a delayed strategy available as an ablation baseline against
  model-based closure.

## Care Sets And Reduced Equality Sharing

Dejan Jovanovic and Clark Barrett. *Sharing is Caring: Combination of Theories.*
FroCoS 2011, LNCS 6989, pp. 195-210. PDF:
<https://cvc4.github.io/publications/2011/JB11-FroCoS.pdf>.

Dejan Jovanovic and Clark Barrett. *Being Careful About Theory Combination.*
Formal Methods in System Design, 2012/2013. PDF:
<https://cvc4.github.io/publications/2013/JB13.pdf>.

### Relevance

These papers reformulate Nelson-Oppen-style combination around explicit
equality propagation and "care" information, so solvers can ignore pairs of
shared variables that cannot affect their current reasoning. This is highly
relevant to Primo because a naive shared-variable cross-product can be too large
and because the QF_UF branch has repeatedly shown that generated work must be
distinguished from delivered, useful work.

### Primo Checklist Items

- Add a theory API for care pairs only after there is a concrete consumer.
- For EUF, derive care pairs from terms whose congruence result can change if an
  argument equality changes.
- Track care-pair pruning counters and compare them with full cross-product
  delayed-combination counters.

## Broader Combination-Property Context

Clark Barrett, Roberto Sebastiani, Sanjit Seshia, and Cesare Tinelli.
*Satisfiability Modulo Theories.* Handbook of Satisfiability, 2009.
Author PDF: <https://people.eecs.berkeley.edu/~sseshia/pubdir/SMT-BookChapter.pdf>.

Guilherme V. Toledo, Yoni Zohar, and Clark Barrett. *Combining Combination
Properties, Part I: Nelson-Oppen and Politeness.* Journal of Automated
Reasoning, 2026. DOI: <https://doi.org/10.1007/s10817-025-09746-5>.

### Relevance

The handbook chapter is useful as a survey of the SMT setting and terminology.
The 2026 combination-properties paper is not an immediate implementation target,
but it is a useful guardrail when we later consider non-stably-infinite or
finite-model theories. It should remain background unless the branch explicitly
targets polite or finite-model combination.

## Initial Design Direction

The first design should be conservative:

- Handle explicit model construction and model printing before implementing
  model-equality guesses from de Moura/Bjorner.
- Do not add eager all-pairs equality atoms as the default.
- Build a theory-combination coordinator in `smt/` that knows shared terms,
  equality atoms, and theory ownership, but delegates theory reasoning to
  `theory/`.
- Start with already-visible equality atoms, then measure whether missing atom
  creation is needed.
- Add separate counters for `candidate`, `created`, `entailed`, `model`,
  `returned`, `conflict`, and `discarded` interface equalities.
- Require focused tests for purification, sort isolation, backtracking,
  equality dispatch, conflict explanations, and model-equality disagreement.
- Require a QF_UF-plus-second-theory benchmark or synthetic mixed-theory corpus
  before any performance claim.

The most plausible competitive path is not "full Nelson-Oppen closure
everywhere"; it is a staged combination layer that starts with precise
interface-equality propagation, uses care sets to keep the candidate set small,
and only then tries model-based equality guesses where a theory can cheaply
produce and mutate candidate models.
