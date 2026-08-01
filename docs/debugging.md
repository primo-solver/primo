# Failure taxonomy

## Solver statistics

Use `--stats` to print numeric counters to stderr while leaving stdout as the
SMT result stream:

```bash
./build/primo --stats input.smt2
```

For a model-producing script, stdout contains the result line followed by the
SMT-LIB model response when the input requests it:

```text
(set-option :produce-models true)
(check-sat)
(get-model)
```

```text
sat
(model
  (define-fun ...))
```

Statistics are intentionally stable, colon-prefixed keys so benchmark scripts can
compare runs mechanically. Hot solver paths use an internal `StatKey` enum for
registered counters; the printed names below are the compatibility boundary for
scripts and result artifacts. Current counters include:

- `:time.parse-us`: SMT-LIB parser elapsed time in microseconds;
- `:time.frontend-us`: command validation, declarations, macro expansion, and
  core term construction time in microseconds;
- `:time.preprocessing-us`: QF_UF preprocessing and pre-solve preparation time
  in microseconds;
- `:time.solve-us`: SAT/EUF solving time in microseconds;
- `:time.total-us`: total `check_sat_smtlib` time in microseconds, excluding
  CLI file I/O and benchmark-harness process overhead;
- `:input.assertions`;
- `:frontend.ite.boolean`;
- `:frontend.ite.auxiliaries`;
- `:frontend.define-fun.nullary-cache-hits`;
- `:frontend.define-fun.nullary-cache-misses`;
- `:frontend.define-fun.nullary-cache-entries`;
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
- `:sat.conflicts`;
- `:sat.decisions`;
- `:sat.propagations`;
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
- `:euf.propagation.delivered`, which counts accepted propagations stored for
  the external propagator callback;
- `:euf.propagation.reason-lits`;
- `:euf.propagation.budget`;
- `:euf.propagation.delivery-budget`;
- `:euf.propagation.budget-cutoffs`;
- `:euf.propagation.hit-rate-cutoffs`;
- `:euf.propagation.auto-online`;
- `:euf.propagation.auto-conflict`;
- `:euf.propagation.auto-off`;

The diagnostic-only disequality-propagation opportunity counters below are
populated when `--euf-diseq-prop-diagnostics` is enabled. They are zero in
ordinary runs:

- `:euf.diseq-prop.assigned-app-disequalities`;
- `:euf.diseq-prop.source-unary-candidates`;
- `:euf.diseq-prop.source-unary-existing-arg-atom`;
- `:euf.diseq-prop.source-unary-arg-atom-true`;
- `:euf.diseq-prop.source-unary-arg-atom-false`;
- `:euf.diseq-prop.flat-same-function-candidates`;
- `:euf.diseq-prop.flat-same-function-existing-arg-atom`;
- `:euf.diseq-prop.flat-same-function-arg-atom-true`;
- `:euf.diseq-prop.flat-same-function-arg-atom-false`;
- `:euf.diseq-prop.guarded-function-guard-candidates`;
- `:euf.diseq-prop.guarded-argument-guard-candidates`;

The diagnostic-only Ackermannization cost counters below are populated when
`--euf-ackermann-diagnostics` is enabled. They count distinct reachable
post-preprocessing UF applications and estimate the SAT-level functional
consistency constraints from Bruttomesso et al.'s LPAR 2006 cost model. They are
zero in ordinary runs:

- `:euf.ackermann.applications`;
- `:euf.ackermann.function-symbols`;
- `:euf.ackermann.eligible-symbols`;
- `:euf.ackermann.unary-symbols`;
- `:euf.ackermann.nary-symbols`;
- `:euf.ackermann.estimated-constraints`;
- `:euf.ackermann.estimated-equalities`;
- `:euf.ackermann.estimated-clause-lits`;
- `:euf.ackermann.max-symbol-applications`;
- `:euf.ackermann.max-symbol-arity`;
- `:euf.ackermann.symbols-cost-le-100`;
- `:euf.ackermann.symbols-cost-le-1000`;
- `:euf.ackermann.symbols-cost-le-10000`;

The dynamic Ackermann shadow counters below are populated when
`--euf-dynamic-ackermann-shadow` is enabled. They inspect only candidate
functional-consistency clauses whose result equality is already a SAT-visible
theory atom, and they classify a candidate as SAT-visible only when every
argument equality is also already a SAT-visible theory atom. The pass does not
add clauses or create missing atoms:

- `:euf.dynamic-ack.shadow.invocations`;
- `:euf.dynamic-ack.shadow.result-pairs`;
- `:euf.dynamic-ack.shadow.arg-atoms-missing`;
- `:euf.dynamic-ack.shadow.sat-visible-clauses`;
- `:euf.dynamic-ack.shadow.satisfied`;
- `:euf.dynamic-ack.shadow.unit`;
- `:euf.dynamic-ack.shadow.conflicting`;
- `:euf.dynamic-ack.shadow.result-diseq-assigned`;
- `:euf.dynamic-ack.shadow.all-args-eq-assigned`;
- `:euf.dynamic-ack.shadow.width-total`;
- `:euf.dynamic-ack.shadow.max-width`;
- `:euf.dynamic-ack.shadow.width-le-3`;
- `:euf.dynamic-ack.shadow.width-gt-3`;

The watched disequality-Ackermann counters below are populated by the
default-off `--euf-diseq-ackermann` online propagation experiment. They describe
candidate construction and queue-local firing for contraposed Ackermann clauses
over existing theory atoms. The option never creates missing equality atoms and
does not scan all candidates when the watched queue is empty:

- `:euf.diseq-ack.candidates`;
- `:euf.diseq-ack.queue-pushes`;
- `:euf.diseq-ack.queue-pops`;
- `:euf.diseq-ack.width-skips`;
- `:euf.diseq-ack.missing-arg-atom`;
- `:euf.diseq-ack.satisfied-skips`;
- `:euf.diseq-ack.unit`;
- `:euf.diseq-ack.conflict`;
- `:euf.diseq-ack.propagated`: pending propagations produced by the watcher;
- `:euf.diseq-ack.returned`: pending propagations actually returned from the
  CaDiCaL `cb_propagate` callback;
- `:euf.diseq-ack.pending-cleared`: pending Ackermann propagations discarded
  after assignment notification or backtracking before being returned;
- `:euf.diseq-ack.reason-lits`;
- `:euf.diseq-ack.queue-budget-cutoffs`;
- `:euf.diseq-ack.per-symbol-cutoffs`;

The incremental closure counters include:

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

Nelson-Oppen interface diagnostics are emitted in stats output when
`--nelson-oppen-diagnostics` is enabled. They are read-only counters from the
post-preprocessing assertions, not evidence that combination is enabled:

- `:no.interface.terms`
- `:no.interface.sorts`
- `:no.interface.candidate-pairs`
- `:no.interface.registry-entries`
- `:no.interface.visible-equalities`
- `:no.interface.missing-visible-equalities`
- `:no.purification.sites`
- `:no.purification.euf-real-terms`
- `:no.purification.arithmetic-uf-apps`

The diagnostic-only purification-site counters above identify mixed-atom
blockers: Real-sorted subterms inside EUF atoms and uninterpreted applications
inside arithmetic atoms.

The default QF_UFLRA path has its own runnable partial-combination counters:

- `:no.purification.definitions`
- `:no.purification.arithmetic-uf-definitions`
- `:no.purification.euf-real-term-definitions`
- `:no.purification.reused-applications`
- `:no.purification.reused-real-terms`
- `:no.purification.skipped-no-sites`
- `:no.interface.shared-real-terms`
- `:no.interface.sat-equality-atoms`
- `:no.interface.sat-equalities-true`
- `:no.interface.sat-equalities-false`
- `:no.interface.sat-equalities-forced`
- `:no.interface.equalities-propagated`
- `:no.interface.euf-to-lra-equalities`
- `:no.interface.lra-to-euf-equalities`
- `:no.interface.conflicts`
- `:no.interface.euf-conflicts`
- `:no.interface.lra-conflicts`
- `:no.interface.model-equality-candidates`
- `:no.interface.model-equality-created`
- `:no.interface.model-equality-assumptions`
- `:no.interface.model-equality-failed-assumptions`
- `:no.interface.model-equality-conflicts`
- `:no.interface.model-equality-full-pairs`
- `:no.interface.model-equality-care-pairs`
- `:no.interface.model-equality-care-pruned-pairs`
- `:no.interface.model-equality-scanned`
- `:no.interface.model-equality-lra-equal`
- `:no.interface.model-equality-selected-care`
- `:no.interface.model-equality-selected-full`
- `:no.interface.model-equality-skipped-assigned`
- `:no.interface.model-equality-skipped-tried`
- `:no.interface.model-equality-adaptive-stops`
- `:no.interface.model-equality-active-assumptions`
- `:no.interface.model-equality-max-active-assumptions`
- `:no.interface.arrangement-checks`
- `:no.interface.arrangement-failures`
- `:no.interface.arrangement-separations`
- `:no.interface.euf-care-checks`
- `:no.interface.euf-care-full-pairs`
- `:no.interface.euf-care-pairs`
- `:no.interface.euf-care-pruned-pairs`
- `:no.interface.lra-prepared-reused`
- `:no.interface.lra-prepared-augmented`
- `:no.interface.lra-conflict-lits`
- `:no.interface.lra-conflict-duplicates`
- `:no.interface.model-separations`
- `:no.interface.model-separation-candidates`
- `:no.interface.model-separation-search-capped`
- `:no.interface.model-separation-search-branches`
- `:no.interface.model-separation-search-backtracks`
- `:no.interface.model-separation-model-oriented`

These count internal proxy definitions in total, the split between Real-valued
UF applications in arithmetic atoms and non-constant Real terms in EUF atoms or
EUF-side application definitions, repeated uses of already-created application
or Real-term proxies, Real proxy terms visible to both EUF and LRA, equality
checks in each interface direction, positive SAT-visible shared-Real equality
atoms created lazily so far, how many of those generated atoms are true or false
in the last checked SAT assignment, how many were forced by EUF/LRA equality
propagation, and strict shared-Real
separations found while validating a final `sat` arrangement or reconstructing
mixed models. A false generated interface equality is asserted to EUF as a
disequality; it is not asserted to LRA as a native false-`Eq` atom. Ordinary
SMT-LIB Real disequality such as `(not (= x y))` is supported by Real equality
lowering and Boolean-level case splitting before prepared LRA atoms are asserted.
When default-off `--model-equality-branching` is enabled, the EUF care counters
compare the full shared-Real cross product with pairs that occur as arguments
under flat EUF function prefixes equal in the current EUF closure. Default
solving leaves these counters at zero because LRA-to-EUF equality propagation,
EUF-to-LRA equality propagation, model reconstruction, and final arrangement
validation use the full shared Real pair set. The default
`--model-equality-branch-policy=first` path also uses the full pair order and
reports the full pair count through `:no.interface.model-equality-care-pairs`
for continuity. The `ranked` and `adaptive` policies use care-pair diagnostics
only as a ranking signal; they fall back to the full pair set instead of pruning
the search.
`:no.interface.conflicts` counts ordinary EUF/LRA conflict clauses whose reason
mentions a generated interface equality literal, split by `:no.interface.euf-conflicts` and
`:no.interface.lra-conflicts`; direct single-theory conflicts that do not use
the generated interface atom are not included there. The model-separation search
counters record the number of strict-order candidate pairs, whether the bounded
backtracking cap was exceeded, and the LRA-consistency branches/backtracks tried
inside final arrangement validation or model reconstruction.
`:no.interface.model-separation-model-oriented` counts strict shared-Real
separations oriented directly from one concrete LRA model in the greedy capped
path, before falling back to per-candidate LRA consistency checks for pairs tied
in that model.
`:no.interface.lra-prepared-reused` counts mixed QF_UFLRA LRA checks that reuse
the base prepared LRA theory because no SAT-visible interface equality is true
in the current assignment. `:no.interface.lra-prepared-augmented` counts checks
that rebuild an augmented prepared LRA theory to include true generated
interface equality atoms. `:no.interface.lra-online-enabled` records whether the
default mixed coordinator attached the pure-QF_LRA trail-synced online
propagator for original arithmetic atoms, and
`:no.interface.lra-online-final-checks` counts final SAT assignments whose base
LRA consistency result came from that callback-owned state. This online path is
disabled for the experimental `--model-equality-branching` mode; when generated
interface equalities are true, the coordinator still rebuilds the augmented LRA
theory and performs an ordinary final check.
`:no.interface.arrangement-checks` counts non-model `sat` results where the
coordinator validated that the EUF arrangement over Real arguments can be
separated consistently by LRA, with failures and accepted strict separations
split into the corresponding arrangement counters. When
`--model-equality-branching` is enabled, the model-equality counters record
full shared Real pairs scanned, how many were equal in the current LRA model,
optional SAT equality atoms created for those pairs, active assumption attempts
over those atoms, assumptions rejected at the SAT level, assigned or
already-tried equality literals skipped, whether the selected guess came from
the care-ranked prefix or the full-pair fallback, adaptive stops, the current
and maximum active model-equality assumption-trail size, and ordinary theory
conflicts reached while at least one guessed equality was active. A surviving
model-equality guess is re-applied as an IPASIR assumption on later SAT calls
while the coordinator tries additional model-equal pairs; it is still not a
permanent unit clause. The current positive-equality path should
also show activity in
`:no.interface.sat-equalities-forced`, which counts guarded propagation clauses
from the current theory assignment to a generated interface equality literal,
and then ordinary EUF/LRA theory-conflict counters if the forced equality is
inconsistent.

