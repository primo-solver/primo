# Preprocessing

Primo currently exposes QF_UF preprocessing through
`--qf-uf-preprocess=off|basic|strong|auto`. The CLI and
`CheckSatOptions` default is `auto`.

- `off` disables the preprocessing loop;
- `basic` runs one pass of unit equality propagation;
- `strong` enables repeated preprocessing, equality bridging, finite-domain
  at-most-one clauses, opt-in finite-domain equality definitions, and
  finite-domain symmetry breaking;
- `auto` keeps equality bridging, finite-domain at-most-one clauses, and
  symmetry breaking when their recognizers apply. Guarded finite-domain equality
  definitions are available with `--finite-domain-equality-definitions`. The
  explicit `strong` preset removes the automatic equality-definition guards for
  experiments. A guarded single-assertion exception uses the lighter `basic`
  subset when the original formula has at most 15,000 reachable terms, at least
  200 finite-domain choices, and a small accepted finite-domain symmetry group;
  `--no-auto-basic-single` disables this exception for same-binary A/B runs. If
  that branch also contains Boolean-valued UF predicate applications, Primo uses
  polarity phase for those syntactically single-polarity atoms; disable that
  narrower phase override with `--no-auto-basic-single-predicate-polarity`. A
  guarded small multi-assertion auto branch uses the `basic` subset when the
  input has more than one assertion, at most 10,000 assertions, and at most
  20,000 initially reachable terms.

The default command now enables the safe part of the predecessor performance
profile:

```bash
./build/primo --uf-symmetry-breaking --preprocessing-passes 4 --eq-bridge \
  --prop-interval 32 --prop-assign-threshold 0.25 \
  --prop-delivery-budget 1000 input.smt2
```

The preprocessing flags in that command are defaults, so plain
`./build/primo input.smt2` uses the same preprocessing unless an option
overrides it. Plain Primo currently uses `--euf-prop=auto`: online callbacks are
used for small EUF abstractions, while large EUF abstractions use conflict-only
CaDiCaL callbacks to avoid repeated lazy post-model checks without enabling the
currently expensive online propagation scan.

The exact legacy-style reproduction profile is:

```bash
./build/primo --qf-uf-profile=tuned-interesting input.smt2
```

It sets the same safe switches as the default and leaves the selected
`--qf-uf-preprocess` mode at `auto` unless the user overrides it. It also
keeps the legacy propagation tuning knobs available for experiments; select
`--euf-prop=online` explicitly when reproducing online-propagation runs.

The individual legacy switches remain available for experiments and for
reproducing narrower configurations:

- equality bridging under disjunctions, enabled with `--eq-bridge` or
  `--qf-uf-preprocess=strong`;
- unit equality propagation, enabled by the `basic`, `strong`, and `auto`
  preprocessing loops;
- experimental ground substitution from positive unit equalities, enabled with
  `--ground-unit-substitution`;
- guarded experimental ground substitution, enabled with
  `--guarded-ground-unit-substitution`;
- finite-domain at-most-one clauses, enabled by `--qf-uf-preprocess=strong` and
  by most of the default `auto` path. The guarded single-assertion exception
  reports `:preprocessing.auto.basic-single` and skips these clauses for a small
  finite-domain/symmetry shape. A same-run July 19, 2026 SLURM comparison on
  the 334-row user HTML set validated this selector: default solved 332 with
  PAR2 2828.629, while `--no-auto-basic-single` solved 331 with PAR2 3031.436.
  The guarded small multi-assertion branch intentionally skips these clauses
  because same-run July 19, 2026 SLURM comparisons showed that several compact
  operation-table rows benefited from the lighter `basic` encoding plus polarity
  phase;
- finite-domain equality definitions. In `auto` they are opt-in with
  `--finite-domain-equality-definitions` and bounded by
  `--finite-domain-eqdefs-auto-domain-limit` and
  `--finite-domain-eqdefs-auto-clause-limit`; large single-assertion formulas
  are additionally guarded by `--finite-domain-eqdefs-auto-term-limit`.
  `--qf-uf-preprocess=strong` keeps the historical unbounded expansion. Use
  `--no-finite-domain-equality-definitions` for ablations;
- solved-constant substitution for acyclic unit definitions, enabled by default
  and disabled with `--no-solved-constant-substitution`;
- bounded ground-congruence clauses over applications already present in the
  formula, enabled by `--ground-congruence`;
- symmetry breaking over finite-domain choice disjunctions, enabled by default
  when preprocessing is enabled and explicitly controllable with
  `--uf-symmetry-breaking` / `--no-uf-symmetry-breaking`. Fixed-term value
  precedence inside accepted symmetry groups is controlled separately by
  `--finite-domain-value-precedence` /
  `--no-finite-domain-value-precedence`.

