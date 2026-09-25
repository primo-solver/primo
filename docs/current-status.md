# Current implementation and qualification

This is the **done** entrance: the implemented user interface, its limitations,
and the evidence available for it. Start with [](guide/getting-started.md) to
build and run Primo. Work underway belongs in [](active-work.md); possible next
work belongs in [](open-leads.md).

**Documented solver source:** {{ source_revision }}. The manual links to this
reviewed source revision. Its {source}`command interface <src/main.cpp>`
and {source}`solver entry points <src/smt/solver_engines.cpp>` are linked to that
commit. New guide examples are small reproducibility checks, not a fresh
full-corpus qualification. Historical results keep their own measured revisions.

## Supported routes

| Declared logic | Implemented solving scope | Models | Session engines |
| --- | --- | --- | --- |
| `QF_UF` | Quantifier-free equality and uninterpreted functions | Yes | Rebuild; opt-in assumptions |
| `QF_LRA` | Linear Real arithmetic | Yes | Rebuild; opt-in assumptions |
| `QF_LIA` | Linear Int arithmetic, with bounded integer completion | Yes | Rebuild |
| `QF_UFLRA` | Uninterpreted functions with linear Real arithmetic | Yes | Rebuild |
| `QF_UFLIA` | Uninterpreted functions with linear Int arithmetic | Yes | Rebuild |
| `UF` with quantified assertions | Enumerative instantiation: contradictions and restricted covered SAT cases; otherwise `unknown` | No for quantified formulas | Rebuild |

All rows remain subject to frontend admission, resource limits and the
limitations below. A declared logic does not uniquely identify the execution
path: preprocessing may discharge the problem or expose a pure-theory/Boolean
leaf. SAT support is delegated to CaDiCaL through IPASIR-UP.

Models require `:produce-models true` before `set-logic`, followed by a
successful `check-sat` and `get-model`. See [](guide/models.md).
Repeated queries, `push`/`pop` and scoped declarations are supported in the
**default rebuild engine**. Assumptions mode retains more internal state but
has additional input/option restrictions; see [](guide/sessions.md).

## Limits and unsupported features

- Integer search is bounded and can return `unknown`. Accepted input is not a
  guarantee that Primo will decide it within the configured resources.
- Quantified UF has no general SAT-completeness claim and no printed-model
  support. Quantified arithmetic, E-matching and conflict-based instantiation
  are not implemented.
- Arrays, bit-vectors, nonlinear arithmetic, proof production, and dedicated
  `QF_IDL`/`QF_RDL` admission are not implemented.
- `get-value`, `get-unsat-core`, `get-proof`, `check-sat-assuming` and global
  declarations are not implemented.
- `div`, `mod` and `abs` are accepted Int extensions, not a claim of strict
  QF_LIA/QF_UFLIA grammar conformance. Division/remainder requires a nonzero
  literal divisor. Full SMT-LIB conformance is not claimed.

Read [](guide/input.md) for commands and result semantics. In particular,
`unknown`, `unsupported` and command errors must never be interpreted as UNSAT.

## Extensibility and retained specialized paths

Primo separates parsing, terms/models, SAT, SMT coordination and theory solvers.
Current architecture explanations remain in [](architecture.md),
[](sat-ipasir-up.md) and [](dpllt.md) while their rewrite proceeds.

| Component | Current implementation reference |
| --- | --- |
| EUF and Boolean arguments | [](euf.md), [](euf-boolean-arguments.md) |
| Real and integer arithmetic | [](arithmetic.md), [](qf-lia-branch-and-bound.md) |
| Dedicated convex Real combination | [](nelson-oppen.md) |
| N-component interface and the production Int combination route | [](modular-theory-combination.md), [](n-way-combination-migration.md) |
| Quantified UF | [](quantifiers.md) |
| Owning models and reconstruction | [](model-interpretation.md) |

The general component interface is an extension boundary, not evidence that
additional production theories have been implemented. The Real combination
route remains specialized. Opt-in experiments and proposals in the existing
chapters are not additional defaults.

## Selecting and checking a profile

Begin with the default command line. The [option reference](reference/options.md)
owns the user-facing defaults and links to the captured CLI help. Opt-in modes
need their own qualification; neither a successful build nor a small example
justifies promoting one to default.

## Shared assignment ownership

Every dedicated, mixed, component, quantified-UF and retained-session route
now uses one Primo-side assignment owner per SAT connection. Consumers borrow
`sat::TrailView`; their theory undo logs remain separate. This mirrors only
observed variables, while CaDiCaL owns its full SAT trail. Ordinary Tseytin
auxiliaries stay in SAT; generated Boolean terms used as function arguments
also have a theory meaning. See [](dpllt.md) for the protocol and lifetime rules.

The implementation is `45e25a6`; the measured build `e4abefb` has identical C++
sources. Fresh SLURM gates cover all six logics, retained sessions, generated
quantified inputs and real-seed mutations. All 43 normal and ASan test groups
pass. The fixed 120-input comparison against `fd4d1eb` has unchanged solved
counts and no answer disagreements in four profiles; exact metadata is in
`benchmarks/shared-assignment-20260925/` and run-log entry
`2026-09-25-shared-assignment-slurm`.

This is bounded correctness/ownership evidence, not a full-corpus speed claim.
The default and offline-Int profiles narrowly exceed the comparison script's
strict 1% common-runtime threshold (1.05% and 1.23% respectively). Search
counters can change in the default mixed route. RSS includes the benchmark
harness and does not isolate the trail's memory cost.

Independent model replay covers all 240 successful SAT rows: 220 direct Dolmen
passes and 20 passes with its logic widened to `QF_NRA`, for five generated LRA
inputs rejected under the declared linear syntax. The saved assertions and
models are unchanged. Strict declared-logic model coverage remains incomplete
for those rows; they are not silently counted as direct passes. TypeFuzz's
1,615 malformed UF mutants are likewise excluded from passing-case counts.

## Evidence boundary

The [archived qualification snapshot](archive/current-status.md) preserves the
previous detailed counts, merge history, negative results and measured
revision boundaries. Exact commands, corpora, resource limits, model checks and
artifacts remain in [](run-log.md) and its `run-log.json` records.

Use the relevant evidence rather than combining unrelated gates:

- [Sessions](incremental-smt.md), [assumptions LRA](assumption-incremental-lra.md)
  and [assumptions UF](assumption-incremental-uf.md) have distinct qualification.
- [Component migration](n-way-combination-migration.md) records its baseline
  comparisons and later optimizations separately.
- [Quantified UF](quantifiers.md) records its correctness and corpus evidence,
  including limitations of independent corroboration.
- Run-log entry `2026-09-16-qf-uflra-forced-equality-merge` records the forced
  model-equality extraction merged into the documented source baseline.

No performance claim on this page automatically applies historical timings to
{{ source_revision }}. The rewrite's example/build checks are recorded in
[](documentation-restructuring.md), separately from solver evaluation.

## Testing and maintenance

Run `make test` for the ordinary test suite and `make docs` for the strict book
build. Run `python3 scripts/check_doc_examples.py --solver ./build/primo` for
the manual's examples and CLI-help snapshot. New solver behavior additionally
requires the repository's review, fuzzing and model-validation gates; see
[](contributing.md).

## Sources and implementation status

The {source}`session interpreter <src/smt/smt_session.cpp>`,
{source}`frontend <src/smt/frontend.cpp>`, and
{source}`solver options <src/smt/check_sat.hpp>` ground the capability map.
Algorithm chapters above retain paper references, implemented subsets and
limitations. Paper notes are a supporting library, not evidence of feature
completion by themselves; start at [](literature.md).
