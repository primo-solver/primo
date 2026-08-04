# IPASIR-UP: User Propagators for CDCL

## Citation

Katalin Fazekas, Aina Niemetz, Mathias Preiner, Markus Kirchweger, Stefan Szeider, Armin Biere:  
IPASIR-UP: User Propagators for CDCL. SAT 2023.

## Role in Primo

This paper is the basis for Primo's SAT backend interface.

Primo uses IPASIR-UP to avoid implementing a custom SAT solver while preserving a clean integration point for lazy DPLL(T), and later tighter CDCL(T)-style interaction.

## What we use

- incremental SAT backend interface
- adding clauses
- assumptions
- solving under assumptions
- assignment/model-value queries
- failed-assumption information where available
- CaDiCaL as the initial backend implementation
- user propagator callbacks for EUF, LRA, and mixed QF_UFLRA modes
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

These details are not just backend trivia. They explain why the mixed
Nelson-Oppen online mode currently precreates and observes interface equality
atoms before SAT search, and why the next model-based design should instead
create and observe equality atoms only when a model/care/theory candidate needs
that pair. Returning a candidate from an ordinary queue is not enough unless the
candidate's equality literal already exists and is observed.

## Tests derived from the interface

- add simple clauses and solve
- solve under assumptions
- distinguish permanent clauses from assumptions
- query assignment only after SAT
- report missing backend dependency clearly
- propagate an observed theory literal with a reason clause
- learn an external theory conflict clause during search
- keep pure single-theory callback paths separate from the mixed dispatcher
