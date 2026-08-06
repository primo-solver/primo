# The Nelson-Oppen QF_LRA regression: evidence, mechanisms, and how to fix it safely

Written 2026-08-07. Main has lost **59 net files** on the full SMT-LIB QF_LRA
division against a commit from four days earlier, and the loss traces to the
merge that brought the Nelson-Oppen / theory-combination work into main. This
note collects the evidence, the two mechanisms identified so far, the one still
unexplained, and -- the part that matters most -- **why the obvious fix is
riskier than it looks**.

It is deliberately separate from [](open-leads.md) (a register of *ideas*) and
from [](arithmetic.md) (a description of what is *implemented*). This is a
defect report about main.

## What was measured

Full division, 1,753 files, 300s, 40 shards, `--cpus-per-task=3`, all three
runs on the same three nodes with the same split (jobs 118904, 120122, 120072).

| | solved / 1753 | wrong | PAR2 |
|---|---:|---:|---:|
| `b6b8d55` (2026-08-03) | **1631** | 0 | 95,091 |
| main `0c0843d` | **1572** | 0 | 136,330 |
| branch `qflra-tropical-t1-propagation` | **1588** | 0 | 127,101 |

- **main vs `b6b8d55`: gained 2, lost 61.** LassoRanker 46, 2019-ezsmt 12,
  miplib 2, tropical-matrix 1.
- **branch vs main: gained 16, lost 0**, PAR2 -9,229, median paired 0.94. All
  16 gains are LassoRanker.

**Zero wrong answers in all three runs.** This corpus carries
`(set-info :status)`, so that is a real soundness check, not just cross-solver
agreement. The regression is performance-only.

Bisected (first-parent) to **`84336a2` "Merge remote-tracking branch
'origin/main'"** -- 46 files, 14,392 insertions, introducing
`src/smt/theory_combination.{cpp,hpp}`. Its first parent `38be7fd` is good.

(mechanism-dispatch)=
## Mechanism A: a propositional variable is treated as an EUF predicate

`summarize_formula_features` (`src/smt/check_sat.cpp`):

```cpp
summary.has_euf_predicate =
    (term.kind == core::TermKind::Application || term.kind == core::TermKind::Constant) &&
    term.sort == terms.symbols().bool_sort();
```

and the dispatch:

```cpp
if ((has_equality || has_euf_predicate) && has_arithmetic) { ... solve_qf_uf_lra ... }
```

A `Constant` of sort `Bool` is **a plain propositional variable**. It has no
arguments, so it can never participate in a congruence rule -- there is nothing
for EUF to do with it. But it sets `has_euf_predicate`, and any QF_LRA formula
containing one is routed to the mixed solver.

Introduced by `ffaf9eb` "Add separable QF_UFLRA final checks" (2026-07-30),
which added `contains_euf_predicate_atom` alongside this.

**Measured, on `2019-ezsmt/travellingSalesperson/tsp_rand_70_300_1155482584_7`
-- a file with zero sorts and zero functions with arguments:**

| commit | `:solver.qf-lra` | `:solver.qf-uflra` | result |
|---|---:|---:|---|
| `4d4a185` (parent) | **1** | -- | **sat, 0.93s**, 11,735 pivots |
| `ffaf9eb` | 0 | **1** | **timeout**, deterministic |

**A second over-trigger in the same function**, not yet measured but visible in
the code: `has_equality` is already refined so a plain *Real* equality does not
set it, but a **Bool** equality does --

```cpp
if (!real_equality || summary.has_application_term) { summary.has_equality = true; }
```

so `(= p q)` between two propositional variables also routes a QF_LRA formula
to the mixed path. Any fix to `has_euf_predicate` that ignores this leaves an
equivalent trigger in place.

(mechanism-euf-atoms)=
## Mechanism B: those variables are then handed to congruence closure

Routing to the mixed path would be merely wasteful if the mixed path treated
propositional variables as propositional. It does not. `is_euf_theory_atom`
repeats the same predicate:

