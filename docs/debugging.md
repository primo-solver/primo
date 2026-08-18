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
- `:time.preprocessing-us`: shared preprocessing and pre-solve preparation time
  in microseconds;
- `:time.solve-us`: SAT/EUF solving time in microseconds;
- `:time.total-us`: total `check_sat_smtlib` time in microseconds, excluding
  CLI file I/O and benchmark-harness process overhead;
- `:input.assertions`;
- `:frontend.ite.boolean`;
- `:frontend.ite.auxiliaries`;
- `:frontend.define-fun.nullary-direct-uses`: direct references, including
  references from later definitions;
- `:frontend.define-fun.nullary-eager-compiles`: declaration-time compiles;
- `:frontend.define-fun.nullary-side-assertions`: auxiliary assertions stored
  with compiled definitions;
- `:frontend.define-fun.nullary-cache-hits`,
  `:frontend.define-fun.nullary-cache-misses`, and
  `:frontend.define-fun.nullary-cache-entries`: compatibility aliases for old
  result files; hits means direct uses, while misses and entries mean eager
  compiles;
- `:preprocessing.assertions`;
- `:preprocessing.passes`;
- `:preprocessing.changed-passes`;
- `:preprocessing.adaptive-passes`;
- `:preprocessing.term-size.initial`;
- `:preprocessing.term-size.final`;
- `:preprocessing.arithmetic-canonicalization.atoms-visited`;
- `:preprocessing.arithmetic-canonicalization.atoms-rewritten`;
- `:preprocessing.arithmetic-canonicalization.comparisons`;
- `:preprocessing.arithmetic-canonicalization.equalities`;
- `:preprocessing.arithmetic-canonicalization.constant-folds`;
- `:preprocessing.arithmetic-canonicalization.orientation-reversals`;
- `:preprocessing.arithmetic-canonicalization.aliases-merged`;
- `:preprocessing.arithmetic-canonicalization.duplicate-assertions-removed`;
- `:preprocessing.arithmetic-canonicalization.terms-created`;
- `:preprocessing.arithmetic-canonicalization.catchup-rewrites`;
- `:preprocessing.arithmetic-canonicalization.post-purification-rewrites`;

These arithmetic-canonicalization counters remain zero unless
`--arithmetic-atom-canonicalization` is enabled; pure QF_UF remains zero even
with the option enabled.

- `:preprocessing.boolean-flatten.attempts`;
- `:preprocessing.boolean-flatten.accepted`;
- `:preprocessing.boolean-flatten.threshold-stops`;
- `:preprocessing.boolean-flatten.operands-copied`;
- `:preprocessing.boolean-flatten.rewrites`;
- `:preprocessing.boolean-flatten.max-arity`;
- `:preprocessing.boolean-implies.visited`;
- `:preprocessing.boolean-implies.clause-candidates`;
- `:preprocessing.boolean-implies.rewritten`;
- `:preprocessing.boolean-implies.all-mode-rewrites`;
- `:preprocessing.boolean-implies.widened-operands`;
- `:preprocessing.boolean-implies.threshold-stops`;
- `:preprocessing.boolean-implies.terms-created`;
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
- `:preprocessing.equality-query.nodes`;
- `:preprocessing.equality-query.memo-hits`;
- `:preprocessing.equality-query.add-term-requests`;
- `:preprocessing.equality-query.add-term-seen-hits`;
- `:preprocessing.solve-eqs.candidates`;
- `:preprocessing.solve-eqs.accepted`;
- `:preprocessing.solve-eqs.rewrites`;
- `:preprocessing.solve-eqs.rejected-cycles`;
- `:preprocessing.solve-eqs.rejected-blowups`;
- `:preprocessing.real-equality-lowering.lowered`;
- `:preprocessing.real-equality-lowering.guarded-distributed`;
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
- `:preprocessing.auto.selector.structural`;
- `:preprocessing.auto.selector.shadow-evaluated`;
- `:preprocessing.auto.selector.legacy-disagreement`;
- `:preprocessing.auto.structural.conjunct-occurrences`;
- `:preprocessing.auto.structural.unique-conjuncts`;
- `:preprocessing.auto.structural.clause-like-conjuncts`;
- `:preprocessing.auto.structural.implication-clause-candidates`;
- `:preprocessing.auto.analysis.finite-domain-domains`;
- `:preprocessing.auto.analysis.finite-domain-choices`;
- `:preprocessing.auto.analysis.predicted-amo-clauses`;
- `:preprocessing.auto.profile.large-skip`;
- `:preprocessing.auto.profile.finite-domain-basic`;
- `:preprocessing.auto.profile.compact-clause-basic`;
- `:preprocessing.auto.profile.strong`;
- `:preprocessing.auto.limit.*`: the six effective structural selector limits;
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
post-model theory conflict, including EUF conflicts discovered by the mixed
QF_UFLRA final-check loop. In online/conflict callback mode Primo also installs
a CaDiCaL terminator, so a long in-progress SAT call can stop soon after the
callback reaches the limit. This is intended for profiling and stats snapshots
on timeout-prone benchmarks; it is not a solving heuristic.
For mixed QF_UFLRA instances that finish SAT search but spend time in final
equality sharing or arrangement/model reconstruction,
`--nelson-oppen-arrangement-probe-limit N` instead caps temporary LRA
consistency probes and returns `unknown` at the cap. Use it when
`:no.interface.lra-extra-atom-probes` grows but `:theory.conflicts` does not.

