# Current implementation and qualification

**QF_LRA default profile (2026-10-10):** source `f0fbe67` enables model-phase
hints and monotone elimination automatically for declared QF_LRA rebuild
queries with `preprocessing-mode=auto`. Each mechanism has an independent
explicit opt-out. Matching compute CPUs give 1,525 versus 1,461 solves out of
1,753 at 30 seconds and 18.68% lower PAR2. Separate Gold-host comparisons
confirm gains at both 30 and 1,200 seconds. All 6,652 successful native SAT
runs passed independent direct Dolmen replay; fresh generated and real-seed
mutation campaigns found no wrong answers, crashes or reference disagreements.
Resource-only incomplete checks and two successful model-timeout retries remain
recorded separately. See [](qf-lra-heuristics.md) for tradeoffs, exact source
relations, reference versions, resource limits and the paper comparison.

**Direct stdin parser (2026-09-10):** `e24dfcd` uses a persistent stream lexer
and one-command parser, with no separate text framer or lookahead after a
completed command. The fresh 1,000-session recorded-reference replay and 100
live-pipe chunking seeds pass. The subsequent [attribute repair](lexical-conformance.md)
rejects malformed metadata values and annotation sequences, including the same
value-category error in `set-option`. Generic attribute syntax is validated;
attribute-specific semantics and complete syntax conformance are not claimed.
Qualification of each revision is recorded separately in the run log.

**Lexical repair (2026-09-10):** [](lexical-conformance.md) records the original
quoted-name wrong answers, their canonical-identity/category repair, and the
selected compatibility policy. Leading-zero numerals and unquoted reserved-word
names are rejected. All 34 valid audit cases pass; 1,000 generated sessions match
recorded Z3/cvc5 results. ANTLR is removed from the build at `6e4ff80`, which has
identical owned parser/solver sources and a fresh hash-checked replay. Detailed
qualification and limitations remain in the run log; this is not a full-corpus
evaluation or a blanket branch merge-readiness claim.

This is the current-state map for `main` after the incremental-session, QF_UF
and atom-scan merges on 2026-09-11. Merge `3169a1c` integrates
`feature/incremental-smt-sessions` at `5d930be`; merge `10b69c7` integrates
`feature/assumptions-qf-uf` at `7a98245`. Each merge preserves the corresponding
feature branch's exact tree. Those first two merges retain the UF baseline
qualified at `fa2fe5c`;
the theory-combination implementation and its earlier qualification remain at
`b883d6f`/`d4e604e`. Rebuild remains the default, with QF_LRA/QF_UF assumptions
mode opt-in. Merge `40adb28` integrates `perf/incremental-uf-atom-scans` at
`3a96e86`, preserving its exact tree. Its separately qualified `7ac555c` solver
change removes repeated UF atom scans; see [](incremental-uf-atom-scans.md).
Traversal scratch and the other profiling leads remain open. The separate
[OpenSMT comparison](opensmt-incremental-comparison.md) adds native UF/LRA
reference timings and records external crashes without changing solver defaults.
Merge `90ac7e9` on 2026-09-15 integrates `quantifiers` at `42a9915`, preserving
its exact tree: quantified `UF` by enumerative instantiation, qualified at
`ff72a12` (with `c7bc7c0` merged in), `72d00d3` and `3b9264a`; see
[](quantifiers.md).
Dated experiments elsewhere retain their original revisions
and limitations. Exact commands, manifests, hashes and jobs belong in
[](run-log.md), not in a moving headline benchmark.

## Supported routes

Dispatch uses both the declared logic and preprocessed formula. In particular,
QF_UFLRA can simplify to a pure-theory or Boolean leaf; model requests can still
require its mixed route to restore eliminated arithmetic. QF_LIA and QF_UFLIA
select their integer leaves explicitly, except for terminal preprocessing
results. A logic declaration alone is therefore not a reliable activation counter.

