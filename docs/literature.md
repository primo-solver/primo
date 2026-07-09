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

Implemented as default-off `--uf-symmetry-breaking` in `src/smt/preprocessing.*`.
Primo does not implement graph automorphism detection.

### Risks

The pass is only sound when the ordered terms are fixed by the value
permutation. Terms containing the permuted constants are therefore rejected.

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

The paper combines random SMT formula generation with delta debugging for
failure-inducing formulas. Primo currently uses YinYang/typefuzz as the
generator and keeps reduction as a separate workflow through
`scripts/minimize_smt2.py`.

### Data structures

The key artifact is an SMT-LIB file plus metadata describing Primo's result,
reference solver results, timeout, and failure class.

### Implementation notes

Use `scripts/yinyang_fuzz.sh` for generated runs, `scripts/compare_with_z3.py`
for differential classification, and `scripts/minimize_smt2.py` for reducing
failing inputs.

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
and using SAT/SMT to guide sort-size search. Primo only borrows the
value-precedence constraint shape for recognized QF_UF finite-domain choices.

### Data structures

The relevant Primo data is the ordered sequence of fixed terms and ordered value
constants in a symmetry group.

### Implementation notes

Generated assertions say that `t_i` cannot use values later than `c_i`, and
using `c_j` for `j > 0` requires an earlier term to use `c_{j-1}`.

### Risks

Value precedence alone is not a finite-model encoding. It must remain guarded by
the SMT invariance check.

### Possible milestone

Add finite-domain AMO and equality-definition clauses once the QF_UF SAT/EUF
interfaces are strong enough to expose useful stats and proof behavior.