`--no-eq-bridge`, `--no-uf-symmetry-breaking`, and
`--theory-phase=auto` are useful ablation or targeted-experiment switches.
`--qf-uf-preprocess=off` disables the preprocessing loop and the finite-domain
symmetry-breaking preprocessing step. `--no-auto-basic-single-predicate-polarity`
disables the predicate-aware phase override for selected single-assertion
finite-domain formulas. `--no-auto-basic-multi` disables the compact
multi-assertion classifier for scripted option matrices, while
`--no-auto-basic-multi-polarity` keeps that compact preprocessing branch but
uses the ordinary theory-atom phase.

## Experimental QF_UF Portfolio

`--qf-uf-portfolio=basic-polarity` runs a bounded first attempt with
`--qf-uf-preprocess=basic`, `--theory-phase=polarity`, and a theory-conflict
budget before falling back to the normal configured solve. The budget defaults
to 2000 theory conflicts and is controlled by
`--qf-uf-portfolio-conflicts=N`.

The first attempt is correctness-preserving because it solves the original
SMT-LIB input after reparsing with a weaker preprocessing preset; if it returns
`sat` or `unsat`, that answer is returned. If it reaches the theory-conflict
budget, Primo discards that solver state, reparses the original input, and runs
the ordinary configured solve. Statistics from the returned attempt include
`:portfolio.qf-uf.*` counters that distinguish first-pass success from fallback.

This mode is intentionally opt-in. Local diagnostics on July 19, 2026 showed
large wins on some satisfiable QG rows such as `iso_brn_repgen033`, but also
fallback overhead on NEQ and PEQ rows. It must be evaluated on the full target
manifest before being considered for any default profile.

```bash
./build/primo --qf-uf-preprocess=off input.smt2
./build/primo --qf-uf-preprocess=auto input.smt2
./build/primo --qf-uf-preprocess=strong input.smt2
./build/primo --eq-bridge input.smt2
./build/primo --uf-symmetry-breaking input.smt2
./build/primo --eq-bridge --preprocessing-passes 3 input.smt2
./build/primo --eq-bridge --preprocess-passes 3 input.smt2
./build/primo --qf-uf-portfolio=basic-polarity input.smt2
```

These passes are deliberately separate from the SAT backend. They create
additional Boolean assertions over ordinary equality terms, then the existing
Boolean clausifier and lazy EUF loop solve the strengthened formula.

The `auto` preset is a dispatcher between existing sound subsets, not a new
inference rule. It was added after QF_UF archive profiling showed that
finite-model encodings such as NEQ, PEQ, SEQ, and QG operation tables can need
SAT-visible finite-domain structure, while unbounded equality-definition
expansion can also flood unrelated instances with thousands of clauses. `auto`
therefore keeps AMO clauses by default but keeps equality definitions opt-in.
The single-assertion `basic` exception is an empirical classifier derived from
the recorded 334-row option comparison: it only selects the existing `basic`
subset when the original finite-domain scan finds at least 200 choices and the
symmetry pass accepts a group with at most six non-value terms. It reports
`:preprocessing.auto.basic-single` and can be disabled with
`--no-auto-basic-single`; the default ordinary strong-auto branch reports
`:preprocessing.auto.strong`. On the July 19, 2026 334-row SLURM comparison it
selected 12 rows, gained `PEQ014_size11`, and reduced PAR2 by about 203 seconds
against the same binary with the selector disabled. A follow-up local probe found
that polarity phase helps the selected NEQ rows with Boolean UF predicate atoms,
including `NEQ023_size7`, but regresses pure-equality PEQ rows; the implemented
subset therefore gates this static phase override on `:euf.predicate-atoms > 0`
and reports `:sat.phase.auto-basic-single-predicate-polarity`. A same-binary
SLURM comparison on the 334-row user HTML set showed no wrong answers and a
clear improvement on the three rows where the override fired
(`NEQ006_size6`, `NEQ023_size7`, and `NEQ032_size6`), while the raw whole-set
PAR2 was dominated by unrelated timing variation on rows where the override was
inactive. It does not address `NEQ016_size8`, which does not select the
single-assertion `basic` branch. The basic-multi exception is
a separate empirical classifier for compact multi-assertion QG rows and reports
`:preprocessing.auto.basic-multi`. A later July 19, 2026 same-run SLURM
comparison on the 334-row user HTML set validated enabling it by default after
the auto-basic-single baseline: default and `--no-auto-basic-multi` both solved
332/334 with no wrong answers, while default reduced PAR2 by about 59 seconds
overall and by about 115 seconds on the selected QG rows. The remaining NEQ gap
is unchanged because those rows do not select the multi-assertion branch. The
compact multi-assertion branch also has a separate phase ablation,
`--no-auto-basic-multi-polarity`, because follow-up local QG diagnostics found
mixed behavior: polarity phase is excellent for rows such as
`iso_brn_repgen033`, but ordinary negative theory phase is much faster on
`iso_brn_repgen_sk032`, `iso_brn_repgen043`, and `iso_icl_nogen_sk004`.
When enabled, equality definitions must fit a domain-size and per-solve clause
budget. For single-assertion formulas they must also fit the initial
reachable-term limit, because a single top-level assertion may contain an entire
operation-table encoding. Single-assertion formulas still run equality bridging
with repeated unit-equality propagation so
`eq_diamond`-style chains keep the fast bridge path.

