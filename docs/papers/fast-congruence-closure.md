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
- explanation-producing congruence closure

## Initial implemented subset

Primo implements the ground congruence behavior and a first explanation-capable
state:

- union-find over term ids;
- recursive term registration;
- congruence closure for applications;
- explicit equality assertions;
- explanation graph edges labelled by SAT reason literals;
- congruence edges justified by child equality explanations.

The explanation graph is a deliberately simple scaffold. It provides sound
reason literals for lazy conflict clauses and model-time propagation lemmas, but
it is not the optimized proof-forest structure from the paper.

## Planned subset

- disequality assertions
- disequality conflict detection
- incremental use-list/lookup-table saturation

## Deferred

- optimized incremental backtracking
- online theory propagation
- optimized proof-forest explanation machinery
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
