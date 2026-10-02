# Primo documentation

Primo is a paper-grounded SMT solver built from first principles, developed
with LLM assistance. Its SMT layers are original; propositional solving uses
CaDiCaL through IPASIR-UP.

**New to Primo?** Follow [](guide/getting-started.md) to build the solver and
run a first example. Continue with [](guide/input.md), [](guide/models.md),
and [](guide/sessions.md). The [option reference](reference/options.md) explains
the command line and output conventions.

## Work status

| Entrance | What you will find |
| --- | --- |
| [Done: current behavior](current-status.md) | Supported input, modes, limitations, implementation references and qualification evidence. |
| [Active work](active-work.md) | Work underway, completed subsets, next steps and remaining acceptance criteria. |
| [Open leads](open-leads.md) | Ideas and proposed investigations with evidence, prerequisites and falsifiers. |

The new manual describes solver source {{ source_revision }}. Source links
such as {source}`src/main.cpp` are pinned to that commit. Historical reports
retain the revisions they measured; their results do not automatically qualify
a later checkout.

(how-the-book-is-organised)=
## Understand and contribute

Start at [](architecture.md) for module boundaries, [](literature.md) for the
primary-source reading map, and [](contributing.md) for contribution
requirements. [](run-log.md) connects experiments to recorded commands,
revisions and artifacts.

The user guide and capability map are the first completed rewrite batch.
Existing implementation chapters still combine some reference material with
history; the [rewrite plan](documentation-restructuring.md) tracks their
separation. The [qualification archive](archive/current-status.md) preserves
the former status narrative.

## Documentation policy

Documentation is reviewed with the code it describes. Implemented behavior,
active changes and open hypotheses must remain distinct. Every nontrivial
algorithm identifies its paper or original design basis, implemented subset
and limitations. A paper summary alone does not establish implementation.

(logo-assets)=
Authoring, source-link conventions, local preview and logo assets are described
in [](development/documentation.md). Public hosting and editable release
packaging remain work in the [migration plan](public-repository-migration.md).