Very large already-flat assertion batches are a separate case. Goel hardware
instances can arrive as tens of thousands of top-level clauses; running repeated
nonclausal preprocessing over those batches can grow the assertion set and
dominate solving time. In `auto` mode, batches above 10,000 top-level assertions
therefore skip the repeated preprocessing loop and go directly to Boolean
abstraction and EUF. The default solved-constant substitution pass is still run
once on these batches because it is linear over the visible assertions, does not
generate clauses, and is guarded against definition-size blowups. Explicit
`--qf-uf-preprocess=basic` and `--qf-uf-preprocess=strong` still force the full
preprocessing loop for ablation or targeted experiments. This auto decision is
reported by `:preprocessing.auto.large-assertion-skip`.

The large-batch guard was accepted after profiling
`2018-Goel-hwbench/QF_UF_firewire_tree.5.prop3_ab_reg_max`: default `auto`
preprocessing grew the instance to 94,530 assertions and took about 56 seconds
locally, while the guarded path skipped preprocessing and solved in about 5.3
seconds. The follow-up 120-second full QF_UF SLURM run
`results/slurm-qfuf-full-forced-atoms-autoskip-120s-110529/` solved
7,492/7,503 instances with zero wrong answers and PAR2 7047.416.

Unit equality propagation runs after optional generated preprocessing facts are
appended. It collects equalities asserted at top level, including equalities
under top-level `and`, and simplifies equality atoms decided by that unit
closure. This is what turns bridge facts from diamond formulas into an immediate
pre-SAT contradiction with the final disequality.

The temporary closure used by this pass is `theory::equality::
IncrementalCongruenceClosure`, the same use-list/lookup implementation used for
incremental EUF checks over `core::TermId` values. Preprocessing assertions have
unconditional reasons, so the closure does not build congruence explanation
paths while deriving simplification-only consequences.

`--ground-unit-substitution` additionally uses the same collected non-Boolean
unit equalities for a bounded ground substitution pass. The pass preserves the
unit equalities as explicit assertions, rebuilds the remaining assertion DAG
modulo those equalities, and then lets the normal `TermManager` simplifiers
collapse equality, disequality, `and`, `or`, and implication structure. This is
a nonclausal simplification subset: it only uses positive equalities visible at
top level or under top-level conjunction, does not inspect equalities hidden
under disjunction, negation, implication, or predicates, and deliberately avoids
rewriting Bool-sorted equivalence classes. The option is not part of the default
profile yet: local probes showed useful substitutions but no reliable solve-time
win on PEQ/SEQ targets. Its effect is reported by
`:preprocessing.unit-equality.substitution-units` and
`:preprocessing.unit-equality.substitutions`.

`--guarded-ground-unit-substitution` computes the same candidate rewrite but
keeps it only when cheap preprocessing metrics predict a useful SAT/EUF shape:
the reachable term count must shrink by at least five percent, or the pass must
perform at least 1500 rewrites while adding at most 1000 assertions. Accepted
and rejected candidates are counted by
`:preprocessing.unit-equality.substitution-accepted` and
`:preprocessing.unit-equality.substitution-rejected`. These thresholds are
empirical campaign gates, not a theorem; they are meant to preserve the PEQ/NEQ
wins found in the targeted 120s SLURM campaign while filtering PEQ/SEQ cases
where substitution only adds unit equality pressure.

`--preprocessing-passes N` controls the repeated preprocessing loop. Each pass
can add bridge facts and then simplify with Boolean and unit equalities. The
default is four passes. `0` disables the loop unless the `strong` preset or the
auto single-assertion path is selected. Those paths run at least three passes
because bridge facts and forced atoms can expose further unit-equality
simplifications. Finite-domain AMO clauses, equality-definition clauses, and
symmetry constraints are emitted once after the repeated simplification loop so
generated lemmas are not fed back into every pass.