When the CLI is run with `--stats`, it keeps the current counters in a live
statistics sink. On `SIGTERM` it prints `unknown` to stdout and flushes the
partial counters to stderr before exiting. The benchmark harnesses send
`SIGTERM` first and only use `SIGKILL` after a short grace period, so timeout
rows can often contain diagnostic counters. This timeout flush is best-effort,
not async-signal-safe: it calls statistics and process-exit code that may
allocate while the signal interrupted an allocation. An oversized QF_UFLRA row
at `4e19d1b` aborted in this handler under high memory pressure. Inspect
`exit_code` and `stderr_head` even when the harness classifies a row as a
timeout. Prefer `--max-theory-conflicts` for controlled profiling, but remember
that it cannot cap frontend work or a search that has not yet reported its first
theory conflict.

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
- `:no.purification.memoized-formula-rewrites`
- `:no.purification.memoized-arithmetic-rewrites`
- `:no.purification.memoized-euf-rewrites`
- `:no.purification.skipped-no-sites`
- `:no.interface.shared-real-terms`
- `:no.interface.sat-equality-atoms`
- `:no.interface.sat-equalities-true`
- `:no.interface.sat-equalities-false`
- `:no.interface.sat-equalities-forced`
- `:no.interface.equalities-propagated`
- `:no.interface.euf-to-lra-equalities`
- `:no.interface.lra-to-euf-equalities`
- `:no.interface.equality-candidates`
- `:no.interface.equality-existing-atoms`
- `:no.interface.equality-missing-atoms`
- `:no.interface.equality-created-atoms`
- `:no.interface.equality-already-true`
- `:no.interface.equality-assigned-false`
- `:no.interface.equality-clauses`
- `:no.interface.equality-reason-lits`
- `:no.interface.equality-local-reasons`
- `:no.interface.equality-local-reason-lits`
- `:no.interface.equality-fallback-reasons`
- `:no.interface.euf-explanation-missing`
- `:no.interface.convex-rounds`
- `:no.interface.exact-only`
- `:no.interface.euf-spanning-equalities`
- `:no.interface.lra-opportunistic-enabled`
- `:no.interface.lra-opportunistic-skipped-rounds`
- `:no.interface.lra-opportunistic-candidates`
- `:no.interface.lra-opportunistic-fixed-eq`
- `:no.interface.lra-opportunistic-eq-row`
- `:no.interface.lra-opportunistic-eq-offset-rows`
- `:no.interface.lra-opportunistic-eq-rows`
- `:no.interface.lra-opportunistic-reason-lits`
- `:no.interface.lra-opportunistic-unmapped-terms`
- `:no.interface.lra-opportunistic-unmapped-equalities`
- `:no.interface.lra-opportunistic-existing-atoms`
- `:no.interface.lra-opportunistic-missing-atoms`
- `:no.interface.lra-opportunistic-created-atoms`
- `:no.interface.lra-opportunistic-clauses`
- `:no.interface.lra-opportunistic-rechecks`
- `:no.interface.lra-legacy-producer-selected`
- `:no.interface.lra-legacy-scan-skipped`
- `:no.interface.lra-model-unit-selected`
- `:no.interface.lra-model-unit-classes`
- `:no.interface.lra-model-unit-class-terms`
- `:no.interface.lra-model-unit-candidate-pairs`
- `:no.interface.lra-model-unit-rows-inspected`
- `:no.interface.lra-model-unit-nonzeros-inspected`
- `:no.interface.lra-model-unit-fixed-matches`
- `:no.interface.lra-model-unit-one-row-matches`
- `:no.interface.lra-model-unit-two-row-matches`
- `:no.interface.lra-model-unit-nonunit-rejections`
- `:no.interface.lra-model-unit-nonfixed-row-rejections`
- `:no.interface.lra-model-unit-reason-lits`
- `:no.interface.lra-adaptive-selected`
- `:no.interface.lra-adaptive-model-unit-rounds`
- `:no.interface.lra-adaptive-legacy-rounds`
- `:no.interface.lra-adaptive-fallback-activations`
- `:time.no-lra-equality-producer-us`
- `:no.interface.lra-exact-fallback-pairs`
- `:no.interface.lra-exact-fallback-skipped-pairs`
- `:no.interface.lra-exact-fallback-equalities`
- `:no.interface.lra-model-filter-builds`
- `:no.interface.lra-model-filter-full-pairs`
- `:no.interface.lra-model-filter-candidate-pairs`
- `:no.interface.lra-model-filter-pruned-pairs`
- `:no.interface.lra-model-filter-unmapped-terms`
- `:no.interface.lra-model-filter-conservative-pairs`
- `:no.interface.materialized-pairs`
- `:no.interface.materialized-missing-pairs`
- `:no.interface.equality-rechecks`
- `:no.interface.euf-to-lra-candidates`
- `:no.interface.euf-to-lra-existing-atoms`
- `:no.interface.euf-to-lra-missing-atoms`
- `:no.interface.euf-to-lra-created-atoms`
- `:no.interface.euf-to-lra-clauses`
- `:no.interface.euf-to-lra-rechecks`
- `:no.interface.lra-to-euf-candidates`
- `:no.interface.lra-to-euf-existing-atoms`
- `:no.interface.lra-to-euf-missing-atoms`
- `:no.interface.lra-to-euf-created-atoms`
- `:no.interface.lra-to-euf-clauses`
- `:no.interface.lra-to-euf-rechecks`
- `:no.interface.lra-to-euf-pair-dedup-lookups`
- `:no.interface.lra-to-euf-pair-dedup-duplicates`
- `:no.interface.conflicts`
- `:no.interface.euf-conflicts`
- `:no.interface.lra-conflicts`
- `:no.interface.model-equality-candidates`
- `:no.interface.model-equality-created`
- `:no.interface.model-equality-pending`
- `:no.interface.model-equality-activated`
- `:no.interface.model-equality-duplicate-pending`
- `:no.interface.model-equality-pending-batches`
- `:no.interface.model-equality-pending-batch-lits`
- `:no.interface.model-equality-max-pending-batch`
- `:no.interface.model-equality-assumptions`
- `:no.interface.model-equality-assumption-solve-calls`
- `:no.interface.model-equality-failed-assumptions`
- `:no.interface.model-equality-blocking-clauses`
- `:no.interface.model-equality-dropped-assumptions`
- `:no.interface.model-equality-retained-assumptions`
- `:no.interface.model-equality-conflicts`
- `:no.interface.model-equality-full-pairs`
- `:no.interface.model-equality-filtered-pairs`
- `:no.interface.model-equality-care-pairs`
- `:no.interface.model-equality-care-pruned-pairs`
- `:no.interface.model-equality-scanned`
- `:no.interface.model-equality-lra-equal`
- `:no.interface.lra-model-mutation-enabled`
- `:no.interface.lra-model-mutation-calls`
- `:no.interface.lra-model-mutation-updates`
- `:no.interface.lra-model-mutation-value-classes-before`
- `:no.interface.lra-model-mutation-value-classes-after`
- `:no.interface.lra-model-mutation-candidate-pairs-before`
- `:no.interface.lra-model-mutation-candidate-pairs-after`
- `:no.interface.lra-model-mutation-pruned-pairs`
- `:no.interface.model-equality-mutation-checks`
- `:no.interface.model-equality-mutated`
- `:no.interface.model-equality-selected-care`
- `:no.interface.model-equality-selected-full`
- `:no.interface.model-equality-skipped-assigned`
- `:no.interface.model-equality-skipped-tried`
- `:no.interface.model-equality-queue-builds`
- `:no.interface.model-equality-queued`
- `:no.interface.model-equality-queue-pops`
- `:no.interface.model-equality-queue-skipped-tried`
- `:no.interface.model-equality-queue-skipped-assigned`
- `:no.interface.model-equality-queue-empty`
- `:no.interface.model-equality-adaptive-stops`
- `:no.interface.model-equality-active-assumptions`
- `:no.interface.model-equality-max-active-assumptions`
- `:no.interface.arrangement-checks`
- `:no.interface.arrangement-failures`
- `:no.interface.arrangement-separations`
- `:no.interface.arrangement-lra-extra-atom-probes`
- `:no.interface.arrangement-lra-extra-atoms-probed`
- `:no.interface.lra-extra-atom-probes`
- `:no.interface.lra-extra-atoms-probed`
- `:no.interface.lra-extra-atom-probe-limit-hit`
- `:no.interface.euf-care-checks`
- `:no.interface.euf-care-full-pairs`
- `:no.interface.euf-care-pairs`
- `:no.interface.euf-care-pruned-pairs`
- `:no.interface.euf-care-argument-definitions`
- `:no.interface.euf-care-function-classes`
- `:no.interface.euf-care-definition-pairs-full`
- `:no.interface.euf-care-definition-pairs-within-classes`
- `:no.interface.euf-care-argument-pairs-checked`
- `:no.interface.euf-care-definition-pairs-pruned`
- `:no.interface.euf-care-duplicate-argument-pairs-pruned`
- `:no.interface.euf-care-cache-hits`
- `:no.interface.euf-care-cache-misses`
- `:no.interface.euf-implied-class-projections`
- `:no.interface.euf-implied-class-projection-terms`
- `:no.interface.lra-prepared-reused`
- `:no.interface.lra-prepared-augmented`
- `:no.interface.lra-online-enabled`
- `:no.interface.lra-online-final-checks`
- `:no.online.enabled`
- `:no.online.shared-real-pairs`
- `:no.online.precreated-equality-atoms`
- `:no.online.paired-bound-precreate-candidates`
- `:no.online.assignments`
- `:no.online.backtracks`
- `:no.online.lra-bound-equality-candidates`
- `:no.online.lra-bound-equality-missing-atoms`
- `:no.online.lra-bound-equality-materialized`
- `:no.online.lazy-equality-atoms-created`
- `:no.online.lazy-equality-vars-observed`
- `:no.online.propagations`
- `:no.online.reason-lits`
- `:no.online.skipped-assigned`
- `:no.online.final-lra-equality-scan-skipped`
- `:no.online.final-lra-equality-scan-capped`
- `:no.online.final-lra-equality-scan-fallbacks`
- `:no.interface.lra-conflict-lits`
- `:no.interface.model-separations`
- `:no.interface.model-real-terms-before-interface-filter`
- `:no.interface.model-real-terms-filtered`
- `:no.interface.model-euf-real-classes`
- `:no.interface.model-euf-real-anchored-classes`
- `:no.interface.model-euf-real-fresh-classes`
- `:no.interface.model-euf-real-unmapped-terms`
- `:no.interface.model-euf-real-value-conflicts`
- `:no.interface.model-separation-candidates`
- `:no.interface.model-separation-euf-mergeable-pairs`
- `:no.interface.model-separation-euf-forbidden-pairs`
- `:no.interface.model-separation-search-capped`
- `:no.interface.model-separation-search-branches`
- `:no.interface.model-separation-search-backtracks`
- `:no.interface.model-separation-model-oriented`

