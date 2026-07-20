# Failure taxonomy

## Solver statistics

Use `--stats` to print numeric counters to stderr while leaving stdout as the
single SMT result line:

```bash
./build/primo --stats input.smt2
```

Statistics are intentionally stable, colon-prefixed keys so benchmark scripts can
compare runs mechanically. Hot solver paths use an internal `StatKey` enum for
registered counters; the printed names below are the compatibility boundary for
scripts and result artifacts. Current counters include:

- `:input.assertions`;
- `:frontend.ite.boolean`;
- `:frontend.ite.auxiliaries`;
- `:preprocessing.assertions`;
- `:preprocessing.passes`;
- `:preprocessing.changed-passes`;
- `:preprocessing.adaptive-passes`;
- `:preprocessing.term-size.initial`;
- `:preprocessing.term-size.final`;
- `:preprocessing.eq-bridge.equalities`;
- `:preprocessing.eq-bridge.skipped-or-nodes`;
- `:preprocessing.boolean-units.facts`;
- `:preprocessing.boolean-units.rewrites`;
- `:preprocessing.forced-atoms.facts`;
- `:preprocessing.forced-atoms.disequalities`;
- `:preprocessing.forced-atoms.rewrites`;
- `:preprocessing.unit-equality.consequences`;
- `:preprocessing.unit-equality.substitution-units`;
- `:preprocessing.unit-equality.substitutions`;
- `:preprocessing.unit-equality.substitution-accepted`;
- `:preprocessing.unit-equality.substitution-rejected`;
- `:preprocessing.solve-eqs.candidates`;
- `:preprocessing.solve-eqs.accepted`;
- `:preprocessing.solve-eqs.rewrites`;
- `:preprocessing.solve-eqs.rejected-cycles`;
- `:preprocessing.solve-eqs.rejected-blowups`;
- `:preprocessing.finite-domain.domains`;
- `:preprocessing.finite-domain.amo-clauses`;
- `:preprocessing.finite-domain.evaluated-equalities`;
- `:preprocessing.finite-domain.evaluation-cutoffs`;
- `:preprocessing.equality-definitions`;
- `:preprocessing.equality-definitions.candidate-pairs`;
- `:preprocessing.equality-definitions.accepted-pairs`;
- `:preprocessing.equality-definitions.predicted-clauses`;
- `:preprocessing.equality-definitions.rejected-domain-size`;
- `:preprocessing.equality-definitions.rejected-clause-limit`;
- `:preprocessing.ground-congruence-clauses`;
- `:preprocessing.ground-congruence-skipped-groups`;
- `:preprocessing.uf-symmetry.accepted-sets`;
- `:preprocessing.uf-symmetry.values`;
- `:preprocessing.uf-symmetry.terms`;
- `:preprocessing.uf-symmetry.constraints`;
- `:preprocessing.uf-symmetry.value-precedence-terms`;
- `:preprocessing.uf-symmetry.value-precedence-constraints`;
- `:preprocessing.uf-symmetry.rejected-noninvariant`;
- `:preprocessing.auto.single-assertion`;
- `:preprocessing.auto.strong`;
- `:preprocessing.auto.basic-single`;
- `:preprocessing.auto.basic-multi`;
- `:preprocessing.auto.large-assertion-skip`;
- `:solver.boolean`;
- `:solver.qf-uf`;
- `:sat.phase.theory-negative`;
- `:sat.phase.auto-polarity`;
- `:sat.phase.auto-basic-single-predicate-polarity`;
- `:sat.phase.auto-basic-multi-polarity`;
- `:sat.vars`;
- `:sat.clauses`;
- `:sat.clause-lits`;
- `:sat.unit-clauses`;
- `:sat.binary-clauses`;
- `:sat.solve-calls`;
- `:euf.theory-atoms`;
- `:euf.predicate-atoms`;
- `:euf.pure-filter.enabled`;
- `:euf.pure-atoms.positive`;
- `:euf.pure-atoms.negative`;
- `:euf.pure-literals.filtered`;
- `:euf.assignments`;
- `:euf.eq-assignments`;
- `:euf.diseq-assignments`;
- `:euf.checks`;
- `:euf.theory-conflicts`;
- `:euf.propagation.rounds`;
- `:euf.propagation.candidates`;
- `:euf.propagation.delivered`;
- `:euf.propagation.reason-lits`;
- `:euf.propagation.budget`;
- `:euf.propagation.delivery-budget`;
- `:euf.propagation.budget-cutoffs`;
- `:euf.propagation.hit-rate-cutoffs`;
- `:euf.propagation.auto-online`;
- `:euf.propagation.auto-conflict`;
- `:euf.propagation.auto-off`;
- `:euf.incremental.terms`;
- `:euf.incremental.flat-definitions`;
- `:euf.incremental.merges`;
- `:euf.incremental.pending-pops`;
- `:euf.incremental.lookup-probes`;
- `:euf.incremental.lookup-hits`;
- `:euf.incremental.congruence-merges`;
- `:euf.incremental.disequality-checks`;
- `:euf.incremental.conflicts`;
- `:euf.incremental.explain-calls`;
- `:euf.incremental.explain-cache-hits`;
- `:euf.incremental.explain-length`;
- `:euf.incremental.explain-proof-forest.shorter-than-bfs`;
- `:euf.incremental.explain-proof-forest.equal-to-bfs`;
- `:euf.incremental.explain-proof-forest.longer-than-bfs`;
- `:euf.incremental.backtracks`;
- `:euf.incremental.trail-entries`;
- `:euf.incremental.class-member-trail-entries-saved`.

