# Exploiting Symmetry In SMT Problems

## Citation

David Déharbe, Pascal Fontaine, Stephan Merz, and Bruno Woltzenlogel Paleo.
*Exploiting symmetry in SMT problems.* CADE 2011.

PDF: <https://www.verit-solver.org/papers/cade2011.pdf>

## Relevance

This paper is the direct source for Primo's default-off
`--uf-symmetry-breaking` preprocessing pass.

The implementation-relevant theorem is that if a formula is invariant under
permutations of a set of uninterpreted constants, then search can be restricted
to assignments that choose a canonical representative for terms that do not
contain those constants.

## Mapping To Primo

Primo implements a conservative syntactic version:

- candidate values are constants from finite-domain choice disjunctions;
- the accumulated assertions must be invariant under generated permutations of
  those constants;
- terms containing the permuted constants are dropped because they move under
  the permutation;
- generated value-precedence assertions restrict the remaining fixed term
  sequence.

Primo does not implement the paper's broader symmetry-detection pipeline or
SyMT-style graph automorphism. It only uses the constant-permutation idea once a
finite-domain choice pattern has already been recognized.

## Implementation Status

Implemented in `src/smt/preprocessing.*`:

- syntactic invariance check under a cycle and first-value swap;
- canonical comparison treating equality, `and`, and `or` as commutative;
- moving-term rejection;
- default-off CLI flag.

Deferred:

- automatic graph automorphism detection;
- proof support for generated restrictions;
- default-on policy based on benchmark evidence.
