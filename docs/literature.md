# Literature

## Lexical specification and external parser audit

[](lexical-conformance.md) records the SMT-LIB 2.7 Sections 3.1–3.3 and binder
rules used for original conformance tests, public cvc5/Dolmen test categories,
and inspection of Griggio's older Flex/Bison parser. Symbol canonicalization
and category-aware lexical/command validation are now implemented with explicit
user-selected compatibility exceptions; leading-zero numerals remain forbidden.
External code is an inspection source, not copied implementation or a substitute
for the current standard. The historical matrix records the original wrong answers;
repair qualification is tracked separately.

## Scoped command sessions and incremental tactics

[](incremental-smt.md) maps SMT-LIB 2.7 Sections 4.1–4.2 to the new
`SmtSession` journal, scopes, protocol modes and query-local ownership. Its
replay-based architecture is an original baseline. The Strategy Challenge,
Section 3, remains the basis of existing ordered goals and model converters;
Z3 Internals Sections 8.3 and 8.6 motivate, but do not qualify as implemented,
future incoming-formula translation and restoration of eliminated constraints.
Persistent incremental solving is deferred, not inferred from incremental
theory callbacks. Tests specifically strengthen formulas after one-sided
elimination and substitution, and discard previous disjunctive winners.

This file collects implementation-oriented literature summaries.

Do not claim to have read a paper unless the paper text or notes are available. When uncertain, mark the claim as uncertain.

If a required paper cannot be downloaded or accessed, ask the user to provide it. Do not replace it with a secondary summary unless explicitly approved.

## Template

```markdown
## Paper title

Bibliographic data.

### Relevance

### Algorithm

### Data structures

### Implementation notes

### Risks

### Possible milestone
```

## Exploiting symmetry in SMT problems

David Déharbe, Pascal Fontaine, Stephan Merz, and Bruno Woltzenlogel Paleo,
CADE 2011.

PDF: <https://www.verit-solver.org/papers/cade2011.pdf>

### Relevance

Motivates QF_UF symmetry breaking by permutations of uninterpreted constants.
This is directly relevant to finite-domain-looking QF_UF benchmarks where many
constants are interchangeable.

### Algorithm

The paper defines formula invariance under permutations of constants and uses
that invariance to restrict the model search space. Primo implements a
conservative syntactic check for the finite-domain choice patterns it already
recognizes.

### Data structures

Primo uses hash-consed `TermId` formulas, groups finite-domain choices by value
set, and canonicalizes formulas under candidate permutations.

### Implementation notes

Implemented as default-on `--uf-symmetry-breaking` in `src/smt/preprocessing.*`,
with `--no-uf-symmetry-breaking` for ablation runs. Primo does not implement
graph automorphism detection.

### Risks

The pass is only sound for moving terms if the generated restriction preserves
the constants syntactically used by the selected term, as in Déharbe et al.'s
Algorithm 1. Treating moving terms as a fixed value-precedence sequence is not a
valid shortcut.

### Possible milestone

Benchmark-driven decision on whether the option should remain experimental or
become part of a tuned QF_UF preset.

## Symmetry Breaking Using Value Precedence

Toby Walsh. *Symmetry Breaking Using Value Precedence.* ECAI 2006.
arXiv: <https://arxiv.org/abs/0903.1136>.

### Relevance

Value precedence is the fixed-variable symmetry-breaking component used by
Primo when an accepted finite-domain UF symmetry group contains recognized terms
that do not themselves contain any permuted value.

### Algorithm

For an ordered sequence of fixed terms and interchangeable values, the first
occurrence of each non-minimal value must be preceded by an occurrence of the
previous value. Primo also emits prefix exclusions that prevent early fixed
terms from using values outside their canonical prefix. The implementation is
limited to terms whose syntax contains no permuted value. Groups containing
moving terms use this value-precedence scheme only over the fixed subset; groups
with no fixed terms use the separate Déharbe-style moving-term fallback.

### Data structures

`SymmetryGroup` stores the interchangeable values and candidate terms.
`SymmetryTerm::used_values` records which group values occur inside a candidate
term. Empty `used_values` selects the fixed-term subsequence for value
precedence; nonempty groups with no fixed term are handled by the moving-term
fallback.

### Risks

