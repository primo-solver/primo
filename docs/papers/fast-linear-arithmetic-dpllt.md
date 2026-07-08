# A Fast Linear-Arithmetic Solver for DPLL(T)

## Citation

Bruno Dutertre, Leonardo de Moura:  
A Fast Linear-Arithmetic Solver for DPLL(T).

## Role in Primo

This paper is the later basis for LRA.

## What we use later

- Simplex-style arithmetic solver
- bounds
- tableau
- DPLL(T) integration
- backtracking
- theory propagation
- strict inequalities

## Not implemented initially

The initial Primo scaffold does not implement full LRA.

## Future data structures

- rational coefficients
- linear polynomials
- bounds
- tableau rows
- arithmetic trail
- explanation objects

## Milestones

- LRA0: linear polynomial representation
- LRA1: normalization of arithmetic atoms
- LRA2: tableau scaffold
- LRA3: feasibility checking
- LRA4: conflict explanations
- LRA5: backtracking and propagation
