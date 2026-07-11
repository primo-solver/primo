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
overrides it. The EUF callback and propagation options are intentionally not
all default: plain Primo currently uses `--euf-prop=off`, while the callback
and online modes remain explicit tuning modes until their backtracking path is
fast on the Goel-style regressions.

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
- unit equality propagation, enabled by the `basic` and `strong` preprocessing
  loops;
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

Unit equality propagation runs after optional generated preprocessing facts are
appended. It collects equalities asserted at top level, including equalities
under top-level `and`, and simplifies equality atoms decided by that unit
closure. This is what turns bridge facts from diamond formulas into an immediate
pre-SAT contradiction with the final disequality.

`--preprocessing-passes N` controls the repeated preprocessing loop. Each pass
can add bridge facts and then simplify with unit equalities. The default is four
passes. `0` disables the loop unless the `strong` preset is selected. Strong
preprocessing runs at least three passes because bridge facts and finite-domain
clauses can expose further unit-equality simplifications.

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

The finite-domain recognizer looks for explicit choice disjunctions of the form:

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
values. If two recognized finite-domain terms `s` and `t` are explicitly
asserted disequal and share the same value set, the pass also appends
distinct-endpoint AMO clauses:

```text
(or (not (= s ci)) (not (= t ci)))
```

for every domain value `ci`. The pass is intentionally narrow. It does not infer
disequalities hidden under Boolean structure and it does not perform finite-model
search.

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
- configurable repeated preprocessing loop with `--preprocessing-passes`;
- finite-domain choice recognition from explicit disjunctions;
- finite-domain at-most-one clauses in the strong preset and auto path;
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

See [](papers/smt-symmetry-breaking.md) and
[](papers/finite-model-finding-sorts.md).
