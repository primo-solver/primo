# Primo design

The current design is documented in the replacement manual:

- [Architecture and layer boundaries](docs-next/implementation/architecture.md)
- [Core terms, ownership and invariants](docs-next/implementation/core.md)
- [SAT integration](docs-next/implementation/sat.md) and
  [DPLL(T) coordination](docs-next/implementation/dpllt.md)
- [Frontend binding](docs-next/implementation/frontend.md) and
  [incremental sessions](docs-next/implementation/sessions.md)
- [Supported capabilities and limits](docs-next/capabilities.md)

The implementation chapters identify owning C++ types, paper sections,
limitations and regression tests. [AGENTS.md](AGENTS.md) retains contributor
rules and architectural constraints. The [original book](docs/index.md) remains
available while the replacement awaits reader acceptance.

The [initial design summary](https://github.com/primo-solver/primo/blob/1f9f116c73520d960278139a0e82dd0dbd4c9b29/DESIGN.md)
is retained as history; its scaffold and lazy-only milestones do not describe
the current solver.
