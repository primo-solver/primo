# Preprocessing

Primo currently exposes QF_UF preprocessing through
`--qf-uf-preprocess=off|basic|strong|auto`. The CLI and
`CheckSatOptions` default is `auto`.

- `off` disables the preprocessing loop;
- `basic` runs one pass of unit equality propagation;
- `strong` enables repeated preprocessing, equality bridging, finite-domain
  at-most-one clauses, finite-domain equality definitions, and finite-domain
  symmetry breaking;
- `auto` keeps equality bridging, finite-domain at-most-one clauses, and
  symmetry breaking when their recognizers apply, but skips finite-domain
  equality-definition expansion. The explicit `strong` preset adds equality
  definitions for experiments.

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
  by the `auto` path;
- bounded ground-congruence clauses over applications already present in the
  formula, enabled by `--ground-congruence`;
- symmetry breaking over finite-domain choice disjunctions, enabled by default
  when preprocessing is enabled and explicitly controllable with
  `--uf-symmetry-breaking` / `--no-uf-symmetry-breaking`.

`--no-eq-bridge` and `--no-uf-symmetry-breaking` disable the two default-on
legacy switches for ablation runs. `--qf-uf-preprocess=off` disables the
preprocessing loop and the finite-domain symmetry-breaking preprocessing step.

```bash
./build/primo --qf-uf-preprocess=off input.smt2
./build/primo --qf-uf-preprocess=auto input.smt2
./build/primo --qf-uf-preprocess=strong input.smt2
./build/primo --eq-bridge input.smt2
./build/primo --uf-symmetry-breaking input.smt2
./build/primo --eq-bridge --preprocessing-passes 3 input.smt2
./build/primo --eq-bridge --preprocess-passes 3 input.smt2
```

These passes are deliberately separate from the SAT backend. They create
additional Boolean assertions over ordinary equality terms, then the existing
Boolean clausifier and lazy EUF loop solve the strengthened formula.

The `auto` preset is a dispatcher between existing sound subsets, not a new
inference rule. It was added after QF_UF archive profiling showed that
finite-model encodings such as NEQ, PEQ, SEQ, and QG operation tables can be
slowed by thousands of finite-domain equality-definition clauses, while many of
them still need the cheaper at-most-one clauses. `auto` therefore keeps AMO
clauses but leaves equality definitions to the explicit `strong` preset.
Single-assertion formulas still run equality bridging with repeated
unit-equality propagation so `eq_diamond`-style chains keep the fast bridge
path.

Very large already-flat assertion batches are a separate case. Goel hardware
instances can arrive as tens of thousands of top-level clauses; running repeated
nonclausal preprocessing over those batches can grow the assertion set and
dominate solving time. In `auto` mode, batches above 10,000 top-level assertions
therefore skip preprocessing and go directly to Boolean abstraction and EUF.
Explicit `--qf-uf-preprocess=basic` and `--qf-uf-preprocess=strong` still force
the preprocessing loop for ablation or targeted experiments. This auto decision
is reported by `:preprocessing.auto.large-assertion-skip`.

Unit equality propagation runs after optional generated preprocessing facts are
appended. It collects equalities asserted at top level, including equalities
under top-level `and`, and simplifies equality atoms decided by that unit
closure. This is what turns bridge facts from diamond formulas into an immediate
pre-SAT contradiction with the final disequality.

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
because bridge facts and finite-domain clauses can expose further unit-equality
simplifications.

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

The pass does not rewrite inside uninterpreted-function applications. A
Bool-valued predicate atom may be replaced when it occurs as a formula atom, but
Boolean terms used as arguments to uninterpreted functions remain represented by
the existing frontend auxiliaries and EUF facts.

Activation is reported by `:preprocessing.forced-atoms.facts`,
`:preprocessing.forced-atoms.disequalities`, and
`:preprocessing.forced-atoms.rewrites`.

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

The finite-domain recognizer has two AMO sources. The first looks for explicit
choice disjunctions of the form:

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
values.

The second source scans equality atoms already present in the assertion DAG. If
the formula mentions both `s = ci` and `s = cj`, and `ci` and `cj` are explicit
top-level disequal non-Boolean constants, the pass may append the same AMO
clause even when there is no choice disjunction for `s`. This is still only a
theory-lemma strengthening over existing equality atoms. It does not add
one-of-domain constraints, fresh finite-domain variables, or finite-model
search.

If two recognized finite-domain terms `s` and `t` are explicitly asserted
disequal and share the same value set, the pass also appends
distinct-endpoint AMO clauses:

```text
(or (not (= s ci)) (not (= t ci)))
```

for every domain value `ci`. The pass is intentionally narrow. It does not infer
disequalities hidden under Boolean structure and it does not perform finite-model
search. Symmetry breaking remains narrower than AMO generation: the current
symmetry pass still requires explicit choice disjunctions before it accepts a
finite-domain value set.

When two or more terms share the same recognized finite domain, `strong`
preprocessing also appends equality-definition clauses over existing equality
atoms. For every pair of terms `s, t` and every value `ci`, the generated clauses
state that:

- if `s = ci` and `t = ci`, then `s = t`;
- if `s = t` and `s = ci`, then `t = ci`;
- if `s = t` and `t = ci`, then `s = ci`.

Together with the explicit choice disjunctions and at-most-one clauses, these
clauses expose finite-domain equality structure to SAT without fresh Boolean
variables or SAT/theory shortcuts. They are counted by
`:preprocessing.equality-definitions`.

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

For an accepted group, the pass follows Déharbe et al.'s Algorithm 1 rather than
the finite-model value-precedence shortcut. Candidate terms are annotated with
the group values that occur syntactically inside the term. Terms are then ordered
by occurrence density, with stable `TermId` tie-breaking. The pass maintains the
set of values already used by previously selected terms; for each selected term
it first keeps every value contained in that term, then adds one fresh value as a
new representative if one remains.

A table term such as `(f c0)` is therefore no longer discarded merely because it
contains `c0`: the generated restriction may rule out values not yet used, but it
cannot rule out `c0` itself. The emitted assertions are unit exclusions for
values outside the allowed representative set. These exclusions are equivalent
to the paper's disjunctive restriction in the presence of the recognized
finite-domain choice assertion for the same term.

These assertions remove value-renaming representatives from the search space.
They do not replace EUF reasoning and they do not create a finite model finder by
themselves.

Activation is reported with `:preprocessing.uf-symmetry.accepted-sets`,
`:preprocessing.uf-symmetry.values`, `:preprocessing.uf-symmetry.terms`,
`:preprocessing.uf-symmetry.constraints`, and
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
- finite-domain equality-definition clauses in the strong preset;
- opt-in bounded ground-congruence clauses;
- top-level pairwise disequality requirement;
- syntactic constant-permutation invariance check;
- Algorithm 1 symmetry restrictions that preserve constants occurring in moving
  terms;
- CLI flag `--eq-bridge`;
- CLI flag `--uf-symmetry-breaking`;
- CLI flag `--no-uf-symmetry-breaking`, which disables symmetry breaking even
  for the strong preprocessing preset;
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