```cpp
return (term.kind == core::TermKind::Application || term.kind == core::TermKind::Constant) &&
       term.sort == terms.symbols().bool_sort();
```

and `solve_qf_uf_lra` splits every clausifier theory atom into exactly two
buckets on it:

```cpp
if (is_euf_theory_atom(terms, atom.term))      { euf_atoms.push_back(atom); }
else if (is_lra_theory_atom(terms, atom.term)) { lra_atoms.push_back(atom); }
else { throw UnsupportedInput("unsupported mixed-theory atom"); }
```

**Consequence, measured on the same file:**

| counter | pure path `4d4a185` | mixed path `ffaf9eb` |
|---|---:|---:|
| `:euf.checks` | 0 | 5,364 |
| `:euf.incremental.merges` | 0 | **39,436,128** |
| `:euf.incremental.terms` | 0 | 39,446,856 |
| `:no.mixed.euf-atoms` | -- | **7,352** |
| `:no.interface.conflicts` | -- | **0** |
| `:sat.clauses` | 26,117 | 31,481 |

The file declares **7,352 Bool-sorted nullary `declare-fun`s** -- and
`:no.mixed.euf-atoms` is exactly 7,352. Every propositional variable goes to
congruence closure, producing **39.4 million merges to decide nothing the SAT
solver does not already own**.

**This is the important framing.** Nelson-Oppen itself is not slow here and is
not at fault: `:no.interface.conflicts` is 0 and every purification counter is
zero. NO correctly no-ops, exactly as it should on a single-theory problem. The
cost comes from starting an EUF engine for a formula with no EUF content.

The pure QF_LRA path faces the same atoms and is fine, because it marks them
`is_arithmetic = false` and skips them. The mixed path has no equivalent
"neither theory owns this" bucket.

**Are the merges really on Booleans, and is `p = q` the cause?** Yes to the
first; the second is only part of it. `is_euf_theory_atom` returns true for
*any* equality whose children are not Real -- so `(= p q)` between propositional
variables is an EUF atom -- but the dominant contribution is the **bare**
Boolean atoms. Each one's truth assignment is realised as a merge with `true`
or `false`, so 7,352 propositional variables mean 7,352 merges per check
regardless of whether any equality is involved.

(mechanism-rebuild)=
## Mechanism B2: the EUF theory is rebuilt from scratch on every check

This is the part that matters beyond misclassified Booleans, and it was found
by noticing the counters are *exactly* divisible:

```text
:euf.incremental.merges 39,436,128 = 5,364 checks x 7,352 atoms   (exact)
:euf.incremental.terms  39,446,856 = 5,364 checks x 7,354 atoms   (exact, +2 = true/false)
```

Not approximately -- exactly. Every check merges every atom. The code says why
(`solve_qf_uf_lra`, inside the check loop):

```cpp
std::vector<BooleanClausifier::TheoryAtom> euf_atoms_for_check = euf_atoms;  // full copy
...
prepared_flat_theory.emplace(terms, euf_atoms_for_check, true_term, false_term, ...);
euf_check = check_incremental_euf_closure(prepared_flat_theory.value(), ...);
```

`check_incremental_euf_closure` is incremental *within* one closure, but the
`PreparedFlatTheory` it works on is **constructed fresh on every check**, so
nothing is reused across checks. The cost is O(checks x euf_atoms) by
construction.

**Verified as general, not specific to one instance.** On four further
mixed-path files the identity holds exactly:

| file | checks | euf atoms | merges | `merges == checks * atoms` |
|---|---:|---:|---:|---|
| `rand_70_300_1155482584_4.lp` | 1 | 7,332 | 7,332 | yes |
| `rand_70_300_1155482584_12.lp` | 1 | 7,352 | 7,352 | yes |
| `rand_80_340_1159656267_13.lp` | 1 | 8,262 | 8,262 | yes |
| `3.smt2` | 1 | 288 | 288 | yes |