These count internal proxy definitions in total, the split between Real-valued
UF applications in arithmetic atoms and non-constant Real terms in EUF atoms or
EUF-side application definitions, repeated uses of already-created application
or Real-term proxies, same-pass rewrite-cache hits over hash-consed formula,
arithmetic, and EUF terms, Real proxy terms visible to both EUF and LRA, and
equality checks in each interface direction. The older
`:no.interface.equalities-propagated`, `:no.interface.euf-to-lra-equalities`,
and `:no.interface.lra-to-euf-equalities` counters count implied equalities
reported by the producing theory, even when the equality is already true or
cannot yet be delivered as a useful SAT clause. The newer
`:no.interface.equality-*` counters split that total into SAT-visible existing
atoms, missing equality atoms, lazily created atoms, already-true equalities,
equalities assigned false in the current SAT assignment, guarded clauses added,
reason literals in those clauses, and solver-loop rechecks caused by those
clauses. The directional `:no.interface.euf-to-lra-*` and
`:no.interface.lra-to-euf-*` counters provide the same clause-delivery split per
producer/consumer direction. `:no.interface.sat-equality-atoms` counts
SAT-visible shared-Real equality atoms registered so far, while
`:no.interface.sat-equalities-true` and `:no.interface.sat-equalities-false`
describe the last checked SAT assignment. Strict shared-Real separations are
counted while validating a final `sat` arrangement or reconstructing mixed
models. A false generated interface equality is asserted to EUF as a
disequality; it is not asserted to LRA as a native false-`Eq` atom. Ordinary
SMT-LIB Real disequality such as `(not (= x y))` is supported by Real equality
lowering and Boolean-level case splitting before prepared LRA atoms are asserted.
The `euf-care-argument-definitions`, `euf-care-function-classes`, and
`euf-care-definition-pairs-*` counters are cumulative across care checks.
`definition-pairs-full` is the work of the former all-definition-pairs scan;
`definition-pairs-within-classes` is the cross-class-pruned intermediate count,
and `argument-pairs-checked` is the work retained after equal arguments within
each class are deduplicated. The two `*-pruned` counters report those reductions.
The work counters advance only on `euf-care-cache-misses`. A cache hit reuses
the exact care diagnostics only when the complete ordered EUF assignment,
including SAT-visible interface equalities, is unchanged.
`:no.interface.convex-rounds` counts SAT-consistent mixed rounds that reach
bidirectional equality sharing. EUF reports one spanning set per shared
equality class, counted by `:no.interface.euf-spanning-equalities`; it does not
materialize every pair in a class. The corresponding
`euf-implied-class-projection-*` counters report the expected-linear closure
projection used to find those classes. `:no.interface.exact-only` is one for
`--nelson-oppen-prop=exact` and its legacy `off` alias. In that mode mandatory
EUF spanning and exact LRA fallback sharing remain active, while
`:no.interface.lra-opportunistic-enabled` is zero and
`:no.interface.lra-opportunistic-skipped-rounds` counts rounds that omit the
optional opportunistic poll. LRA otherwise first polls the checked tableau's
`FixedEq`, `EqRow`, `EqOffsetRows`, and `EqRows` rules. The
`:no.interface.lra-opportunistic-*` counters
split rule hits, reason literals, unmapped shared terms, lazy SAT atom delivery,
clauses, and rechecks. After these cheap producers are quiescent, the feasible
tableau's `model_equal_classes()` partition removes cross-value pairs, which
cannot be LRA-entailed. The `:no.interface.lra-model-filter-*` counters report
partition builds, the unfiltered cross product, retained and pruned pairs, and
conservative fallbacks for unmapped terms. Equal-value pairs are only candidates
and still require exact proof. `:no.interface.lra-exact-fallback-pairs` counts
unresolved retained pairs sent to strict-separation probes,
`:no.interface.lra-exact-fallback-skipped-pairs` counts retained pairs already
resolved by SAT/EUF, and
`:no.interface.lra-exact-fallback-equalities` counts exact entailments found.
These counters are cumulative across fixpoint rounds. A FixedEq/EqRow test with
zero fallback pairs is the direct evidence that the cheap producer removed
probe work; a zero fallback count on arbitrary inputs is not expected.
For LRA-to-EUF equality sharing, the default `final` path classifies each
LRA-implied shared Real equality through the current EUF closure before creating
or forcing an equality atom. The filter records
`:no.interface.lra-to-euf-equalities-euf-already-true` when EUF already has the
pair equal, `:no.interface.lra-to-euf-equalities-euf-mergeable-skipped` when EUF
can absorb the LRA equality without conflict and the equality is skipped,
`:no.interface.lra-to-euf-equalities-euf-mergeable-forced` when such a
mergeable equality is conservatively sent through the guarded clause path, and
`:no.interface.lra-to-euf-equalities-euf-forbidden-forced` when EUF rejects the
equality immediately. The 2026-08-05 full QF_UFLRA run rejected the
mergeable-skip optimization after wrong answers; the conservative path should
therefore keep `mergeable-skipped` and
`:no.interface.lra-to-euf-internal-model-equalities` at zero for LRA-to-EUF
sharing. Broad fallback after an internal skip is counted by
`:no.interface.lra-to-euf-skip-model-reconstruction-fallbacks` and should also
remain zero on the conservative path.

