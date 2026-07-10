# Fast congruence closure and extensions

## Citation

Robert Nieuwenhuis, Albert Oliveras:  
Fast Congruence Closure and Extensions.

## Role in Primo

This paper is the target reference for the planned fast QF_UF theory solver. It
is not yet implemented as described in the paper.

## What we use today

- ground equality reasoning
- congruence closure
- equality propagation concepts as motivation
- explanation-producing congruence closure as motivation

## Initial implemented subset

Primo currently implements only a basic ground-congruence scaffold:

- union-find over term ids;
- recursive term registration;
- repeated saturation for application congruence;
- explicit equality assertions;
- explanation graph edges labelled by SAT reason literals;
- congruence edges justified by child equality explanations.

The current code does not implement the paper's use lists, signature lookup,
pending merge algorithm, incremental backtracking, or optimized proof forests.
The explanation graph is a deliberately simple scaffold. It provides sound
reason literals for lazy conflict clauses and model-time propagation lemmas, but
it is not the paper algorithm.

## Required implementation checklist

Before claiming that Primo implements Nieuwenhuis-Oliveras fast congruence
closure, the code must add and document:

- equivalence classes with parent/representative invariants matching the paper;
- use lists or an equivalent occurrence index for affected applications;
- a signature lookup table for congruent applications;
- a pending merge queue rather than repeated all-pairs saturation;
- disequality conflict detection integrated with the incremental state;
- explanation/proof-forest data structures for equality reasons;
- backtracking support with a trail of all mutable CC state;
- counters for pending merges, lookup hits, congruence merges, explanation
  length, and backtracks.

## Deferred

- optimized incremental backtracking
- online theory propagation
- optimized proof-forest explanation machinery
- proof production

## Data structures

Current scaffold:

- union-find over `TermId`;
- set/vector of registered terms;
- explicit explanation edge graph.

Paper-backed target:

- equivalence classes
- application lookup/signature table
- pending merge queue
- disequality set
- explanation graph

## Invariants

Current scaffold invariants:

- all inspected terms are created by `core::TermManager`;
- equality is over `TermId`, not SMT-LIB AST nodes;
- explanations contain SAT literals supplied by the SMT layer.

Target paper invariants still to implement:

- lookup table entries agree with current representatives;
- every pending merge is either processed or invalidated by backtracking;
- use lists contain exactly the applications that can become congruent after a
  class merge;
- proof-forest explanations survive backtracking only while their merge remains
  live.

## Tests derived from the paper

- equality/disequality contradiction
- unary congruence
- binary congruence
- nested congruence
- future tests must include lookup-table congruence discovery without scanning
  all terms, proof-forest explanation paths, and backtrack rediscovery.