Those files are cheap only because they need one check. The blow-up needs
*both* many atoms and many checks, which `tsp_rand_70_300_1155482584_7` has
(5,364).

**Why this is the more important finding.** Mechanisms A and B are about
formulas that should never have reached the mixed path. B2 is not: **a genuine
QF_UFLRA problem pays O(checks x atoms) too.** Fixing A and B removes the
Boolean atoms from the multiplication but leaves the multiplication. Any
mixed-theory problem with many EUF atoms and a search that needs many final
checks will hit this, and no amount of classifier tightening helps.

(mechanism-unknown)=
## Mechanism C: LassoRanker -- 46 of the 61 losses, still unexplained

**Do not assume A and B explain the regression.** They explain the 12
2019-ezsmt losses. The largest family does not fit them at all.

On `LassoRanker/CooperatingT2/dropbuf.t2.c_Iteration3_Loop_4-pieceTemplate`
(3.4 MB):

| | answer | dispatch | pivots | decisions |
|---|---|---|---:|---:|
| `b6b8d55` | **sat, 13.0s** | `:solver.qf-lra 1` | 10,217 | 256,688 |
| main `0c0843d` | timeout 150s | `:solver.qf-lra 1` | 3,793 | 190,089 |
| branch HEAD | timeout 150s | `:solver.qf-lra 1` | 4,183 | 198,695 |

- **The dispatch never flips** -- both sides run the pure LRA solver, so
  Mechanism A is not involved and Mechanism B cannot be either.
- **Not preprocessing**: `--preprocess-only` is 1.35s vs 1.42s.
- **Not the tableau**: at a 45s cap main does 1,472 pivots and 1.66M
  pivot-term-ops against `b6b8d55`'s 10,217 and 76.1M in 13s. The online
  check-call rate falls from 13,976/s to 2,143/s *while the LRA work per call
  shrinks*. Something outside the simplex is paid per check call.

**Open question to answer next:** what runs per online check call on the *pure*
QF_LRA path after `84336a2` that did not before? `perf` is unavailable in the
current sandbox; use a `-pg` build or profile on the cluster.

Interesting and unexplained: the `qflra-tropical-t1-propagation` branch
recovers **16 LassoRanker files** relative to main without targeting any of
this. Whatever the mechanism is, it interacts with tableau cost.

(how-to-fix)=
## How to fix this safely

The obvious fix -- "stop treating a Bool `Constant` as an EUF predicate" --
is correct in substance and **wrong to apply as a single edit in both places at
once**. The two layers have different risk profiles.

### The asymmetry that decides the order

| | A (dispatch) | B (atom split) | B2 (rebuild per check) |
|---|---|---|---|
| what it decides | *which solver runs* | which theory owns an atom | how often EUF state is rebuilt |
| if too narrow | **unsoundness** | none: SAT owns it anyway | n/a |
| if left as is | slow | slow | **slow on genuine QF_UFLRA too** |
| perturbs search elsewhere | **yes, massively** | only already-mixed files | no -- pure performance |
| fixes the regression | 12 of 61 files | with A | none of it directly |

**So fix B first**, and treat **B2 as the one that matters for the NO work
going forward** even though it does not by itself recover a single lost file:
it is the only one of the three that a correctly-classified QF_UFLRA problem
still pays. It is where the 39.4M merges are, it cannot introduce
unsoundness, and it does not change which solver runs on any file -- which
means the change is measurable against a stable baseline. Fixing A first
changes the dispatch for a large fraction of the corpus and makes every
before/after comparison a comparison of two different solvers.

### Fixing B: add the missing third bucket

The trap is the `else` branch: it **throws**. Removing propositional variables
from `euf_atoms` without adding somewhere for them to go turns a performance
bug into `UnsupportedInput` on every affected file. The change is therefore:

1. Recognise atoms that neither theory owns -- Bool-sorted `Constant`s with no
   arguments, and Bool-sorted equalities over Bool -- as *propositional*.