With the default QF_UFLRA model reconciliation, the broad exact scan is no
longer the normal final step. The shared-model counters diagnose the
replacement (`--no-qf-uflra-model-reconciliation` selects the old fallback):

- `:no.shared-model.forbidden-equalities`, `forbidden-before`, and
  `forbidden-after` report SAT-false pairs supplied to scratch LRA mutation;
- `:no.shared-model.unresolved-forbidden` reports violations remaining in the
  final candidate partition;
- `:no.shared-model.targeted-separation-*` reports strict-orientation searches
  restricted to unresolved mandatory disequalities;
- `:no.shared-model.compatibility-edges`, `compatibility-merges`, and
  `compatibility-projections` report EUF absorption of LRA class spanning
  edges;
- `:no.shared-model.compatibility-splits` counts direct or congruence-induced
  rejection of an optional merge, while
  `compatibility-consequence-disagreements` isolates the latter;
- `:no.shared-model.split-frontier-batches`, `split-frontier-pairs`,
  `split-frontier-duplicates`, and `split-frontier-max` report how many rejected
  model edges are canonicalized and sent through one SAT re-entry rather than
  stopping at the first edge;
- `:no.shared-model.split-materializations` counts lazy positive-first equality
  assumptions; failed assumptions continue to use the
  `:no.interface.model-equality-*` blocking-clause counters;
- `:no.shared-model.compatibility-successes` and
  `:no.online.final-lra-equality-scan-skipped` count accepted candidate rounds.

The `:no.online.*` spelling of the final-scan counter is historical. It also
counts reconciliation rounds when `--nelson-oppen-prop` is `final`. Nonzero
`:no.shared-model.exact-fallbacks` means an unexpected mapping failure, failed
mandatory separation, or another candidate invariant forced the old complete
path. The shared interface is the intersection of EUF Real constants with
variables that survive LRA polynomial normalization; syntactically present but
algebraically cancelled constants are EUF-only model terms. Consequently
`lra-model-filter-unmapped-terms` should remain zero. A model-equality
coincidence alone is never an entailed propagation.

Augmented LRA component-state reuse is reported by:

- `:no.interface.lra-model-epoch-prepared-hits` and `prepared-misses`;
- `:no.interface.lra-model-epoch-check-hits` and `check-misses`;
- `:no.interface.lra-model-epoch-tableau-reuses` for same-interface checks that
  pop prior bounds and reuse the feasible basis;
- `:no.interface.lra-model-epoch-invalidations` when the exact true-interface
  equality key changes.

