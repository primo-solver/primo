# Fast congruence closure and extensions

## Citation

Robert Nieuwenhuis, Albert Oliveras:  
Fast Congruence Closure and Extensions.

## Role in Primo

This paper is the target reference for Primo's fast QF_UF theory solver. The
default engine selected with `--euf-engine=incremental` implements the Section 3
use-list and signature-lookup shape over Primo's own flat term representation.
The older lazy scaffold remains available with `--euf-engine=lazy` for
diagnostics. The Section 4 and 5 proof-producing union-find machinery is not
implemented.

## Implemented Subsets

The diagnostic lazy path implements a basic ground-congruence scaffold:

- union-find over term ids;
- recursive term registration;
- repeated saturation for application congruence;
- explicit equality assertions;
- explanation graph edges labelled by SAT reason literals;
- congruence edges justified by child equality explanations.

The default incremental path adds:

- deterministic Curry-style flat definitions over `core::TermId` values;
- eager representatives and class lists;
- affected-definition use lists;
- sparse signature lookup for congruent flat applications;
- a pending merge queue for explicit and congruence merges;
- disequality conflict detection;
- sparse `push`/`pop` checkpoints and an undo trail for online propagation
  experiments;
- counters for terms, flat definitions, lookup probes and hits, pending pops,
  congruence merges, conflicts, explanation length, and backtracks.

This is a Section 3 shaped subset, not the complete optimized paper algorithm.
Explanations still come from a simple equality edge graph and are sound but not
minimal. Backtracking trails ordinary mutable closure state rather than using
the paper's optimized timestamped lookup-table policy.

## Remaining implementation checklist

Before claiming that Primo implements the full Nieuwenhuis-Oliveras fast
congruence closure, the code must add and document:

- explanation/proof-forest data structures for equality reasons;
- the paper's timestamped lookup-table backtracking policy;
- pure-class performance probes showing the Section 3 machinery improves the
  intended QF_UF families;
- benchmark evidence showing solved-count or PAR2 improvement without wrong
  answers.

## Deferred

- optimized incremental backtracking
- optimized proof-forest explanation machinery
- proof production

## Data structures

Diagnostic lazy scaffold:

- union-find over `TermId`;
- set/vector of registered terms;
- explicit explanation edge graph.

Default incremental subset:

- flat term definitions;
- equivalence classes;
- application lookup/signature table;
- affected-definition use lists;
- pending merge queue;
- disequality set;
- explanation graph.

## Invariants

Current scaffold invariants:

- all inspected terms are created by `core::TermManager`;
- equality is over `TermId`, not SMT-LIB AST nodes;
- explanations contain SAT literals supplied by the SMT layer.

Incremental subset invariants:

- lookup table entries agree with current representatives;
- every pending merge is either processed or invalidated by backtracking;
- use lists contain exactly the applications that can become congruent after a
  class merge;
- explanation edges only cite SAT literals supplied by the SMT layer.

## Tests derived from the paper

- equality/disequality contradiction
- unary congruence
- binary congruence
- nested congruence
- lookup-table congruence discovery without scanning all term pairs
- backtrack rediscovery
- future tests must include proof-forest explanation paths after that mechanism
  is implemented.
