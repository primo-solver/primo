# Library API and embedding

**Status: open work; audit not started. Owner: unassigned.**

Primo should be usable as a library inside another application as well as
through its command-line executable. This is an intended direction. Whether
the current abstractions need substantial changes remains an open question.

The [Yices 2.2 architecture](../papers/yices.md#the-2014-paper-terms-contexts-and-models)
is a literature reference for separating shared terms, mutable solving
contexts, and independent model snapshots. It motivates this ownership audit;
it does not establish that Primo already supports the same public API.

The [literal-capacity item](../implementation/sat.md#literal-range-and-exhaustion)
is a small related hardening task: allocation exhaustion and invalid literal
values need a controlled API failure, with no signed overflow or process abort.
It does not require a wider literal representation by itself.

The [statistics instrumentation lead](statistics-instrumentation.md) proposes
typed counters and a build-time boundary for optional diagnostics. Library
builds must preserve resource-limit accounting and a consistent public layout;
unavailable diagnostics need an explicit API contract.

## Starting points in the code

- The {source}`CMake library targets <CMakeLists.txt#L187-L240>` already separate
  several implementation layers. The compatibility target `primo_core` also
  links `primo_cli_support`; it is not by itself a defined public library API.
- {source}`SmtSession <src/smt/smt_session.hpp>` accepts command text or parsed
  SMT-LIB commands. Decide how an application constructs and asserts typed
  terms directly, without having to generate SMT-LIB text or depend on parser
  syntax objects.
- {source}`CheckSatOutcome <src/smt/check_sat.hpp#L233-L240>` already carries a
  structured result and an owning typed model. The
  {source}`term manager <src/core/term_manager.hpp>` and
  {source}`model interfaces <src/core/model.hpp>` provide starting points for
  defining caller-visible identity and lifetime rules.
- The {source}`CLI entry point <src/main.cpp#L33-L69>` owns process signal
  handling and a deadline path that exits the process. Keep executable policy
  outside the embedding contract; audit cancellation and error paths through
  the library and its dependencies separately.

These are inspection starting points, not findings that the solver violates
an established embedding contract. No public API, ABI stability, or thread
safety guarantee is introduced by this work item.

The [core chapter](../implementation/core.md) describes current identity,
borrowing, and scope lifetimes for this audit to start from.

The related [incremental-memory investigation](incremental-memory.md) considers
term reclamation and reference counting. Resolve those lifetime requirements
before committing to a public handle API.

## Audit and design questions

1. Define the smallest useful public API: contexts, sorts, symbols, terms,
   assertions, scopes, solving, options, statistics, and model access. Decide
   whether to start with C++, and whether a C API or language bindings belong
   in a later phase. Keep SMT-LIB parsing and printing available as adapters.
2. Specify ownership across contexts, invalid or stale handles, model lifetime
   after another query or context destruction, and callback lifetime. Separate
   independent-instance guarantees from concurrent use of one instance.
3. Review recoverable errors, unsupported inputs, cancellation, and resource
   limits. Embedding must let the host control diagnostics and its process;
   normal library use must not install signal handlers, print unsolicited
   output, or terminate the host.
4. Identify a public header and build-target boundary. Audit transitive
   dependencies, installation/export rules, and any exposed backend details.
   The CLI should consume the public solver API, with process policy outside it.

## First check and acceptance criteria

Start with a small external CMake consumer using the existing layers. Construct
a QF_UF problem programmatically, solve it, inspect a typed model, and exercise
`push`/`pop` and two independent solver instances. Record each private header,
parser object, CLI dependency, or lifetime workaround the client needs.
Use that evidence to choose the API and estimate implementation work.

A broad architectural refactor is justified only by concrete obstacles in that
prototype. If the existing boundaries suffice, prefer exposing and packaging
them through a small public interface. The library requirement remains either
way.

Completion requires an external consumer that builds against the installed
package, documented ownership and error contracts, executable examples, and
tests for model lifetime, cancellation, and instance isolation. The CLI and
library must agree on equivalent inputs. Behavior changes must pass the
[applicable correctness gates](../development/correctness.md), including fresh
fuzzing; packaging alone does not qualify new solver behavior.