This epoch exists only in mixed QF_UFLRA when true generated interface
equalities augment LRA. A prepared hit with a check miss is expected: it means
the same rows are being checked under a different current SAT assignment.

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
the search. The opt-in `care-queued` policy rebuilds a bounded queue from the
current EUF care pairs only, so a shared Real pair that is merely present in the
full cross product is not materialized as a model-equality branch candidate.
The opt-in `--qf-uflra-candidate-final-scan` path also consumes the EUF care
diagnostic, but for entailed LRA-to-EUF equality propagation rather than model
guesses. It probes only a candidate set made from current EUF care pairs, shared
Real constants co-occurring inside one EUF atom, and shared Real pairs whose
two non-strict LRA bounds are both true in the current SAT assignment. The
counters `:no.interface.candidate-final-scan-enabled`,
`:no.interface.candidate-final-scan-pairs`,
`:no.interface.candidate-final-scan-skipped-pairs`, and
`:no.interface.candidate-final-scan-equalities` report that first pass. If
final arrangement validation or model reconstruction cannot be completed using
those equalities, `:no.interface.candidate-final-scan-fallbacks` increments and
Primo runs the broad all-pairs LRA equality scan once. Equalities found by that
fallback are forced through SAT before the solver accepts the final
arrangement; `:no.interface.candidate-final-scan-fallback-equalities` records
how many the fallback found.
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
interface equality atoms.
`:no.interface.lra-fixed-elimination-enabled` is one when fixed-variable
elimination is active in the mixed tableau. It is on by default in QF_UFLRA,
can be disabled with `--no-qf-uflra-fixed-elimination`, and does not describe
the separate pure-QF_LRA default. `:no.interface.lra-online-enabled` records whether the
default mixed coordinator attached the pure-QF_LRA trail-synced online
propagator for original arithmetic atoms, and
`:no.interface.lra-online-final-checks` counts final SAT assignments whose base
LRA consistency result came from that callback-owned state. This online path is
disabled for the experimental `--model-equality-branching` mode; when generated
interface equalities are true, the coordinator still rebuilds the augmented LRA
theory and performs an ordinary final check.

EUF preparation follows the same reuse boundary. `:no.interface.euf-prepared-reused`
counts rounds whose existing `PreparedFlatTheory` was sufficient, while
`:no.interface.euf-prepared-augmented` counts rounds that appended newly
materialized interface equality atoms or their flat definitions. The
`:no.interface.euf-prepared-atoms` and `:no.interface.euf-prepared-definitions`
counters report the cumulative prepared sizes. This is structural reuse only:
each SAT round still copies the definition-only congruence closure and asserts
its own assignments, so explanations and backtracking remain round-local.
The preparation is append-only during one mixed solve because lazy interface
equality atoms and Boolean EUF proxies can appear after the initial
preparation. Each cached atom is keyed by its signed SAT literal and `TermId`.
Before a closure check, current assignments are realigned through that key;
their input order is not stable because generated equalities are gathered as
true atoms followed by false atoms in each SAT round. A large
`:sat.solve-calls` count on a small model-equality example can indicate a broken
alignment: a conflict clause may then describe a different cached equality and
already be satisfied by the assignment that produced it.

With `--qf-uflra-online-euf`, final reconciliation first attempts to borrow the
trail-synchronized closure instead of copying `PreparedFlatTheory::base_closure`.
`:no.interface.euf-live-model-attempts` splits into
`:no.interface.euf-live-model-reuses` and
`:no.interface.euf-live-model-fallbacks`; the latter is further diagnosed by
`:no.interface.euf-live-model-incomplete` and
`:no.interface.euf-live-model-registration-invalid`.
`:no.interface.euf-final-closure-rebuilds` is the direct count of broad final
closure copies. Repeated CaDiCaL solves may omit already known observed values
from ordinary trail notifications.
`:no.interface.euf-live-model-callback-assignments` counts missing values
synchronized from the complete model vector passed to `cb_check_found_model`.
`:no.interface.euf-live-deferred-callbacks` counts assignment, level, or
backtrack callbacks queued while a protected model scope is active and replayed
before the next solve. `:no.interface.euf-live-registry-syncs` counts the
pre-solve generated-equality registry synchronization passes. A
protected-checkpoint exception indicates that coordinator or callback code
tried to roll back SAT-owned EUF state through a live model scope.

`--nelson-oppen-prop=online` adds a mixed-theory dispatcher around the LRA
callback. `:no.online.enabled` records that the dispatcher, not the direct LRA
callback, was connected. `:no.online.precreated-equality-atoms` counts the
shared Real equality literals created before SAT search; in the lazy online path
it should remain zero. `:no.online.paired-bound-precreate-candidates` is kept as
a compatibility diagnostic and should also remain zero. The full shared Real
cross product is reported by `:no.online.shared-real-pairs` for diagnostics
only, not as an observed SAT variable count. `:no.online.assignments` and
`:no.online.backtracks` count the dispatcher trail traffic.
`:no.online.lra-published-equalities` counts equality candidates produced by
`OnlineLraPropagator`; today those are exactly the implemented paired-bound
rule where both `(<= a b)` and `(<= b a)` are true for a shared Real pair.
Broader LRA-derived equalities are intentionally not part of this **online
callback** counter. `FixedEq` and `EqRow` are polled after the final feasible
LRA check and reported through `:no.interface.lra-opportunistic-*`; they do not
run on each callback bound update.
`:no.online.lra-bound-equality-candidates` counts published candidates whose
pair is known to the mixed-theory interface registry.
`:no.online.lra-bound-equality-missing-atoms` counts published candidates outside
that registry; this can still be nonzero because the pure LRA producer may
publish equalities for pairs that are not mixed-theory shared Real interface
pairs. `:no.online.lra-bound-equality-materialized` counts candidates that
obtained a deliverable equality literal, either by reusing an existing atom or
by lazy creation. `:no.online.lazy-equality-atoms-created` counts equality atoms
created by the dispatcher from `cb_propagate()`, and
`:no.online.lazy-equality-vars-observed` counts the corresponding fresh observed
SAT variables.
`:no.online.final-lra-equality-scan-skipped`,
`:no.online.final-lra-equality-scan-fallbacks`, and
`:no.online.final-lra-equality-scan-capped` remain compatibility counters and
must be zero. Online mode retains the complete exact final fallback over
unresolved pairs because a successful separation check is not sufficient to
prove that omitted LRA-implied equalities are irrelevant to EUF congruence.
`:no.online.propagations` counts returned equality literals, and
`:no.online.reason-lits` counts the two bound literals used as the SAT reason.
`:no.online.skipped-assigned` counts candidates already assigned by SAT before
delivery.