| Formula/logic | Production solving route | Model construction |
| --- | --- | --- |
| Boolean-only outside explicit integer routing | Direct SAT leaf | `build_typed_model` |
| QF_UF | Incremental flat EUF by default; lazy diagnostic engine retained | `build_typed_model` |
| QF_LRA | Exact bounded-variable Simplex, online arithmetic checks | `build_typed_model` |
| QF_LIA | Arithmetic relaxation and bounded integer completion | `build_typed_model` |
| QF_UFLRA | Dedicated convex EUF/LRA coordinator and shared-Real reconciliation | `build_typed_model` with compatible component inputs |
| QF_UFLIA | EUF/LIA recipe through the N-component driver and online dispatcher | Validated component fragments and `assemble_component_model` |
| UF with quantifiers | EUF plus an enumerative-instantiation component through the N-component driver; `unsat`, Skolem-covered `sat`, otherwise `unknown` | None: model requests are `unsupported` |

All model routes freeze the same owning `core::Model`, replay goal-owned
conversions, and use `core::ModelEvaluator` and `smtlib::print_model`. A function
symbol and evaluated typed argument tuple have exactly one result. Printing
cannot repair or replace that result. See [](model-interpretation.md).

The frontend accepts the five named logics above and `UF`, nonrecursive
`define-fun`, typed `let`, ITEs and the documented linear arithmetic operators.
Quantified assertions skip preprocessing: in `UF` they go to the quantified
route above, and in any other logic, or in assumptions mode, they are
`unsupported`. On the SMT-LIB UF division at 60 s, the route at `72d00d3`
solves 1,948 of 7,590 files with its defaults: the `sum` tuple order, rounds
of 128 instances and the stall cutoff. All but one answer are `unsat`, with no
wrong answers or reference disagreements. Z3 solves 2,765 and cvc5 3,154; cvc5
restricted to enumerative instantiation without E-matching solves 2,930. An
independent check with cvc5 pools over Primo's own instantiations confirms
1,601 of those `unsat` answers and refutes none. The earlier tuple-order
comparison predates the Boolean-domain repair; see [](quantifiers.md). `div`, `mod`
and `abs` are deliberate Int extensions to the accepted logic declarations;
division requires a nonzero literal divisor. They are not a claim of strict
QF_LIA/QF_UFLIA grammar conformance, and a Dolmen logic rejection is not a model
pass. See [](arithmetic.md) and [](preprocessing.md).

Sessions support interactive SMT-LIB `push`/`pop` with scoped
declarations/definitions and repeated checks. Solving state is rebuilt per
query by default. An opt-in `--incremental-mode=assumptions` now retains frontend,
SAT and prepared QF_LRA or QF_UF structure, with fresh working theory state
for each check. The separate QF_LRA bounded gate passed 2,000 scoped
sessions, 16,000 answers and 10,802 independently checked SAT models; see
[](assumption-incremental-lra) for coverage and measured limitations.
QF_UF qualification at `fa2fe5c` passed 6,000 generated/mutated sessions. On the
1,778-session native archive, assumptions completed 1,772 sessions with 25,027
independently validated SAT models; six conflict-limited sessions remain
unqualified. Rebuild remains the default. See [](assumption-incremental-uf) for
paired results, Goel losses and evidence limits, and [](uf-z3-profiling) for the
optimization leads and their current disposition in [](open-leads.md).
For the rebuild baseline, SLURM qualification matched 15,000 answers across 1,000 generated
sessions and independently validated 10,000 session models. A separate
100-input, five-logic sample retained every solve and shared non-timing counter;
two paired rounds cost 0.75% more overall, with local family regressions.
All 122 SAT results from those rounds passed separate Dolmen replay.
This is bounded baseline qualification, not a full-corpus or zero-overhead claim.
See [](incremental-smt.md).

Not implemented: quantified arithmetic, E-matching and conflict-based
instantiation, arrays, bit-vectors, nonlinear arithmetic,
proof production, or dedicated QF_IDL/QF_RDL admission.
Within-query SAT/theory callbacks are distinct from cross-query reuse, which
requires the opt-in assumptions engine and its reconnection contract. Integer
search and explicit resource budgets can return `unknown`. Unsupported input,
cancellation, resource exhaustion and internal model errors are distinct outcomes,
not UNSAT.

## Extensibility and retained specialized paths

The production extension boundary is `theory::TheoryComponent`, not a hard-coded
EUF/LIA pair. Descriptors declare ownership, shared sorts, convexity, stable
infiniteness and optional capabilities. `TheoryCombinationPlan` validates these
claims and normalizes connected sharing scopes; components outside a scope do
not participate in its comparisons. The coordinator compares validated model
partitions, while the dispatcher routes SAT assignment deltas and explanations.
The driver owns SAT assumptions, failed-core clauses and complete fallback.