2. Give them a third bucket that is neither `euf_atoms` nor `lra_atoms`, and
   confirm every downstream consumer tolerates an atom in neither list.
   `shared_real_constants`, the interface-equality registry and the model
   builder all iterate these vectors; each needs checking, not assuming.
3. Mirror what the pure path already does (`is_arithmetic = false` and skip)
   rather than inventing a new convention.

**Falsifier, stated in advance:** on
`tsp_rand_70_300_1155482584_7`, `:euf.incremental.merges` must drop from 39.4M
to ~0 and `:no.mixed.euf-atoms` from 7,352 to ~0, with the answer unchanged. If
merges stay high, the classification is not where the atoms are entering EUF
and this analysis is wrong.

### Fixing A: only after B, and measure it as a dispatch change

Restrict `has_euf_predicate` to `Application` (arity >= 1) of sort Bool, and
handle the Bool-equality trigger noted in [](#mechanism-dispatch) at the same
time -- fixing one and leaving the other achieves little.

**Before doing so, re-read [](open-leads.md)'s lead 2b.** The dispatch is a
known nondeterminism trigger: a semantically irrelevant edit already flips
which solver runs, and the flip is worth up to an order of magnitude in either
direction. Consequences for the fix:

- **The A/B is not a normal A/B.** Files that change path are being solved by a
  different program, not a faster one. Report gained/lost per family, not just
  PAR2.
- **Err toward the mixed path when unsure.** A missed EUF obligation is a wrong
  answer; an unnecessary mixed dispatch is only slow. If B is fixed first, the
  cost of erring wide is much lower, which is another reason for that order.
- **Fuzz it as a soundness change, not a performance change.** The QF_LRA model
  fuzz gate on Slurm is the minimum; mutation fuzzers add and remove
  declarations, so they exercise exactly this classifier.

### Fixing B2: reuse the prepared theory across checks

The atom set does **not** change between checks -- `euf_atoms` is built once
before the loop, and `euf_atoms_for_check` is a copy of it plus whatever
interface equalities the round appends. So the rebuild is not required by
anything; it is incidental.

The shape of the fix is to construct `PreparedFlatTheory` once outside the
loop and push/pop the per-check assignment, mirroring what the LRA side
already does (`OnlineLraPropagator` copies `base_tableau` once and uses
`push()`/`pop()`, see [](arithmetic.md)).

**The complication to respect**: `euf_atoms_for_check` is not always identical
to `euf_atoms` -- `append_true_interface_equalities` and
`append_interface_equalities` extend it during the round. So the reusable part
is the base theory over `euf_atoms`, with the round's interface equalities
layered on top and retracted afterwards. A first version that reuses the base
and rebuilds only when the interface set changes would capture most of the win
with far less risk than a full incremental rewrite.

**Falsifier:** `:euf.incremental.merges` must stop being exactly
`checks * atoms`. If it still divides exactly, the base theory is still being
rebuilt.

(boolean-test-cases)=
### Booleans have more shapes than "a propositional variable" -- test all of them

Any fix here is a change to *how Booleans are classified*, and Booleans appear
in SMT in several forms that behave differently. A test suite that only covers
`(declare-fun p () Bool)` will pass while the fix is broken. The cases, with
what each one must do:

| shape | `TermKind` | must route / classify as | why |
|---|---|---|---|
| `(declare-fun p () Bool)` | `Constant` | **not** EUF | nullary: no congruence rule can apply |
| `true` / `false` | `BoolValue` | **not** EUF | a literal, distinct kind -- verified, never matched `has_euf_predicate` |
| `(declare-fun f (Real) Bool)`, used as `(f x)` | `Application` | **EUF** | congruence applies: `x = y` implies `f(x) = f(y)` |
| `(declare-fun g (Bool) Bool)`, used as `(g p)` | `Application` | **EUF** | same, and note `p` is now an *argument*, so it is congruence-relevant here even though it is not on its own |
| `(= p q)` over Bool | `Equality` | today: EUF, via `has_equality` | this is the **second over-trigger**; a fix to `has_euf_predicate` alone leaves it |
| `(= a b)` over an uninterpreted sort | `Equality` | **EUF** | genuine congruence content |
| `(= x y)` over Real | `Equality` | LRA, unless it contains an application | already handled correctly |

**The trap that matters most, and it is on the Mechanism B side.** The truth
value of a Bool-sorted term is represented in the EUF engine by *merging it
with `true` or `false`* -- that is why `PreparedFlatTheory` is constructed with
`true_term` and `false_term`, and why the terms count came to
`checks x (atoms + 2)`. So the true/false machinery is **load-bearing for
genuine predicates**: `(f x)` still needs to be merged with `true`/`false` to
have a truth value at all.

The fix must therefore remove *propositional variables* from the EUF atom set
**without** removing the true/false handling that Bool-sorted applications
depend on. "Stop merging Booleans with true/false" would be a
correctness bug, not an optimisation. Concretely, the discriminator is
**arity**, not sort: a Bool-sorted term with zero arguments carries no
congruence obligation, and one with arguments does.

**Minimum regression tests to add alongside the fix**, each asserting both the
answer and the dispatch/classification counter:

1. Pure QF_LRA plus propositional variables -- must stay on `:solver.qf-lra`.
2. The same plus one `(declare-fun f (Real) Bool)` application -- must go mixed.
3. Pure QF_LRA plus `(= p q)` over Bool -- states the current behaviour, and is
   the test that changes if the second over-trigger is also fixed.
4. A Bool-sorted predicate whose truth value is forced both ways, checking the
   true/false merge still happens (guards against over-pruning the EUF atoms).
5. Uninterpreted-sort equalities with arithmetic -- must stay mixed
   (the unsoundness direction).
6. `true`/`false` appearing literally in an otherwise pure QF_LRA formula --
   must not flip the dispatch.

### What not to do

- **Do not "fix" this by disabling NO on single-theory problems.** NO is
  already inert here (`:no.interface.conflicts 0`). That would treat the
  symptom's neighbour and leave the EUF engine running.
- **Do not declare the regression fixed when A and B are done.** They account
  for 12 of 61 lost files. [](#mechanism-unknown) is the larger half and is
  independent.
- **Do not stop at A and B because the regression numbers recover.**
  [](#mechanism-rebuild) recovers no lost file on this corpus and is still the
  defect most likely to bite genuine QF_UFLRA work, because it is the only one
  that survives a correct classifier.
- **Do not benchmark a fix against `b6b8d55`.** That is a pre-regression
  commit, not the merge base. Compare against the commit the fix is built on;
  this note exists partly because that mistake was made once already and made a
  main-side loss look branch-side.
- **Do not merge a fix on one instance's evidence.** Both mechanisms here were
  found on single files and confirmed on the corpus afterwards. The corpus step
  is what turned "1588 vs 1631, we regressed" into "+16 / -0 against the merge
  base".

## Measurement traps hit while producing this note

Recorded because each cost real time and each would recur.

- **Task density on the cluster.** The first division run put 67 tasks on one
  56-core node alongside another user's array, giving a 1.31x median slowdown
  that looked like a code regression. Re-running spread over three nodes with
  `--cpus-per-task=3` moved the result by 4 files -- so contention was *not*
  the cause, but the run was uninterpretable until it was excluded. Match task
  density to the baseline run.
- **Bisecting across a branch merge.** Main contains merge commits from the
  Nelson-Oppen branch whose own history predates QF_LRA support; those commits
  answer `unsupported logic: QF_LRA`. A bisect script that treats "no answer"
  as *bad* converges on nonsense -- twice, here, both times onto a docs-only
  commit. Use `git bisect --first-parent` **and** exit 125 on unsupported /
  no-answer.
- **Reading the version banner as the answer.** Without `--quiet` the first
  line of output is `primo git <sha> ...`, not `sat`. A test script using
  `head -1` marks every commit bad.
- **Comparing against the wrong baseline.** See "what not to do" above.