For preprocessing-only comparisons, pass `--preprocess-only --stats`. Primo
then parses and preprocesses the input, prints `unknown`, skips SAT/theory
solving, and emits the preprocessing counters. Do not use this mode for
benchmark solved counts.

`:euf.theory-conflicts` counts EUF conflict clauses learned either by the
ordinary post-model check or by the CaDiCaL external-clause callback used by
`--euf-prop=conflict|online`.

`:euf.assignments`, `:euf.eq-assignments`, and `:euf.diseq-assignments` count
theory-literal notifications received by the online/conflict EUF callback.
Predicate atoms contribute only to `:euf.assignments`.
The callback keeps assignment and pending-propagation membership in fixed
arrays indexed by SAT variable. If these counters change without a matching
change in `:euf.theory-atoms`, inspect the observed-variable map and
`notify_backtrack` rollback path before treating the run as a solver-search
effect.

The propagation counters are used by both `--euf-prop=model` and
`--euf-prop=online`; `--euf-prop=conflict` leaves them at zero. In online mode,
one round corresponds to a callback pass that inspects currently unassigned
observed theory atoms. When `--euf-prop=auto` selects online callbacks, repeated
zero-delivery scans increase the discovery interval, so the candidate counter
can be much smaller than `rounds * theory-atoms`.

For difficult instances, `--max-theory-conflicts N` returns `unknown` after at
least `N` learned theory conflicts. In lazy mode this is checked after each
post-model theory conflict. In online/conflict callback mode Primo also installs
a CaDiCaL terminator, so a long in-progress SAT call can stop soon after the
callback reaches the limit. This is intended for profiling and stats snapshots
on timeout-prone benchmarks; it is not a solving heuristic.

When the CLI is run with `--stats`, it keeps the current counters in a live
statistics sink. On `SIGTERM` it prints `unknown` to stdout and flushes the
partial counters to stderr before exiting. The benchmark harnesses send
`SIGTERM` first and only use `SIGKILL` after a short grace period, so timeout
rows can still contain diagnostic counters.

The auto counters record which concrete path `--euf-prop=auto` selected for the
current abstraction.

Add a counter when implementing a nontrivial preprocessing rule, generated
lemma, SAT interaction, or theory-solver action. Keep names stable once they are
documented; add a new key instead of changing the meaning of an old one.

## Fuzzing and Reduction

Primo follows the Brummayer-Biere SMT fuzzing/debugging pattern: generate or
mutate well-sorted SMT-LIB formulas, compare Primo against trusted reference
solvers, and reduce each crash, timeout, unsupported-input regression, or answer
disagreement to a smaller reproducer.

Current entry points:

- `scripts/yinyang_fuzz.sh` runs YinYang/typefuzz against Primo and a reference
  solver;
- `make fuzz` runs the same path when `typefuzz` is installed and otherwise
  records a clear local skip;
- `make fuzzsmt` runs Brummayer's FuzzSMT generator for `QF_UF`, compares
  Primo with Z3, and copies disagreements or crashes under
  `fuzz-results/qf_uf-fuzzsmt/bugs`;
- `scripts/compare_with_z3.py` classifies Primo/reference outcomes;
- `scripts/bench_interesting_qf_uf.py` records `file,status,answer,seconds`
  plus family and PAR2 summary rows for the current hard QF_UF target families
  under `tmp/QF_UF`; pass `--expected expected.csv` with `file,answer` columns
  to count checked wrong answers;
