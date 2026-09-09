# IPASIR-UP: User Propagators for CDCL

## Citation

Katalin Fazekas, Aina Niemetz, Mathias Preiner, Markus Kirchweger, Stefan Szeider, Armin Biere:  
IPASIR-UP: User Propagators for CDCL. SAT 2023.

## Role in Primo

This paper is the basis for Primo's SAT backend interface.

Primo uses IPASIR-UP for both final-assignment DPLL(T) checks and implemented
callback-time theory interaction. The backend owns Boolean search; theory
adapters supply explained consequences. See [](../dpllt.md) for current control
flow and [](../n-way-combination-migration.md) for the collection dispatcher.

## What we use

- incremental SAT backend interface
- adding clauses
- assumptions
- solving under assumptions
- assignment/model-value queries
- failed-assumption information where available
- CaDiCaL as the initial backend implementation
- user propagator callbacks for EUF, arithmetic, dedicated QF_UFLRA modes,
  and the QF_UFLIA component collection
- observed-variable assignment, decision-level, backtrack, and model callbacks
- theory propagations with streamed reason clauses
- external conflict/lemma clauses during search
- optional theory-guided decision literals

## Boundaries

- Do not implement custom DPLL or CDCL in Primo.
- Do not copy CaDiCaL code.
- Keep CaDiCaL-specific integration behind the SAT backend adapter.
- Keep SMT terms and theory state out of backend-specific code.

## CaDiCaL-Specific Constraints Used By Primo

Primo's current implementation is based on CaDiCaL's C++ external propagator
interface. The local header in `contrib/local/include/cadical.hpp` records the
constraints the SMT layer must obey:

- only one external propagator can be connected to a solver;
- variables relevant to that propagator must be observed after connection;
- observed variables are frozen for inprocessing;
- fresh variables may be observed during solving;
- `cb_propagate()` must return an observed literal;
- current external clauses are expected to mention observed variables.

These details are not just backend trivia. An earlier mixed Nelson-Oppen
prototype precreated candidate equality atoms because its adapter could not add
an observed variable in the callback. The current adapter exposes
`new_observed_var()`, and `MixedTheoryPropagationDispatcher` now creates and
observes an equality variable from `cb_propagate()` only after a theory producer
selects that pair. Returning a term pair from an ordinary queue is still not
enough by itself: the dispatcher must materialize and observe its SAT literal
before returning that literal to the backend. See [](../nelson-oppen.md) for
the current combination path.

## Tests derived from the interface

- add simple clauses and solve
- solve under assumptions
- distinguish permanent clauses from assumptions
- query assignment only after SAT
- report missing backend dependency clearly
- propagate an observed theory literal with a reason clause
- learn an external theory conflict clause during search
- keep pure single-theory callback paths separate from the mixed dispatcher