`--qf-uflra-online-euf` additionally attaches the existing incremental EUF
propagator as a child of the mixed dispatcher. It works with either final or
online Nelson-Oppen propagation and is default-off. Use these
counters to distinguish useful mixed EUF work from callback overhead:

- `:no.interface.euf-online-enabled` is one only when the mixed EUF child is
  attached;
- `:no.online.euf-propagations` counts literals returned by that child;
- `:no.online.euf-external-clauses` counts EUF external-clause callbacks
  selected by the dispatcher;
- `:no.online.euf-generated-equalities-registered` counts lazily created
  interface equality atoms appended to the EUF child;
- `:no.online.euf-generated-equality-registration-failures` counts atoms whose
  endpoints were not already representable in its flat term state;
- `:no.online.euf-generated-equality-observation-failures` counts dispatcher
  attempts that could not be handed to the EUF child.

A high `:euf.propagation.rounds` value with zero
`:no.online.euf-propagations` and zero
`:no.online.euf-external-clauses` means the option is adding callback work
without helping the mixed search. Consult the live-model and final-rebuild
counters above to determine whether final validation reused callback state.
Pure QF_UF and pure QF_LRA runs should leave
`:no.online.enabled` at zero; that is the guard against single-theory overhead.
`:no.interface.arrangement-checks` counts non-model `sat` results where the
coordinator validated that the EUF arrangement over shared Real interface
arguments can be separated consistently by LRA, with failures and accepted
strict separations split into the corresponding arrangement counters.
`:no.interface.model-real-terms-before-interface-filter` reports how many Real
constants and Real-valued applications were present in purified EUF atoms
before the final arrangement/model path was restricted to the Nelson-Oppen
interface, and
`:no.interface.model-real-terms-filtered` reports how many of those EUF-side
Real terms were excluded because they are not common EUF/LRA interface
constants. Exclusion applies to arrangement search and LRA probes, not to
printed model completion. For `get-model`, the shared class keeps its concrete
LRA value and every unanchored EUF-only Real equivalence class receives a
distinct fresh rational. Completion writes that class value for every Real
constant and Real-valued application in the class. Exact application values
take precedence over stale pre-rewrite application rows with the same concrete
arguments when the UF table is printed. `:no.interface.model-euf-real-classes`,
`:no.interface.model-euf-real-anchored-classes`, and
`:no.interface.model-euf-real-fresh-classes` report that reconstruction;
`model-euf-real-unmapped-terms` or `model-euf-real-value-conflicts` makes model
construction return `unknown` rather than print an invalid model. The final
arrangement path then probes the current EUF closure with a
temporary equality for each remaining shared-Real pair. A pair counted by
`:no.interface.model-separation-euf-mergeable-pairs` is in different EUF
classes but can still be merged without an EUF conflict, so it does not require
a strict LRA order. A pair counted by
`:no.interface.model-separation-euf-forbidden-pairs` would make the EUF closure
inconsistent if merged; only those pairs become
`:no.interface.model-separation-candidates` for strict LRA separation.
`:no.interface.lra-extra-atom-probes` counts LRA consistency checks with one or
more temporary strict separation/equality atoms.
`:no.interface.lra-extra-atoms-probed` sums the number of temporary atoms across
those checks. Exact single-pair equality scans normally use a check-local copy of
the already feasible tableau and probe both strict orientations with
`push()`/`pop()`. The corresponding work is split into
`:no.interface.lra-probe-reuse-pairs`,
`:no.interface.lra-probe-reuse-orientations`, and
`:no.interface.lra-probe-reuse-assignment-bounds`.
`:no.interface.lra-probe-reuse-checked-tableau-copies` counts contexts that
avoided assignment replay. The checked basis is rejected when its endpoint
difference expands above eight terms or its prior pivot-term work exceeds eight
operations per tableau variable. The total and the two reasons are reported by
`:no.interface.lra-probe-reuse-checked-tableau-fallbacks`,
`:no.interface.lra-probe-reuse-checked-tableau-difference-fallbacks`, and
`:no.interface.lra-probe-reuse-checked-tableau-basis-cost-fallbacks`; the
observed maxima are published by the matching `max-difference-terms` and
`max-pivot-term-ops` counters. A nonzero
`:no.interface.lra-probe-reuse-mapping-fallbacks` means a shared endpoint was
absent from the prepared term map and the old augmented-theory reconstruction
oracle was used. A nonzero
`:no.interface.lra-probe-reuse-context-conflicts` means replaying an assignment
already accepted by the main LRA check conflicted in the local context. This is
an internal-consistency violation: the solver records it and fails loudly rather
than treating a base conflict as evidence for the candidate equality. These live
counters expose arrangement exploration cost even if the final search does not
finish before a profiling timeout.
`:no.interface.arrangement-lra-extra-atom-probes` and
`:no.interface.arrangement-lra-extra-atoms-probed` are the subset spent while
choosing strict separations for final mixed QF_UFLRA arrangement validation or
model reconstruction. `--nelson-oppen-arrangement-probe-limit N` caps the broader
temporary-probe counter, including conservative LRA-to-EUF equality scans that
feed the final mixed check, and returns `unknown` when the next temporary LRA
consistency check would exceed the budget.
`:no.interface.lra-extra-atom-probe-limit-hit` marks that soft stop. The cap is
opt-in and does not limit SAT solving or ordinary LRA theory conflicts.
When `--model-equality-branching` is enabled, the model-equality counters record
full shared Real pairs, how many remain after the one scratch model-mutation
sweep, how many are scanned by the branch selector, optional SAT equality atoms
created for those pairs, guesses queued for the next SAT call, guesses activated onto the IPASIR
assumption trail, duplicate queued guesses that were already active, active
assumption attempts over those atoms, SAT-level failed-assumption blocking
clauses, dropped failed assumptions, the active assumptions retained after the
last SAT rejection, assigned or already-tried equality literals skipped, whether
the selected guess came from the care-ranked prefix or the full-pair fallback,
care-queued queue builds, queued candidates, queue pops, queued candidates
skipped because their existing equality atom was already tried or assigned,
adaptive stops, the current and maximum active model-equality assumption-trail
size, and ordinary theory conflicts reached while at least one guessed equality
was active. A surviving model-equality guess is re-applied as an IPASIR
assumption on later SAT calls while the coordinator tries additional model-equal
pairs; it is still not a permanent unit clause.
`:no.interface.lra-model-mutation-*` reports the one Section 5.2 scratch sweep
performed per feasible mixed round: whether the path is enabled, calls, accepted
updates, distinct value classes before/after, candidate pairs before/after, and
pairs pruned. `--model-equality-mutation-scope=all|care` selects all shared Real
variables (the default) or only current EUF care-pair endpoints. The scope
counters report the selected mode, full and scoped variable visits, empty care
scopes, attempted versus accepted updates, and care-scoped results rejected
because they would increase the complete shared candidate-pair count. Even in
care mode the final partition and printed model use all shared terms. The
pre-mutation pair count is computed from class sizes; Primo does not materialize
that pair set. `:no.interface.model-equality-filtered-pairs`
is the post-mutation candidate count presented to the optional branch selector.
The legacy `model-equality-mutation-checks` and `model-equality-mutated` counters
remain aggregate branch diagnostics for pairs considered and removed by the
sweep. A pair that remains equal after mutation is only a branch candidate, not
an entailed equality. Guarded equality clauses and
`:no.interface.sat-equalities-forced` continue to require an independent LRA or
EUF explanation.
Compare it with `:no.interface.equality-candidates`: a large gap means the
producer is rediscovering equalities that are already true, missing from the
current interface registry, or not useful enough to force a SAT recheck.

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

