# CVC4 architecture

## Citation

Clark W. Barrett, Christopher L. Conway, Morgan Deters, Liana Hadarean, Dejan Jovanovic, Tim King, Andrew Reynolds, Cesare Tinelli:  
CVC4. CAV 2011.

## Role in Primo

This paper is used as an architectural reference, not as a direct algorithmic specification.

## What we use

- modular SMT engine
- typed terms
- theory solvers
- SMT-LIB orientation
- model construction
- incremental solving as a design concern

## What we do not implement initially

- full SMT-LIB
- quantifiers
- arrays
- bit-vectors
- datatypes
- strings
- proof production

## Implementation notes

Primo should keep clear boundaries between:

- SMT-LIB frontend
- core term representation
- SAT layer
- SMT/DPLL(T) layer
- theory solvers
- model construction

## Milestones

- M0: core term and SMT-LIB scaffold
- M1: Boolean abstraction and IPASIR-UP/CaDiCaL SAT interface
- M2: lazy DPLL(T)
- M3: QF_UF theory solver
