# Exploiting Symmetry In SMT Problems

## Citation

David Déharbe, Pascal Fontaine, Stephan Merz, and Bruno Woltzenlogel Paleo.
*Exploiting symmetry in SMT problems.* CADE 2011.

PDF: <https://www.verit-solver.org/papers/cade2011.pdf>

## Relevance

This paper is the direct source for Primo's default-on
`--uf-symmetry-breaking` preprocessing pass. The pass can be disabled with
`--no-uf-symmetry-breaking`.

The implementation-relevant theorem is that if a formula is invariant under
permutations of a set of uninterpreted constants, then search can be restricted
to assignments that choose a canonical representative for terms that do not
contain those constants.

## Mapping To Primo

Primo implements a conservative syntactic version:

- candidate values are constants from finite-domain choice disjunctions;
- the accumulated assertions must be invariant under generated permutations of
  those constants;
- candidate terms are selected from recognized finite-domain choices;
- generated restrictions follow Algorithm 1: every selected term keeps the
  permuted constants that occur inside that term, then receives one fresh
  representative value if available.

Primo does not implement the paper's broader symmetry-detection pipeline or
SyMT-style graph automorphism. It only uses the constant-permutation idea once a
finite-domain choice pattern has already been recognized.

## Implementation Status

Implemented in `src/smt/preprocessing.*`:

- syntactic invariance check under a cycle and first-value swap;
- canonical comparison treating equality, `and`, and `or` as commutative;
- occurrence-density term ordering for the Algorithm 1 selection loop;
- unit exclusions for values outside the selected term's allowed representative
  set;
- default-on CLI flag with an explicit disable switch.

Deferred:

- automatic graph automorphism detection;
- proof support for generated restrictions;
- broader benchmark-driven family policy beyond the current conservative
  finite-domain recognizer.
