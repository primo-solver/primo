# Supported input and capabilities

Start with [building and running](guide/getting-started.md), then the
[input](guide/input.md), [model](guide/models.md), and [session](guide/sessions.md)
guides. This page describes implemented routes; historical experiment counts
do not extend their supported scope.

## Supported routes

| Logic | Solving scope | Printed models | Session engines |
| --- | --- | --- | --- |
| `QF_UF` | Equality and uninterpreted functions | Yes | Rebuild; opt-in assumptions |
| `QF_LRA` | Linear Real arithmetic | Yes | Rebuild; opt-in assumptions |
| `QF_LIA` | Linear Int arithmetic with bounded integer completion | Yes | Rebuild |
| `QF_UFLRA` | UF with linear Real arithmetic | Yes | Rebuild |
| `QF_UFLIA` | UF with linear Int arithmetic | Yes | Rebuild |
| `UF` with quantifiers | Enumerative instantiation; restricted SAT completion | No for quantified formulas | Rebuild |

Admission is checked by {source}`SmtSession <src/smt/smt_session.cpp#L271-L289>`;
the actual solver route is selected after preprocessing. A formula can simplify
to a Boolean or pure-theory problem despite its declared mixed logic.
The Real combination route retains a dedicated coordinator, while Int
combination and quantified UF use the general component driver. See
[architecture](implementation/architecture.md) and
[combination](implementation/combination.md).

## Limits

Integer search can exhaust its branch/resource budget and return `unknown`.
Quantified UF can prove contradictions but has no general SAT-completeness
claim. Its supported SAT cases and inconclusive boundary are described in
[quantifiers](implementation/quantifiers.md).

Arrays, bit-vectors, nonlinear arithmetic, quantified arithmetic, general mixed
Int/Real input, and proof production are not implemented. `QF_IDL` and `QF_RDL`
are not admitted as separate logic names. Integer `div`, `mod`, and `abs` are
accepted extensions; division and remainder require a nonzero literal divisor.
These extensions are not a claim of strict linear-logic grammar conformance.

`get-value`, `get-unsat-core`, `get-proof`, `check-sat-assuming`, and global
declarations are unsupported. The [frontend](implementation/frontend.md)
documents character extensions and other lexical limitations.

Models require enabling production before selecting the logic, then a current
SAT result. Context changes invalidate the session's current model. Internal
model checking and independent validation are separate operations.

## Qualification and maintenance

The {source}`run ledger <docs/run-log.json>` records measured revisions,
commands, resources, failures, and artifacts. The
{source}`previous qualification snapshot <docs/archive/current-status.md>`
and {source}`shared-assignment qualification <docs/current-status.md#L81-L109>`
remain dated evidence. They do not qualify every later checkout or option.

Use [options](reference/options.md) for defaults, and
[correctness requirements](development/correctness.md) when changing behavior.
Source revision metadata belongs to the [review ledger](review.md); it is not
repeated in each chapter's prose.
