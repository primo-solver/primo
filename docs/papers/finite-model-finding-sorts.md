# Using SAT And SMT Solvers For Finite Model Finding With Sorts

## Citation

Giles Reger and Martin Suda. *Using SAT and SMT Solvers for Finite Model
Finding with Sorts.*

PDF: <https://www.cs.man.ac.uk/~regerg/papers/arw16.pdf>

## Relevance

This paper is a source for value-ordering/value-precedence constraints in
finite-model encodings. In its MACE-style finite-model setting, ground terms are
ordered and the earliest terms are constrained to use the earliest domain
elements to cut symmetric assignments.

## Mapping To Primo

Primo is not currently a finite model finder and does not eagerly encode whole
finite domains. The current QF_UF pass borrows only the value-precedence shape:

```text
t_i may use only c_0 ... c_i
t_i = c_j for j > 0 implies some earlier t_k = c_{j-1}
```

Those constraints are emitted only after the SMT-specific symmetry check from
Déharbe et al. accepts the value constants as interchangeable. Existing EUF
reasoning remains authoritative.

## Implementation Status

Implemented in `src/smt/preprocessing.*`:

- value-precedence assertion generation over recognized finite-domain choices.

Deferred:

- full finite-model SAT reduction;
- sort-size search;
- totality/functionality encodings;
- finite-domain at-most-one and equality-definition clauses from LLM2SMT.