- `scripts/minimize_smt2.py` delegates to ddSMT when `ddsmt` is installed and
  otherwise copies the input unchanged with a clear message.

Use YinYang for mutation of real SMT-LIB seeds and FuzzSMT for grammar-based
black-box generation. Both are useful: YinYang stays close to benchmark shapes,
while FuzzSMT stresses frontend and Core/QF_UF coverage with independently
generated formulas. For QF_UF, the real-formula seed corpus must include
representative benchmark-family shapes, not just tiny generic satisfiable and
unsatisfiable formulas. In particular, keep seeds for QG-style finite-domain
operation tables, Goel hardware-style formulas, NEQ/PEQ/SEQ finite-domain
choices, and any minimized bug reproducer that previously escaped fuzzing.

Long fuzzing campaigns should run on SLURM, not on developer workstations.
Preserve generated failures under `yinyang_bugs/` until each one is copied back,
classified, minimized, and either turned into a regression test or documented as
an unsupported feature.

### Required fuzz gates

Fuzzing is a required correctness gate for behavior changes. Run a fresh fuzz
gate against the current commit before considering changes complete when they
touch:

- parser or printer behavior;
- type checking and sort handling;
- term construction, rewriting, or substitutions;
- preprocessing transformations;
- clausification and Boolean abstraction;
- SAT backend integration or solver control flow;
- theory solvers, theory propagation, conflicts, explanations, or models;
- command-line options that alter solving behavior.

Use three gate sizes:

- **Smoke gate:** at least 100 generated or mutated inputs for narrow
  parser/core changes with low solver impact, plus the focused unit tests.
- **Merge gate:** at least 1000 generated or mutated inputs against a trusted
  reference for ordinary solver, preprocessing, SAT, or theory behavior changes.
- **Campaign gate:** at least 3000 generated QF_UF FuzzSMT-style cases for
  QF_UF default/profile changes, plus YinYang/typefuzz over representative real
  benchmark seeds. Other theories should define comparable generator and
  seed-mutation counts before becoming headline targets.

For QF_UF work, the campaign gate is the minimum before promoting a new
preprocessing transformation, external propagation mode, SAT profile, or theory
engine to default behavior. A seed-mutation run that covers only the generic
`benchmarks/seeds/qf_uf/seed_basic.smt2` shape is smoke evidence, not campaign
evidence. If the generator, representative seeds, or reference solver is
missing, install or add them when feasible; otherwise record the failed command
and treat the missing fuzz gate as incomplete verification.

Every fuzz artifact should record:

- Primo commit hash;
- generator name, seed, count, and timeout;
- Primo command line and reference command line;
- host or SLURM job id;
- pass, wrong-answer, crash, timeout, unsupported, parser-error, model-error,
  and reference-disagreement counts;
- failure directory with generated inputs and logs.

### Fuzz result policy

Wrong answers, crashes, invalid models, parser rejection of valid generated
input, and reference disagreements block performance work until fixed or
classified. Do not report a performance result as headline-ready while these
failures remain open.

Timeouts from fuzzing are diagnostic rather than immediate correctness failures.
Report them separately from passes and use them to seed performance triage only
after answer correctness is clean.

Old `bugs/` directories are not current evidence. Before citing an old fuzz
campaign, rerun saved bug files against the current binary and references. Each
file should end in one of these states:

- regression test added;
- documented unsupported feature;
- current bug still open with a minimized reproducer;
- stale failure ignored because it no longer reproduces.

## Result failures

- `UNSOUND_SAT`: Primo says sat, trusted solver says unsat.
- `UNSOUND_UNSAT`: Primo says unsat, trusted solver says sat.
- `UNKNOWN_REGRESSION`: Primo now says unknown on a previously solved case.
- `REF_DISAGREEMENT`: reference solvers disagree.

## Runtime failures

- `CRASH`
- `ASSERTION_FAILURE`
- `TIMEOUT`
- `MEMORY_LIMIT`
- `NONTERMINATION`

## Model failures

- `INVALID_MODEL`
- `PARTIAL_MODEL`
- `SORT_INCORRECT_MODEL`
- `FUNCTION_INTERPRETATION_MISSING_DEFAULT`

## Infrastructure failures

- `PARSER_REJECTS_VALID_INPUT`
- `PRINTER_PRODUCES_INVALID_SMTLIB`
- `TYPECHECKER_ACCEPTS_INVALID_INPUT`
- `NONDETERMINISTIC_OUTPUT`
