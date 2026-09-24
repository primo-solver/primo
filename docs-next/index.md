# Primo

Primo is a paper-grounded SMT solver built from first principles, developed
with LLM assistance. It decides whether a set of logical assertions can hold
together and, for supported inputs, can produce a satisfying model. Its SMT
implementation is original; propositional search is delegated to CaDiCaL
through IPASIR-UP. Primo-owned code and documentation use the MIT license.
The {source}`command loop <src/main.cpp#L87-L119>` is the executable entry
point; {source}`IpasirUpBackend <src/sat/ipasir_up.hpp#L68-L94>` defines the SAT
boundary.

Correctness is the first requirement. Wrong answers, invalid models, and crashes
block acceptance of solver changes. Contributions need explanations, tests,
and reproducible evidence; see [](development/correctness.md).

## Build and solve a first problem

From a repository checkout, with a C++20 compiler, CMake 3.20+, GNU Make, Git,
Python 3, and a Unix-like shell:

```bash
make build
./build/primo --quiet docs-next/examples/first-solve.smt2
```

The first build downloads required dependencies into `contrib/local` and needs
network access. Linux is the development environment checked for this draft.
The example contains:

```{literalinclude} examples/first-solve.smt2
:language: lisp
```

The answer is `unsat`: equal arguments must give equal results when passed to
the same function, so these assertions contradict each other.

## Implemented now

Primo supports quantifier-free uninterpreted functions, linear Real arithmetic,
linear Int arithmetic, and their supported combinations. Integer search is
bounded and may return `unknown`. The quantified `UF` route supports
enumerative instantiation with limited SAT completion; it does not provide
general quantifier solving or printed models for quantified assertions.
The admitted logic names are checked by
{source}`SmtSession <src/smt/smt_session.cpp#L271-L288>`; quantified assertions
take the {source}`dedicated UF branch <src/smt/solver_session.cpp#L624-L649>`.

The default session engine supports repeated queries and `push`/`pop` by
rebuilding solving state. Opt-in assumptions engines retain more state for
`QF_UF` and `QF_LRA`. Arrays, bit-vectors, nonlinear arithmetic, and proof
production are outside the implemented scope.

The [architecture overview](implementation/architecture.md) explains the
component boundaries; [terms, symbols, and ownership](implementation/core.md)
explains the core representation and its lifetimes.
[Boolean abstraction and SAT integration](implementation/sat.md) follows those
terms into clauses and describes assumptions and theory callbacks. The full
capability matrix and user guides remain in the existing book while they
undergo page review for this draft.

A documentation build or a successful example does not certify solver
correctness across a benchmark corpus.

## Work underway

This replacement manual is being written and reviewed page by page. Start with
the home page, the contributor correctness guide, and the architecture overview.
The [review ledger](review.md) records what has been checked and which pages
still await acceptance. These are draft explanations of existing behavior.

## Open leads

Primo should also be usable as a library. The
[library API and embedding work item](leads/library-api.md) records the required
boundary audit and a first external-consumer prototype; implementation has
not started.

The related [incremental-memory lead](leads/incremental-memory.md) will
investigate high memory use, storage reclamation, and whether reference-counted
term handles are needed.

[Compact term storage](leads/term-storage.md) is a separate open lead: compare
payload representations and account for future leaf data. The current layout
remains in use; implementation is deferred.

Possible solver extensions and research ideas remain in the existing book's
open-leads register. They are candidates for investigation, not commitments or
implemented features. The rewrite will give them a separate index and preserve
the outcomes of accepted and rejected experiments.

During review, `make docs-next-serve` serves this draft at
`http://localhost:8001`; `make docs-serve` serves the existing book at
`http://localhost:8000`. These are local previews. No website is published by
either command.
