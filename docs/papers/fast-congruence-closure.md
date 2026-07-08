# Fast congruence closure and extensions

## Citation

Robert Nieuwenhuis, Albert Oliveras:  
Fast Congruence Closure and Extensions.

## Role in Primo

This paper is the basis for the QF_UF theory solver.

## What we use

- ground equality reasoning
- congruence closure
- equality propagation concepts
- explanation-producing congruence closure, later

## Initial implemented subset

None yet. This repository currently contains the scaffold.

## Planned subset

- union-find over terms
- congruence closure for applications
- equality assertions
- disequality assertions
- disequality conflict detection

## Deferred

- optimized incremental backtracking
- theory propagation
- full explanation machinery
- proof production

## Data structures

TODO:

- equivalence classes
- application lookup/signature table
- pending merge queue
- disequality set
- explanation graph

## Invariants

TODO.

## Tests derived from the paper

- equality/disequality contradiction
- unary congruence
- binary congruence
- nested congruence