The fixed pass count is a baseline cap, not always a hard stop. After the cap is
reached, Primo may run up to four additional adaptive passes when the previous
pass reduced the number of unique reachable formula terms by at least ten
percent. This keeps useful simplification chains going on formulas that are
still collapsing, while preventing clause-generating preprocessing from
continuing merely because it can append more facts. Adaptive extension is
reported by `:preprocessing.adaptive-passes`; the measured formula sizes are
reported by `:preprocessing.term-size.initial` and
`:preprocessing.term-size.final`.

The adaptive continuation is configurable. `--preprocessing-adaptive-extra-passes
N` sets the maximum number of passes beyond `--preprocessing-passes`; `0`
disables adaptive continuation. `--preprocessing-adaptive-shrink-percent N`
sets the required reachable-term shrink threshold, from `0` to `100`.

## Boolean Unit Propagation

Each preprocessing pass collects top-level unit facts for user Bool constants,
including facts under top-level `and` and negative facts of the form `(not p)`.
If both polarities are present, the constant is left untouched and the SAT layer
keeps the contradiction.

Collected unit facts simplify Boolean structure, Boolean equalities, and guarded
formulas through the normal `TermManager` constructors. The unit facts
themselves are preserved as assertions because SMT-LIB `Bool` is a first-class
sort: a Boolean constant may also occur as an argument to an uninterpreted
function, and the EUF solver must still see the asserted Boolean fact.
Recursive rebuilding copies child `TermId` vectors before rewriting children.
This is an ownership invariant: `TermManager` may intern new terms while a child
is being simplified, so preprocessing must not keep references into term storage
across recursive calls.

The pass deliberately does not rewrite inside uninterpreted-function
applications and does not collect frontend `$primo_bool_arg_*` auxiliaries as
substitution facts. Those auxiliaries encode equivalences for compound Boolean
arguments, and erasing them can hide predicate-congruence conflicts. The pass is
therefore a bounded Boolean simplifier, not global Boolean propagation through
the EUF term language.

## Forced Atom Propagation

After Bool-constant unit propagation, each preprocessing pass also runs a
forced-atom simplifier over the current assertion conjunction. The pass follows
the standard SAT-preprocessing unit-simplification idea: collect top-level atom
facts, rebuild compound formulas under those facts, and keep the unit facts as
assertions so Boolean abstraction and EUF still see the original hypotheses.

The collected atoms are:

- user Bool constants, excluding frontend `$primo_bool_arg_*` auxiliaries;
- Bool-valued uninterpreted applications, such as predicate atoms;
- equality atoms;
- negated forms of the same atoms.

Exact atoms seen in both polarities are not used for rewriting. This avoids
making simplification order-dependent on already-inconsistent input; the SAT/EUF
layers still receive the contradictory facts.

Positive equality units additionally seed a temporary congruence closure.
Negative equality units are retained as known disequalities. When rebuilding
compound formulas, equality atoms are folded to `true` if the closure proves the
endpoints equal and folded to `false` if their equality would contradict a known
disequality modulo the positive closure. This catches common QF_UF patterns such
as a disequality unit eliminating one branch of a disjunction, or transitive
equalities contradicting a disequality before SAT search.

After the temporary closure is saturated, negative equality units are indexed by
their unordered pair of congruence-class representatives. This keeps the
rewriter's disequality test proportional to the number of equality atoms being
rebuilt instead of scanning every known disequality for every query. The index is
local to one preprocessing pass and the original disequality assertions remain in
the output.

This pass uses the same temporary incremental closure as unit equality
propagation. The closure gives preprocessing affected-use-list congruence
updates instead of the older all-pairs saturation scaffold, but it remains a
pre-SAT simplifier: generated equalities and disequalities are preserved as
assertions rather than hidden inside preprocessing state.

The pass does not rewrite inside uninterpreted-function applications. A
Bool-valued predicate atom may be replaced when it occurs as a formula atom, but
Boolean terms used as arguments to uninterpreted functions remain represented by
the existing frontend auxiliaries and EUF facts.

Activation is reported by `:preprocessing.forced-atoms.facts`,
`:preprocessing.forced-atoms.disequalities`, and
`:preprocessing.forced-atoms.rewrites`.

## Solved-Constant Substitution

Primo runs a small solved-equation preprocessing pass after forced-atom
propagation. The pass is a local design-note implementation of the standard
satisfiability-preserving first-order simplification rule for free constants:
if a visible unit equality has the form `x = t`, where `x` is a non-Boolean
uninterpreted constant and `x` does not occur in `t`, then `x` may be eliminated
by rewriting the assertion conjunction with `t`.

This is not a full `solve-eqs` tactic. The implemented subset is deliberately
bounded:

- candidates are collected only from top-level assertions and top-level
  conjuncts;
- only non-Boolean constants are eliminated;
- definitions whose right-hand side contains the eliminated constant are
  rejected;