The July 2026 prototype stacked fixed-term value precedence with the moving-term
Algorithm 1 restrictions for the same group. On the 151-file NEQ/PEQ/SEQ
targeted SLURM run it improved PAR2 from 3972.164 to 2152.063 and solved 145
instead of 140 instances, but it produced wrong `unsat` answers on
`SEQ/SEQ018_size8.smt2` and `SEQ/SEQ019_size6.smt2`. The current implementation
does not stack the two schemes for one group and keeps those SEQ cases as
sat-preservation checks.

### Possible milestone

Study Reger, Riener, and Suda's broader principal-term symmetry-avoidance
conditions before extending the current fixed-term subset to richer principal
terms. Any extension must include sat-preservation tests for
`SEQ/SEQ018_size8.smt2` and `SEQ/SEQ019_size6.smt2`-shaped formulas.

## Symmetry Avoidance in MACE-Style Finite Model Finding

Giles Reger, Martin Riener, and Martin Suda. *Symmetry Avoidance in MACE-Style
Finite Model Finding.* FroCoS 2019, LNCS 11715.
DOI: <https://doi.org/10.1007/978-3-030-29007-8_1>.

### Relevance

The NEQ, PEQ, and SEQ QF_UF benchmarks have the same finite-model flavor as
MACE-style encodings: explicit domain constants, totality choices, and
functionality/AMO clauses. The paper gives the soundness boundary for stronger
symmetry-avoidance clauses over principal terms, including why terms containing
domain constants need additional ordering conditions.

### Algorithm

Primo does not implement the paper's principal-term ordering schemes or the
generalized non-constant-term conditions from Theorem 5.

### Data structures

The current symmetry data structures identify candidate value sets and term
annotations, but a future principal-term implementation would need an explicit
domain-constant order, a principal-term classifier, and checks that argument
constants enter the sequence before the term that contains them.

### Risks

The paper explicitly warns that overly strong clauses for terms like `f(d1)` can
be unsound. Any extension beyond fixed terms needs separate tests and benchmark
counters.

### Possible milestone

Implement a documented principal-term sequence for finite-model-style QF_UF
inputs only after deriving the admissible term ordering from the paper and
adding sat-preservation tests for the SEQ counterexamples above.

## Generating minimum transitivity constraints in P-time for deciding Equality Logic

Ofer Strichman and Mirron Rozanov, SMT Workshop 2005.

Paper text is not currently checked into this workspace. The title and authors
come from the SMT-LIB equality-diamond benchmark metadata.

### Relevance

The SMT-LIB `eq_diamond` QF_UF benchmarks are crafted equality-logic formulas
where each disjunction branch implies the same endpoint equality. A lazy EUF
loop without preprocessing can explore exponentially many branch combinations.

### Algorithm

Primo does not implement the paper's full minimum-transitivity-constraint
generation. The implemented `--eq-bridge` pass is narrower: for each `or`
subformula, compute equality closure inside every branch and add only equalities
that are implied by all branches.

### Data structures

The pass uses a local union-find over `core::TermId` values per disjunction
branch and emits ordinary hash-consed equality terms as new assertions.

### Implementation notes

Implemented as default-off `--eq-bridge` in `src/smt/preprocessing.*`. See
[](preprocessing.md).

### Risks

The pass is intentionally capped by branch count and pair checks. It is a
targeted equality-diamond optimization, not a general replacement for theory
propagation or a full transitivity-constraint generator.

### Possible milestone

Obtain the paper and decide whether a more general minimum-transitivity
constraint pass is worth implementing.

## Lazy Satisfiability Modulo Theories

Roberto Sebastiani, *Lazy Satisfiability Modulo Theories*, Journal on
Satisfiability, Boolean Modeling and Computation, 2007.

PDF: <https://disi.unitn.it/rseba/papers/JSAT-2007.pdf>

### Relevance

Section 6.10 describes pure-literal filtering for SMT: non-Boolean theory atoms
that occur only positively or only negatively in the input formula can have the
opposite-polarity assigned theory literal omitted from the assignment sent to
the theory solver. Section 7.2.2 describes the related, stronger idea of
avoiding ghost literals.

### Algorithm

Primo implements only the pure-literal filtering subset for QF_UF. It records
positive and negative occurrence of each theory atom during Boolean abstraction.
At EUF check time, a false assignment of a positive-only atom and a true
assignment of a negative-only atom are filtered out before congruence closure is
updated.

### Data structures

The polarity bits are stored on `BooleanClausifier::TheoryAtom`. Assigned
theory literals carry a `filtered` bit so the lazy and incremental EUF engines
can preserve atom-to-flat-index mappings while skipping irrelevant assignments.