Three/four-component tests exercise the real SAT driver and fragment assembly.
They demonstrate the protocol, **not** support for three/four production
theories. New theories still need kernels, frontend admission, purification,
ownership rules, model exports, tests and corpus qualification. Joint stable
infiniteness on shared sorts and disjoint interpreted signatures are admission
preconditions; finite shared domains such as Bool are not admitted to this
arrangement protocol. Bool is coordinated through SAT and the EUF bridge.

Convexity is metadata with a semantic obligation, not a switch that makes LIA
convex. Model coincidences justify reversible choices, never explained lemmas.
Care pairs rank choices; they do not replace complete non-convex arrangements.
Ordinary disagreement selection is now a linear, order-preserving scan; explicit
mutation batches and the complete-arrangement fallback are unchanged.
See [](n-way-combination-migration.md) for types, invariants and paper scope.

The old dedicated QF_UFLIA scheduler has been replaced. No further dedicated
route is retired in this revision:

- Pure UF/LIA/LRA keep their direct fast paths.
- QF_UFLRA stays frozen relative to the migration baseline. Its
  `--experimental-generic-model-combination` uses the **older** projection
  coordinator, not the new collection driver; the option is not a default
  promotion or an engine replacement.
- Complete-arrangement fallback remains part of the correctness design.
- `ComponentLraExecution` versus the retained `OnlineLraPropagator`, and the
  fragment builder versus the retained typed builder, are transitional
  duplication. Factoring their common internals is future work, requiring
  behavior-preserving tests and paired qualification. It has not been done.

## Selecting and checking a profile

`./build/primo --help` is the exhaustive CLI inventory; `src/smt/check_sat.hpp`
owns option defaults, and strategy construction determines when an option is
reachable. Important current boundaries are:

| Control | Current default and scope |
| --- | --- |
| `--preprocessing-mode` | `auto`; also accepts `off`, `basic`, `strong`. `off` does not bypass mandatory lowering or integer-safe required preparation. |
| `--auto-preprocess-selector` | `legacy`; `structural` remains opt-in. |
| `--lra-model-phase` / `--no-lra-model-phase` | Automatic for declared QF_LRA/rebuild/auto preprocessing; independently disable with the negative flag. Explicit enabling remains available. |
| `--monotone-elimination` / `--no-monotone-elimination` | Same automatic scope; independently disable with the negative flag. Assertion-dependent elimination remains incompatible with assumptions mode. |
| `--euf-engine` | `incremental`; `lazy` is a diagnostic alternative. |
| `--no-qf-uflia-model-combination` | Guidance is on; disabling selects complete arrangements through the same collection driver. |
| `--no-qf-uflia-online-components` | Online components are on with the incremental engine; integer completion remains a final check. |
| `--qf-uflia-model-decision-budget=N` | `512`; exhausting model choices retains complete fallback, not an UNSAT shortcut. |
| `--no-qf-uflra-model-reconciliation` | Dedicated Real reconciliation is on; disabling selects its explained exact path. |
| `--lra-fixed-elimination` / `--no-qf-uflra-fixed-elimination` | Off for pure LRA, on for dedicated QF_UFLRA; these are separate settings. |
| `--check-model` | Checks original assertions when a model is requested; it does not itself request one. |

For example, save this as an SMT-LIB input and run
`./build/primo --check-model input.smt2`:

```lisp
(set-option :produce-models true)
(set-logic QF_UFLIA)
(declare-fun f (Int) Int)
(declare-fun x () Int)
(declare-fun y () Int)
(assert (= x 2))
(assert (= y (+ (f (+ x 1)) 1)))
(check-sat)
(get-model)
```

The chosen value of `f(3)` is not prescribed, but the model must satisfy
`y = f(3) + 1`. Reconstruction and the printed table must use the same value.
This illustrates the evaluated-tuple regression, not an independent proof that
all model inputs are correct. Use the separate Dolmen procedure below for that
external check.

## Evidence boundary

