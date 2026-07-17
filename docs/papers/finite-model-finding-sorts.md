# Using SAT And SMT Solvers For Finite Model Finding With Sorts

## Citation

Giles Reger and Martin Suda. *Using SAT and SMT Solvers for Finite Model
Finding with Sorts.*

PDF: <https://www.cs.man.ac.uk/~regerg/papers/arw16.pdf>

## Relevance

This paper is background for value-ordering/value-precedence constraints in
finite-model encodings. In its MACE-style finite-model setting, ground terms are
ordered and the earliest terms are constrained to use the earliest domain
elements to cut symmetric assignments.

## Mapping To Primo

Primo is not currently a finite model finder and does not eagerly encode whole
finite domains. The current QF_UF symmetry pass no longer implements the
finite-model value-precedence shape from this paper. It uses Déharbe et al.'s
SMT-specific Algorithm 1, which is safe for moving terms only when the generated
restriction preserves the constants occurring inside the selected term.

The QF_UF preprocessor does implement a narrow finite-domain-lemma subset around
explicit choice disjunctions: at-most-one clauses for pairwise disequal values
and equality-definition clauses for equality atoms already observed between
same-domain terms. These are ordinary ground EUF consequences over existing
terms, not the paper's full finite-model SAT reduction.

## Implementation Status

Implemented in `src/smt/preprocessing.*`:

- no direct finite-model value-precedence pass.

Deferred:

- full finite-model SAT reduction;
- sort-size search;
- totality/functionality encodings;
- equality definitions for unmentioned term pairs or generated finite-domain
  search variables.