- definitions that depend on another simultaneously solved constant are rejected
  for this pass and may be exposed in a later preprocessing pass;
- definitions larger than 64 tree nodes, or whose occurrence-count product would
  exceed 200,000 added nodes, are rejected to avoid accidental blowups;
- all terms are rebuilt through `core::TermManager`, preserving hash-consing and
  sort checks.

The pass preserves satisfiability, not a user-facing model for the eliminated
constant. Primo currently does not expose model reconstruction, so this is
acceptable for `check-sat`; model support would need a substitution trail.

The pass is reported by:

- `:preprocessing.solve-eqs.candidates`;
- `:preprocessing.solve-eqs.accepted`;
- `:preprocessing.solve-eqs.rewrites`;
- `:preprocessing.solve-eqs.rejected-cycles`;
- `:preprocessing.solve-eqs.rejected-blowups`.

Use `--no-solved-constant-substitution` for ablation. Broader equation solving,
including Boolean constants, equalities hidden under disjunction, and
context-safe conditional definitions from lowered ITE chains, remains future
work and needs a separate design note before implementation.

## ITE Lowering

The SMT-LIB frontend eliminates ITEs before Boolean clausification because the
core term language does not store ITE nodes. Boolean ITEs are lowered to Boolean
connectives. Non-Boolean ITEs in general term position still use a fresh
constant plus two guarded equalities.

For the common assignment shape:

```text
(= x (ite c a b))
```

the frontend avoids the fresh non-Boolean auxiliary and emits the guarded
equalities directly:

```text
(and (=> c (= x a))
     (=> (not c) (= x b)))
```

This is a targeted preprocessing rule, not a full strong ITE simplifier. Primo
does not yet perform global ITE hoisting, condition-aware ITE-chain
simplification, or domain-specific ITE table compression.

## Equality Bridge

The equality bridge targets equality-diamond formulas from the SMT-LIB QF_UF
benchmarks, generated from the Strichman-Rozanov minimum-transitivity-constraints
workshop benchmark family.

The pass scans `or` subformulas. For every branch, it computes the equality
closure of equality atoms found directly in that branch or under nested `and`.
If every branch implies the same equality between shared terms, the pass appends
that equality as a new unit assertion.

Example:

```text
(or (and (= x y) (= y z))
    (and (= x w) (= w z)))
```

Both branches imply `x = z`, so `--eq-bridge` appends `(= x z)`.

For the diamond chain:

```text
(or (and (= x_i y_i) (= y_i x_{i+1}))
    (and (= x_i z_i) (= z_i x_{i+1})))
```

each disjunction implies `x_i = x_{i+1}`. Adding these bridge equalities lets the
ordinary EUF checker derive the final contradiction without exploring all branch
combinations.

The implementation is intentionally conservative:

- it looks through `or` and `and`, but not through negation, implication, or
  predicates;
- it only adds equalities implied by every branch;
- it caps branch count and pair checks per `or` node;
- generated bridge equalities are ordinary `TermId` equality assertions.

## Finite-Domain Clauses

The finite-domain recognizer has three AMO sources. The first is a raw
preprocessing scan over the original QF_UF assertions, before Boolean-unit and
forced-atom simplification can turn a full operation-table choice into a
residual choice. It looks for explicit choice disjunctions of the form:

```text
(or (= t c0) (= t c1) ... (= t cn))
```

and requires top-level pairwise disequalities among all candidate values. For
each recognized domain, `strong` preprocessing appends SAT-visible at-most-one
clauses:

```text
(or (not (= t ci)) (not (= t cj)))
```

for every pair of distinct values. These are ordinary EUF theory lemmas: if
`ci` and `cj` are explicitly asserted disequal, a single term cannot equal both
values. Raw clauses are delayed until after the destructive simplification loop
and are deduplicated against the post-simplification recognizer before they are
added to the assertion vector. The raw scan is counted by
`:preprocessing.finite-domain.raw-choices` and
`:preprocessing.finite-domain.raw-amo-clauses`; post-simplification clauses are
counted by `:preprocessing.finite-domain.post-amo-clauses`; duplicates removed
between raw, post, and already-present assertions are counted by
`:preprocessing.finite-domain.deduped-amo-clauses`.

The second source repeats explicit-choice recognition after the ordinary
preprocessing loop. This keeps AMO generation useful on formulas whose choices
are exposed by earlier passes.
Use `--no-finite-domain-post-amo` for same-binary ablations that keep the raw
choice AMO clauses but suppress this post-simplification scan.

