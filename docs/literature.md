# Literature

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

Implemented in `src/smt/check_sat.cpp` for equality atoms, Boolean predicate
applications, and Boolean constants that are handled as EUF terms. Filtered
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