The accepted symbolic shared model is not printed with an arbitrary concrete
infinitesimal. The coordinator sorts distinct shared `DeltaRational` values and
chooses delta below every adjacent crossing bound, following Dutertre and de
Moura Section 3.3. Inspect `:no.shared-model.concrete-delta-order-pairs`,
`:no.shared-model.concrete-delta-order-bounds`, and
`:no.shared-model.concrete-delta-order-tightenings` when Dolmen reports that two
Real UF arguments collapsed in the printed model.

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
stderr logs, `metadata.json`, and `summary.csv`. It runs Dolmen with
`--check-model=true -r` by
default. Pass `--check-model-mode=full` or `--check-model-mode=interleave` only
when the installed Dolmen binary supports that option; Dolmen's interleave mode
imposes additional ordering constraints between input declarations and model
definitions. Dolmen is an external dependency and is not vendored in this
repository. `make contrib-dolmen` builds pinned Dolmen 0.10 through a
repository-local opam switch. Building from source is intentional: the official
0.10 Linux executable requires glibc 2.35, while the SLURM environment provides
2.34.

The generated QF_UFLRA gate uses Dolmen's default strict parser. The real-corpus
batch script passes `--strict=false` through `--dolmen-arg` because the MathSAT
RandomCoupled files contain linear multiplication over compound linear terms;
Dolmen 0.10 accepts and model-checks those terms in lenient mode but promotes
its narrower strict-linear-syntax warning to a fatal report in strict mode.
This changes Dolmen's input acceptance, not model checking. Override
`PRIMO_MODEL_DOLMEN_ARGS` to change or clear that real-corpus setting.

Each completed validation row is appended, flushed, and synced immediately.
`metadata.json` starts with `status: running` and changes to `complete` only
after every selected input has a row. The `validator` column distinguishes a
direct `dolmen` pass from `z3-fallback` and `z3-only`; `dolmen_exit` is retained
when a fallback is attempted instead of being overwritten by the fallback's
exit status. A clean count with incomplete metadata is not a completed gate.
Use `--root`, `--shards`, and `--shard-index` for deterministic manifest
sharding.

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
For QF_LRA and QF_UFLRA mutation campaigns, use the patched YinYang fork until
the public package release includes the linear-Real mutation fix: older
typefuzz builds can generate non-linear Real properties, so their arithmetic
coverage is not acceptable fuzz-gate evidence.
Provision the fork on the SLURM login node with `make contrib-yinyang` before
submitting mutation jobs. The checked-in SLURM gates put `.venv-yinyang/bin` on
`PATH` and fail if `typefuzz` or `opfuzz` is missing; they do not install Python
packages from compute nodes. Array tasks must also use task-local YinYang log
directories through `scripts/yinyang_fuzz.sh --logs ...`, because sharing the
default `./logs` directory makes concurrent mutation shards a harness failure
rather than fuzz evidence.