The third source scans equality atoms already present in the assertion DAG. If
the formula mentions both `s = u` and `s = v`, and `u` and `v` are explicit
top-level disequal non-Boolean terms, the pass may append the same AMO clause
even when there is no choice disjunction for `s`. This generalizes the common
constant-value case to arbitrary disequality endpoints, but it is still only a
theory-lemma strengthening over existing equality atoms. It does not add
one-of-domain constraints, fresh finite-domain variables, or finite-model
search.

If two recognized finite-domain terms `s` and `t` are explicitly asserted
disequal, or are forced disequal by the top-level forced-atom pass, and share
the same value set, the pass also appends
distinct-endpoint AMO clauses:

```text
(or (not (= s ci)) (not (= t ci)))
```

for every domain value `ci`. Forced disequalities are carried with the forced
equalities from the same preprocessing snapshot, so the endpoint check is modulo
those equalities. This is still an asserted-fact strengthening: it does not infer
disequalities hidden under arbitrary Boolean structure and it does not perform
finite-model search. Symmetry breaking remains narrower than AMO generation: the
current symmetry pass still uses the original assertion vector and requires
explicit choice disjunctions before it accepts a finite-domain value set.

When an equality atom between two recognized finite-domain terms occurs in the
assertion DAG, `strong` preprocessing also appends equality-definition clauses
over that existing equality atom. For the observed pair `s, t` and every value
`ci`, the generated clauses state that:

- if `s = ci` and `t = ci`, then `s = t`;
- if `s = t` and `s = ci`, then `t = ci`;
- if `s = t` and `t = ci`, then `s = ci`.

Together with the explicit choice disjunctions and at-most-one clauses, these
clauses expose finite-domain equality structure to SAT without fresh Boolean
variables or SAT/theory shortcuts. The pass deliberately does not define every
pair of terms in a finite-domain group: it only strengthens equality atoms the
formula already mentions, which keeps the expansion bounded on NEQ/PEQ/SEQ
families. They are counted by `:preprocessing.equality-definitions`;
`:preprocessing.equality-definitions.candidate-pairs` counts observed same-domain
term equalities.

A July 20, 2026 local probe on the NEQ/PEQ gap confirmed the limits of this
observed-pair design. Forced finite-domain equality definitions generated no
clauses on `NEQ016_size7` or `NEQ016_size8`, because the observed equality pairs
were not pairs of terms with explicit recognized finite-domain choices. On
`PEQ002_size8`, unbounded forced equality definitions generated 10,752 clauses
but made the 50,000-theory-conflict capped run slower. The pass therefore
remains guarded and is not a default solution for the NEQ/PEQ operation-table
slowdown.

`--ground-congruence` emits a bounded subset of ordinary ground-EUF congruence
clauses over applications already present in the formula:

```text
(or (not (= a0 b0)) ... (not (= an bn)) (= (f a0 ... an) (f b0 ... bn)))
```

No fresh applications or propositional variables are introduced. Clauses are
deduplicated structurally, capped per preprocessing call, and counted by
`:preprocessing.ground-congruence-clauses`; skipped groups are counted by
`:preprocessing.ground-congruence-skipped-groups`.

Choice disjunctions may be nested `or` trees. The recognizer flattens the `or`
node before checking that every branch is an equality between the same candidate
term and one explicit value.

## UF Symmetry Breaking

The recognizer looks for finite-domain choice formulas:

```text
(or (= t c0) (= t c1) ... (= t cn))
```

where `c0 ... cn` are constants and every pair of values is asserted distinct at
top level. In SMT-LIB this usually appears as assertions such as:

```text
(assert (not (= c0 c1)))
(assert (not (= c0 c2)))
(assert (not (= c1 c2)))
(assert (or (= x c0) (= x c1) (= x c2)))
```

Terms with the same value set form a candidate symmetry group. A group is
accepted only if:

- all values are constants of an uninterpreted sort, not Bool-valued
  propositions;
- the accumulated assertion multiset is syntactically invariant under the value
  cycle and under swapping the first two values.

For an accepted group, candidate terms are annotated with the group values that
occur syntactically inside the term. If the group contains fixed terms, meaning
recognized finite-domain terms that contain none of the permuted values, Primo
orders those fixed terms by `TermId` and emits value-precedence restrictions for
that fixed sequence. The first fixed term may use only the first value, the
second may use only the first two values, and so on; when a later term uses
value `v_j`, an earlier fixed term must have used `v_{j-1}`. These restrictions
are emitted as ordinary SMT assertions over the existing equality atoms.

If a candidate group has no fixed terms after dropping terms that contain the
permuted values, the pass rejects the group. A QG-style table term such as
`(op c0 c1)` moves when the values `c0, c1, ...` are permuted, so it is not a
stable ordered representative for value-precedence clauses. This conservative
guard prevents benchmark-provided symmetry constraints from being strengthened
with restrictions that are not justified by the implemented subset.