The N-component migration at `2413397` was qualified against `75d5177` (solver
sources identical to `287b743`), **not against current main**. Two 60-second
paired rounds covered all five corpora. Counts below are solved executions over
both rounds, not unique instances.

| Logic | Inputs | Baseline → migration solved executions | PAR2 change |
| --- | ---: | ---: | ---: |
| QF_UF | 7,503 | 14,990 → 14,990 | +0.42% |
| QF_LIA | 13,306 | 19,404 → 19,417 | −0.124% |
| QF_LRA | 1,753 | 3,076 → 3,075 | +0.112% |
| QF_UFLRA | 1,284 | 2,540 → 2,540 | +0.22% |
| QF_UFLIA | 659 | 1,142 → 1,149 | −2.15% |

All original per-logic 1% regression limits passed, and prescribed lost-solve
follow-ups finished. Every candidate SAT row in these five main evaluations
received an independent Dolmen pass: **23,405 models**. Reference timeouts leave
some UNSAT rows corroborated only by declared status and/or previous Primo
results; this is not universal independent UNSAT certification. Follow-up
both-timeout rows also remain inconclusive.

The subsequent `b883d6f` optimization has **separate QF_UFLIA-only** evidence
against `2413397`. On all 659 inputs, both versions solved 575 per round
(405 SAT, 170 UNSAT, 84 timeouts), with no gained/lost solve. Common-solved time
fell 2.76%, PAR2 fell 0.312%, and all 425,500 compared existing nontiming counters
matched across 1,150 solved pairs. This is unchanged measured search counters,
not a full execution-trace proof or a uniform per-instance speedup.
All 810 full-corpus SAT replays and 26 quick-sample SAT replays passed Dolmen.

Its generated gate passed all 4,000 cases, including 2,032 SAT-model checks.
The selected mutation profiles retained 1,435 cases: 1,398 answer passes and
37 timeouts, with 951 SAT-model passes. Raw nonzero mutation job exits are
retained and classified as resource-only, not rewritten as all-pass jobs.
No wrong answer, crash or invalid model was found in the accepted gates.
Original migration campaigns and their additional resource rows are detailed
in [](n-way-combination-migration.md); they must not be relabeled as fresh
five-logic validation of the later optimization.

Run-log records: `2026-09-08-n-component-preservation-four-logics` and
`2026-09-09-linear-component-selection`. Local artifact roots are
`results/n-way-2413397/` and `results/n-way-linear-b883d6f/`; these large artifacts
are not source files. The tracked run log preserves their provenance and remote
locations. No new benchmark was required for this documentation refresh.

## Testing and maintenance

Timing runs and correctness replay are separate stages. Preserve original
timing rows; replay **every successful SAT row** with the same frozen binary
and solver arguments, request a model, and validate it with Dolmen. Resource
limits, changed answers, invalid models and missing coverage remain visible.
Internal checking evaluates original assertions but shares Primo's evaluator;
it cannot independently certify the evaluator or serializer. Unit tests must
also challenge the checker with false assertions and malformed interpretations.

Reuse reference results by input hash and recorded provenance. Rerun references
only for missing/ambiguous evidence or an explicitly requested new comparison.
See [](experiments.md) for the evidence chain, [](debugging.md) for failure
classification, and [](run-log.md) for durable records.

Repository skills own the procedures: `skills/testing/`, `skills/fuzzing/`,
`skills/benchmarking/`, `skills/primo-model-validation/`, and `skills/code-review/`.
`AGENTS.md` keeps the obligations and routing, not duplicated job recipes.
Documentation changes require a strict `make docs`; behavior changes additionally
require the appropriate tests, ASan for lifetime-sensitive work, fresh fuzzing,
and qualification before default/performance claims.

## Sources and implementation status

This page is a code/evidence index, not a new algorithm proposal.
[](architecture.md) owns the representation and lifetime contract;
[](model-interpretation.md) is the original model/replay design;
[](n-way-combination-migration.md) maps the Nelson-Oppen/delayed/model-based
combination sources to the implemented collection protocol and its proof
obligations. [](nelson-oppen.md) records the separate convex subset, and
[](arithmetic.md) / [](qf-lia-branch-and-bound.md) delimit the arithmetic paper
mechanisms. An extension interface is not a claim to implement every algorithm
or theory described in those sources.