For the mixed generated gate, `scripts/slurm_qf_uflra_fuzz_gate.sbatch`
accepts space-free solver option tokens through `PRIMO_FUZZ_SOLVER_ARGS`; for
example, the experimental mixed callback profile uses
`--nelson-oppen-prop=online --qf-uflra-online-euf`. The mutation gate's task 2
uses that same profile. Submit only array task 2 when the default and
candidate-final mutation profiles are not part of the current experiment.
The generated gate also reruns every general and finite-domain QF_UFLRA input
with `:produce-models true` and `get-model`, then validates every SAT response
with Dolmen. Dolmen is required when the default
`PRIMO_FUZZ_MODEL_VALIDATION=1` is active; a missing executable fails before
generation. `PRIMO_FUZZ_SOLVER_ARGS` is forwarded to model production as
separate `--solver-arg` values, so an option cannot accidentally be tested only
for verdicts. Model artifacts live under each task's `model-validation/`
subtree and `scripts/summarize_fuzz_results.py` reports them as a separate
result class. Disable this phase only for an explicitly verdict-only diagnostic
run; such a run is not a complete QF_UFLRA merge gate.
Every standard mixed gate also runs dedicated finite-domain shards: 500 QF_UF
cases under strong preprocessing and 500 default-profile QF_UFLRA cases. These
cases have construction-known answers, compare with Z3 and cvc5, and fail when
the expected finite-domain, Real-equality-lowering, or mixed-dispatch counters
do not fire. Selector experiments that intentionally choose a profile without
finite-domain AMO clauses must set
`PRIMO_FINITE_DOMAIN_QF_UFLRA_REQUIRE_POST_AMO=0` and
`PRIMO_FINITE_DOMAIN_QF_UFLRA_ACTIVATION_PROFILE=structural`. The structural activation
profile requires the structural selector, detected finite-domain structure, a
selected structural profile, the expected solver route, and Real-equality
lowering for QF_UFLRA. The default `amo` profile still requires raw and emitted
AMO clauses, and the QF_UFLRA post-AMO default remains `1`. The QF_UF shard
continues to require AMO and post-AMO activation. Answer and reference
comparisons are mandatory in both QF_UFLRA profiles.
`scripts/slurm_finite_domain_fuzz_gate.sbatch` provides the focused
default/strong/raw-only profile matrix. See [](finite-domain-fuzzing.md).
For models over real benchmark structure rather than generated formulas,
`scripts/slurm_qf_uflra_model_validation.sbatch` shards the deterministic
five-per-family QF_UFLRA manifest by default. Relative manifest entries are
resolved below `PRIMO_MODEL_ROOT`; every shard uses the same durable validator
format as the generated gate.
Before mutation, `scripts/slurm_qf_uflra_mutation_fuzz_gate.sbatch` runs
`scripts/check_solver_seed_corpus.py` on every compact QF_UFLRA mutation seed
with the exact task options and Z3 reference. Any disagreement, timeout,
unsupported result, crash, or mismatch with a declared status fails the task.
The seed timeout defaults to 60 seconds and can be changed with
`PRIMO_FUZZ_SEED_TIMEOUT`; the shorter mutation timeout remains independent.
Mutation counts do not replace this deterministic regression check. The gate
also rejects seeds larger than 4096 bytes by default; use
`PRIMO_FUZZ_SEED_SIZE_LIMIT` only when a deliberately larger mutation seed has
been shown to retain useful throughput.

The mutation phase defaults to 2000 mutations per compact seed and records a
reproducible typefuzz seed derived from `PRIMO_FUZZ_MUTATION_SEED_BASE` plus the
SLURM task id. `PRIMO_FUZZ_YINYANG_ITERATIONS` may reduce the count for a smoke
run, but such a run is not merge-grade evidence when typefuzz filters most
generated mutants before obtaining comparable solver answers.

Large source benchmarks are not mutation seeds. In particular, the two full
MathSAT witnesses under `benchmarks/performance/qf_uflra/` exposed the unsound
online final-scan shortcut, but they are expensive search benchmarks and make
poor YinYang inputs. The compact
`benchmarks/seeds/qf_uflra/online_nonpaired_congruence_forced_unsat.smt2`
witness and focused unit tests preserve that correctness obligation. Keep the
full witnesses in the deferred performance manifest and evaluate them
separately after material changes to convex QF_UFLRA combination; their current
runtime must not prevent the mutation campaign from starting.
`scripts/qf_uflra_smoke_fuzz.py` flushes each case result immediately so SLURM
stdout is a durable progress record even before the final `summary.csv` is
written. The dedicated finite-domain fuzzer is stricter: it appends and fsyncs
each CSV row and writes a running metadata record before invoking any solver.

### The remote sanitizer build

Building an ASAN/UBSan binary on the cluster needs the EasyBuild GCCcore
toolchain, not the system compiler, and it must be built on the **head node**.
`scripts/build_slurm_fuzz.sh` does both.

The reason is worth knowing because the earlier diagnosis in
`docs/run-log.json` had it backwards ("head node lacks libasan, compute nodes
lack C++ headers", concluding the build belonged in a batch job). Measured
2026-08-03:

- Compute nodes have no C++ standard headers at all -- `#include <vector>`
  fails there with the system compiler, sanitizers or not. They cannot compile
  anything, which makes a batch job the wrong vehicle.
- The head node's system gcc has a broken libasan: its linker script reads
  `INPUT ( /usr/lib64/libasan.so.6.0.0 )` and that file does not exist. The
  path is absolute inside the script, so `-L` cannot redirect it.

`module load GCCcore/11.3.0` fixes both -- own headers, own libasan, linker
script pointing inside its own tree. Compute nodes still need the runtime when
*executing*, so anything running the binary must put
`/export/eb/software/GCCcore/11.3.0/lib64` on `LD_LIBRARY_PATH`; the fuzz gate
does this itself.

Verified end to end: configure, build, and a compute-node run answering both
sat and unsat with zero sanitizer reports.

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

## Bisecting on the SLURM cluster

Four environment facts, each of which silently produced a *wrong* result
rather than an error, found the hard way on 2026-08-12 while bisecting one
QF_LRA instance. All four make a bisect converge on nothing or on the wrong
commit, so validate both endpoints through the real harness before starting.

| fact | how it fails |
|---|---|
| `module` is a shell function and is **not available in a batch shell** | `module load cmake` silently does nothing, `cmake` is not found, every test returns 125 and the bisect skips everything |
| `/opt/ohpc` is **head-node only** | an absolute path to the module's cmake also fails on compute nodes |
| compute nodes have `g++` but **not the C++ standard headers** | configure succeeds, then the build dies on `fatal error: string: No such file or directory`. Compute nodes can *run* binaries but cannot *compile*, which is why benchmark jobs are unaffected and only bisects hit this |
| `bc` is **not installed** on compute nodes | timing arithmetic silently yields `0.0s` for every arm, so a probe looks complete and says nothing |

The shape that works: **build on the head node, push the solve to a compute
node with `srun`.** A cmake that works in both places is the pip-installed one
in another workspace's `.venv-build/bin/cmake` (shared home), which is why
`scripts/slurm_prepare_benchmark_workspace.py` defaults to exactly that.

Two more from the same day, on the benchmark side:

- **Do not symlink the corpus into a workspace.** `bench_qf_uf_full.py` calls
  `path.relative_to(repo)`, and a symlink resolves outside the repo, so every
  array task dies. `cp -al` gives a hard-linked tree at no storage cost when
  both live on the same filesystem.
- **Count shard CSVs before believing a result.** A shard killed by the wall
  clock leaves no trace; 40/40 per arm is a check, not a formality.