Some QG-derived formulas also contain fixed Skolem-style constants with explicit
finite-domain choices over the same value set. Those fixed terms do not contain
the permuted values, so they remain eligible even when the operation-table terms
are dropped. The pass should then report one accepted symmetry set, count only
the fixed terms, and emit value-precedence clauses for that fixed sequence.
`benchmarks/seeds/qf_uf/qg_symmetry_fixed_terms.smt2` keeps this behavior in the
seed corpus; `qg_symmetry_residual_domain.smt2` covers the rejecting side.

`--finite-domain-value-precedence` is default-on. Disabling it keeps the same
fixed-term eligibility guard, but emits the weaker Déharbe-style unit exclusions
only for the fixed terms. Primo does not currently implement a moving-term
symmetry restriction.

These assertions remove value-renaming representatives from the search space.
They do not replace EUF reasoning and they do not create a finite model finder by
themselves.

UF symmetry breaking is emitted after the fixed-point simplification loop, but
the recognizer checks the source assertion snapshot from before that loop. The
ordering matters: preprocessing can turn a full finite-domain choice into a
residual partial-domain disjunction after propagating forced negative equality
units, and such residual domains are not independent evidence of a source-level
value symmetry. AMO/equality-definition clauses are generated after symmetry
breaking and are treated as derived strengthening lemmas, not symmetry evidence.

Activation is reported with `:preprocessing.uf-symmetry.accepted-sets`,
`:preprocessing.uf-symmetry.values`, `:preprocessing.uf-symmetry.terms`,
`:preprocessing.uf-symmetry.constraints`,
`:preprocessing.uf-symmetry.value-precedence-terms`,
`:preprocessing.uf-symmetry.value-precedence-constraints`, and
`:preprocessing.uf-symmetry.rejected-noninvariant`.

## Implementation Notes

The preprocessing passes operate on hash-consed `core::TermId` values after
SMT-LIB parsing.

Equality bridging builds a small local union-find per disjunction branch. It
intersects the terms that occur in equality atoms in every branch, then checks
pairs of shared terms for equivalence in every branch closure.

Unit equality propagation builds one local union-find from top-level equality
facts and rewrites equality atoms whose endpoints are already in the same class
to `true`. Negated equalities then simplify through `mk_not`, and enclosing
Boolean structure simplifies through the existing `TermManager` constructors.
It does not inspect equalities hidden under `or`, implication, negation, or
predicate applications.

Finite-domain equality definitions are owned by
`smt::FiniteDomainEqualityDefinitions` in `src/smt/preprocessing.*`. The pass
collects explicit finite-domain choice disjunctions and top-level value
disequalities, records equality atoms that occur in the assertion DAG, and emits
ordinary SMT assertions relating an observed `(= s t)` to the already-visible
atoms `(= s c_i)` and `(= t c_i)` when both endpoints have the same recognized
finite-domain value list. It does not create fresh Boolean variables, inspect
SAT backend state, or define unmentioned term pairs. In `auto`,
`CheckSatOptions::auto_finite_domain_equality_definitions`
must be set before this pass runs. Then
`CheckSatOptions::finite_domain_eqdefs_auto_domain_limit` and
`CheckSatOptions::finite_domain_eqdefs_auto_clause_limit` bound the expansion;
the clause limit applies to the single post-loop equality-definition expansion.
`CheckSatOptions::finite_domain_eqdefs_auto_term_limit` blocks this expansion on
large single-assertion formulas. Zero means unlimited for each of these guards.
Guard decisions are reported with
`:preprocessing.equality-definitions.candidate-pairs`,
`:preprocessing.equality-definitions.accepted-pairs`,
`:preprocessing.equality-definitions.predicted-clauses`,
`:preprocessing.equality-definitions.rejected-domain-size`, and
`:preprocessing.equality-definitions.rejected-clause-limit`.

Ground unit-equality substitution uses a second local union-find over the
endpoints of the same visible non-Boolean unit equalities. Each equivalence
class is represented by a deterministic existing term, preferring constants and
then the smallest `TermId`. Recursive rebuilding rewrites child terms first,
then rewrites the rebuilt application if it is itself a known unit-equality
endpoint. The original unit equalities are emitted before the rewritten
assertions, so replacing a term by its representative is justified by facts
still present in the formula. This is not a full congruence-closure
preprocessor: it substitutes endpoints already connected by visible unit
equalities and relies on repeated passes plus hash-consing to expose additional
exact endpoint matches.

Boolean unit propagation collects top-level user Bool constants and rewrites
Boolean structure with those constants while preserving the unit assertions. It
does not rewrite inside UF applications, so SAT/EUF still receives the facts
needed for Boolean terms used as first-order arguments.

