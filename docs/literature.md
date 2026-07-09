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
