# Primo documentation

Primo is a clean-room, paper-grounded SMT solver developed with LLM assistance.

This documentation is authored in MyST Markdown and built with Jupyter Book.

Build it from the repository root with:

```bash
scripts/build_docs.sh
```

The generated HTML is written under `docs/_build/html`.

## Documentation policy

- Use MyST Markdown for architecture, design, algorithm, paper, and developer notes.
- Use notebooks only for executable experiments, benchmark analysis, and plotted reports.
- Keep claims synchronized with implemented functionality.
- Cite the paper or design note behind each nontrivial algorithmic component.
- Keep dependency integration notes separate from algorithm descriptions.
- Link to an explicit target such as `(h8)=` by its label alone -- `[H8](#h8)`
  -- not by path and fragment. The path form (`qf-lra-heuristics.md#h8`) makes
  MyST look up an HTML id in a document that may not have been rendered yet, so
  it emits `xref_missing` warnings that depend on build order while still
  producing a working link. Labels resolve globally and do not.

## Main sections

- Architecture and invariants
- IPASIR-UP/CaDiCaL SAT backend integration
- Lazy DPLL(T)
- EUF/congruence closure
- QF_UF preprocessing and finite-domain symmetry breaking
- Future arithmetic theories
- Debugging, experiments, and literature