UF symmetry breaking traverses top-level conjunctions to collect explicit
disequalities, then scans `or` nodes for choice disjunctions. The invariance
check builds canonical string keys for formulas after applying a value
permutation. Equality, `and`, and `or` children are sorted in the key because
those operators are commutative for this syntactic check.

Generated assertions are ordinary `TermId` formulas using `mk_not`,
`mk_implies`, `mk_or`, and `mk_eq`. They are appended before QF_UF solving, so
they reuse the same SAT abstraction and congruence-closure code as user
assertions.

## Current Status

Implemented:

- preset CLI option `--qf-uf-preprocess=off|basic|strong|auto`;
- CLI-default `auto` preprocessing dispatcher;
- equality bridging under disjunctions;
- unit equality propagation from top-level equalities;
- opt-in ground unit-equality substitution over non-Bool terms;
- guarded opt-in ground unit-equality substitution for campaign evaluation;
- Boolean unit propagation from top-level user Bool constants, preserving the
  unit facts for EUF;
- configurable repeated preprocessing loop with `--preprocessing-passes`;
- targeted equality-ITE lowering for `(= x (ite c a b))` without a fresh
  non-Boolean auxiliary;
- finite-domain choice recognition from explicit disjunctions;
- finite-domain at-most-one clauses in the strong preset and auto path, from
  explicit choices and from observed equality atoms to explicitly disequal
  constants;
- finite-domain equality-definition clauses in the strong preset and guarded
  finite-domain equality-definition clauses in the auto preset;
- opt-in bounded ground-congruence clauses;
- top-level pairwise disequality requirement;
- syntactic constant-permutation invariance check;
- Algorithm 1 symmetry restrictions that preserve constants occurring in moving
  terms;
- CLI flag `--eq-bridge`;
- CLI flag `--uf-symmetry-breaking`;
- CLI flag `--no-uf-symmetry-breaking`, which disables symmetry breaking even
  for the strong preprocessing preset;
- CLI flags `--finite-domain-value-precedence` and
  `--no-finite-domain-value-precedence`;
- CLI flags `--finite-domain-equality-definitions` and
  `--no-finite-domain-equality-definitions`;
- unit tests for equality bridging and for accepted, asymmetric, and moving-term
  symmetry-breaking cases.

Deferred:

- proof support for generated preprocessing lemmas;
- unconstrained-value simplification as a global formula transformation;
- strong term-ITE conversion beyond frontend lowering of the targeted
  assignment shape;
- minimum-transitivity-constraint generation and selective Ackermannization for
  equality logic;
- SyGuS-derived rewrite-rule enumeration for preprocessing;
- finite-domain equality evaluation in the strong preset. A prototype evaluator
  exists as an internal utility, but it remains disabled because unguarded
  equality simplification can be unsound on PEQ-like cases;
- a semantic finite-model profile selector. The current `auto` dispatcher uses
  assertion shape only and must be judged by full-archive SLURM results, not by
  individual local timings;
- enabling `--ground-congruence` in the tuned profile. Exact-set evaluation
  regressed from 2 solved rows to 1 solved row on SLURM job 108936, so the
  clause generator remains explicit opt-in pending a better filter;
- SyMT-style graph automorphism detection;
- full-archive symmetry-only A/B reports with activation counts.

## Paper Traceability

This pass is a clean Primo implementation of the idea that was successful in
LLM2SMT `lra`; no LLM2SMT code was copied.

- Ofer Strichman and Mirron Rozanov, *Generating minimum transitivity
  constraints in P-time for deciding Equality Logic*, SMT Workshop 2005, is the
  source named by the SMT-LIB equality-diamond benchmarks. Primo does not
  implement a full minimum-transitivity-constraint generator; `--eq-bridge` is a
  narrow disjunction-consequence pass for that benchmark shape.
- Déharbe, Fontaine, Merz, and Woltzenlogel Paleo, *Exploiting symmetry in SMT
  problems*, is the source for SMT symmetry breaking by invariance under
  permutations of uninterpreted constants.
- Reger and Suda, *Using SAT and SMT Solvers for Finite Model Finding with
  Sorts*, remains background for finite-model value ordering; Primo's current
  SMT symmetry pass uses the Déharbe et al. restriction algorithm instead.
- Bradley and Manna, *The Calculus of Computation*, Chapter 9, is the
  background reference for ground EUF reasoning used by the finite-domain
  at-most-one lemmas.
- Barrett et al., *CVC4*, CAV 2011, is the architecture reference for treating
  nonclausal simplification as a preprocessing stage. Primo implements only the
  visible unit-equality substitution subset described above.

See [](papers/smt-symmetry-breaking.md) and
[](papers/finite-model-finding-sorts.md).