### Implementation notes

Polarity collection is implemented in `src/smt/clausifier.cpp`; filtering and
EUF integration are in `src/smt/solver_engines.cpp`. Equality atoms and Boolean
predicate applications can be filtered only when their theory meaning is
exhausted by propositional occurrences. Boolean terms used as UF arguments
are protected as relevant at both polarities; filtering those assignments
would admit a third Boolean value. See [](euf-boolean-arguments.md). Filtered
literals are not used as model-propagation premises or propagated candidates.

### Risks

This is not SAT pure-literal elimination and must not force pure theory atoms to
a preferred truth value. Filtering can reduce pruning power, so benchmark
evaluation must compare family-level solved counts, PAR2, and conflict counts.

### Possible milestone

Evaluate whether ghost-literal decision avoidance is worth adding after the
pure-filtering counters show a useful hit rate.

## Fuzzing and delta-debugging SMT solvers

Robert Brummayer and Armin Biere, *Fuzzing and delta-debugging SMT solvers*,
SMT 2009. DOI:
[`10.1145/1670412.1670413`](https://dl.acm.org/doi/10.1145/1670412.1670413).
Public PDF:
[`BrummayerBiere-SMT09.pdf`](https://fmv.jku.at/papers/BrummayerBiere-SMT09.pdf).

### Relevance

This is the debugging model Primo should use for solver validation: fuzz
SMT-LIB formulas, compare with trusted solvers, and minimize failures before
turning them into regression tests.

### Algorithm

The paper combines grammar-based random SMT formula generation with delta
debugging for failure-inducing formulas. Primo uses YinYang/typefuzz for
seed-based mutation and FuzzSMT for independent grammar-based generation.
Reduction is handled through ddSMT when it is installed.

### Data structures

The key artifact is an SMT-LIB file plus metadata describing Primo's result,
reference solver results, timeout, and failure class.

### Implementation notes

Use `scripts/yinyang_fuzz.sh` for seed mutation, `make fuzzsmt` for FuzzSMT
`QF_UF` generation, `scripts/compare_with_z3.py` for differential
classification, and `scripts/minimize_smt2.py` for ddSMT-backed reduction.

### Risks

Fuzzing without minimization produces bug piles that are hard to act on. Every
SLURM fuzzing campaign should have a collection and reduction step.

### Possible milestone

Automate SLURM artifact collection and run reduction on every new
`yinyang_bugs/` formula.

## Using SAT and SMT Solvers for Finite Model Finding with Sorts

Giles Reger and Martin Suda.

PDF: <https://www.cs.man.ac.uk/~regerg/papers/arw16.pdf>

### Relevance

Documents MACE-style finite model finding with sorted domains and explicitly
uses value ordering as symmetry breaking for efficiency.

### Algorithm

The full paper is about finite model finding by grounding over finite domains
and using SAT/SMT to guide sort-size search. Primo keeps this as background
only; the current QF_UF symmetry implementation uses the SMT-specific
restriction algorithm from Déharbe et al.

### Data structures

There is no direct Primo implementation data structure for this paper at the
moment.

### Implementation notes

Generated assertions say that `t_i` cannot use values later than `c_i`, and
using `c_j` for `j > 0` requires an earlier term to use `c_{j-1}`.

### Risks

Value precedence alone is not a finite-model encoding. It must remain guarded by
the SMT invariance check.

### Possible milestone

Add finite-domain AMO and equality-definition clauses once the QF_UF SAT/EUF
interfaces are strong enough to expose useful stats and proof behavior.

## CVC4 architecture and preprocessing

Clark Barrett, Christopher L. Conway, Morgan Deters, Liana Hadarean, Dejan
Jovanovic, Tim King, Andrew Reynolds, and Cesare Tinelli, *CVC4*, CAV 2011.

PDF: <https://smt-comp.github.io/2012/system-descriptions/cvc4.pdf>

### Relevance

The system description separates formula preprocessing from the SAT/theory
engines and lists nonclausal simplification, ITE simplification, and
unconstrained-value simplification as important reductions before search.

### Algorithm

Primo currently has an experimental narrow unit-equality substitution subset of
nonclausal simplification: positive equalities visible at top level or under
top-level conjunction are preserved as facts and used to rewrite other
non-Boolean ground terms before clausification. It is available for tuning but
is not enabled by default.

### Data structures

The pass uses hash-consed `core::TermId` terms and a temporary union-find over
visible equality endpoints. It does not introduce fresh terms or inspect SAT
backend state.

### Implementation notes

Implemented in `substitute_ground_unit_equalities` in `src/smt/preprocessing.*`
and counted by `:preprocessing.unit-equality.substitution-units` and
`:preprocessing.unit-equality.substitutions`.

### Risks

The current subset does not use proof objects and does not rewrite Bool-sorted
classes. It must preserve the unit equalities that justify substitutions.

### Possible milestone

Extend the nonclausal simplifier with proof-producing rewrites and a broader
but still bounded congruence-aware representative selection.

## Efficient conversion of term ITEs

Sangyun Kim, Fabio Somenzi, and HoonSang Jin, *Efficient Term-ITE Conversion for
Satisfiability Modulo Theories*, SAT 2009.

DOI page: <https://link.springer.com/chapter/10.1007/978-3-642-02777-2_20>

### Relevance

QF_UF benchmarks with large term-level ITE tables can become much smaller when
ITE conversion avoids unnecessary fresh terms and shares guards carefully.
Recent NEQ/PEQ/SEQ targets inspected locally did not contain term ITEs, so this
is not the immediate bottleneck for those families.

### Algorithm

Not implemented. Primo only lowers Boolean ITEs and the targeted
`(= x (ite c a b))` assignment shape in the frontend.

### Data structures

Future work would need a design note mapping the paper's conversion state to
`Frontend`, `TermManager`, and assertion-side constraints.

### Implementation notes

Documented as a future preprocessing target, not as implemented behavior.

### Risks

Naive ITE expansion can increase formula size. Any implementation needs size
counters and benchmark gates before it is enabled by default.

### Possible milestone

Add a paper checklist and implement the paper's guarded sharing only for
QF_UF formulas whose frontend ITE counters are nonzero.

## Unconstrained-value simplification

The CVC4 system description cites unconstrained-value simplification as a
preprocessing technique. The accessible paper below is related follow-up
material found during the survey; it should be read and checked against Primo's
term/model invariants before implementation.

Accessible preprint located during the survey:
<https://www.fi.muni.cz/~xstrejc/publications/sat2017preprint.pdf>

### Relevance

Large ground formulas often contain terms whose values are unconstrained by the
rest of the formula. Replacing such terms can eliminate equalities and branches
before SAT search.

### Algorithm

Not implemented. The paper needs a separate design checklist before coding:
which term occurrences are unconstrained, how replacements interact with
hash-consing and SMT-LIB models, and which cases are proof-producing.

### Data structures

Likely requires occurrence/polarity information over `core::TermId` formulas
and a controlled replacement map distinct from the current unit-equality
substitution map.

### Implementation notes

Future target only.

### Risks

Unsoundness is easy if a term is considered unconstrained in one Boolean context
but constrained elsewhere, or if model reconstruction is later required.

### Possible milestone

Start with a documentation checklist and tests for one paper-supported,
model-preserving subset.

## Equality-logic transitivity constraints and selective Ackermannization

Randal E. Bryant, Steven German, and Miroslav N. Velev, *Processor verification
using efficient reductions of the logic of uninterpreted functions to
propositional logic*, ACM Transactions on Computational Logic 2001.

Ofer Strichman and Mirron Rozanov's minimum-transitivity-constraint work is
also relevant to equality-logic benchmark families.

Bryant/Velev preprint: <https://arxiv.org/abs/cs/0008001>

### Relevance

Some QF_UF families are dominated by equality-logic search where adding the
right transitivity or congruence clauses can make the SAT abstraction decisive
before repeated lazy EUF conflicts.

### Algorithm

Not implemented beyond Primo's narrow equality bridge and optional bounded
ground-congruence clauses. A full implementation needs a fresh clean-room design
note from the paper algorithms.

### Data structures

Likely candidates are equality graphs over existing `TermId` endpoints, guarded
clause generation, and counters separating generated transitivity,
congruence, and skipped clauses.

### Implementation notes

Future target only. Do not copy encodings from another solver.

### Risks

Clause blowup can erase any benefit. Default behavior would need caps,
family-level PAR2 evaluation, and proof plans.

### Possible milestone

Design and evaluate a bounded equality-graph pass on the small equality-logic
instances where Z3, cvc5, and LLM2SMT are all subsecond but Primo still learns
many EUF conflicts.