Add a counter when implementing a nontrivial preprocessing rule, generated
lemma, SAT interaction, or theory-solver action. Keep names stable once they are
documented; add a new key instead of changing the meaning of an old one.

## Model Validation

Primo supports conservative pure-theory `get-model` responses for scripts that
set `:produce-models true` and call `get-model` after `check-sat`. QF_UF
prints definitions for user-declared constants, functions, and predicates.
QF_LRA prints user-declared nullary `Real` constants from the final tableau and
also composes in the QF_UF model body for non-Real preprocessing facts, so a
QF_UFLRA formula whose UF equality atoms are solved away before LRA dispatch
still defines the original uninterpreted constants needed to validate the input
assertions. The printed model is intended to be validated as an SMT-LIB response
file: stdout starts with `sat`, followed by a parenthesized `model`.

The mixed QF_UFLRA path additionally supports a narrow Real-argument model subset:
Primo composes the QF_UF model body with the nullary-Real QF_LRA definitions,
and UF symbols may take Real constants or purified non-constant Real terms as
arguments or return `Real` values. Before printing, the model path augments LRA
with EUF-implied Real-constant equalities and strict separations for Real
constants that are distinct in the EUF arrangement; the resulting concrete Real
values are used in UF function-table guards and Real-valued UF results. The
strict separations are chosen by bounded backtracking over candidate orientations
and fall back to the former greedy order when the candidate set is too large for
the reconstruction cap, currently 18 candidate pairs.

Use [Dolmen](https://ceur-ws.org/Vol-3429/short9.pdf) as the independent
model-format and assertion validator. Dolmen's model-validation interface uses a
solver response file, conventionally with an `.rsmt2` extension:

```bash
python3 scripts/validate_qfuf_models_with_dolmen.py \
  --solver ./build/primo \
  benchmarks/smoke/qf_uf/*.smt2 \
  --out results/qfuf-dolmen-smoke
```

The script writes the input problem, Primo response file (`.rsmt2`), stdout and
stderr logs, and `summary.csv`. It runs Dolmen with `--check-model=true -r` by
default. Pass `--check-model-mode=full` or `--check-model-mode=interleave` only
when the installed Dolmen binary supports that option; Dolmen's interleave mode
imposes additional ordering constraints between input declarations and model
definitions. Dolmen is an external dependency and is not vendored in this
repository.

Current limitations:

- only pure `QF_UF`, pure Boolean, pure nullary-Real `QF_LRA`, separable
  QF_UFLRA, Real-argument QF_UFLRA values, and Real-valued QF_UFLRA UF
  application results are printed;
- unsupported, unknown, and unsat results do not print a model;
- QF_UF preprocessing reconstruction is limited to Boolean/forced unit facts,
  solved-constant substitutions, and ground-unit substitutions;
- QF_LRA preprocessing reconstruction is limited to Real equalities recorded by
  solve-eqs/Gaussian elimination;
- no array theory or future preprocessing rewrite is model-reconstructed unless
  it emits explicit reconstruction assignments and the active model builder
  consumes them. The optional QF_UFLRA model-equality branch is reconstructed
  only as an ordinary true interface equality in the documented mixed model
  subset, not as a full Nelson-Oppen arrangement object.

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
