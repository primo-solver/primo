# Wrong answer: congruence is dropped for Bool-sorted function arguments

**Severity: wrong answer.** Found 2026-08-07 while checking a proposed change
to the theory dispatch. This is not a performance issue and not the
Nelson-Oppen regression in [](nelson-oppen-qf-lra-regression.md); it is
long-standing and independent.

## Reproducer

```lisp
(set-logic QF_UF)
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun f (Bool) Bool)
(assert (= p q))
(assert (not (= (f p) (f q))))
(check-sat)
```

Congruence gives `p = q` implies `f(p) = f(q)`, so this is **unsat**.

| solver | answer |
|---|---|
| z3 | `unsat` |
| Primo `0c0843d` (main) | **`sat`** |
| Primo `b6b8d55` (pre-NO-merge) | **`sat`** |

Deterministic across repeated runs. Present before and after the
Nelson-Oppen merge, so it is not a regression from that work.

## What it is not

- **Not an EUF bug.** Congruence over Bool arguments works correctly when the
  EUF solver actually runs. Adding an uninterpreted sort to the same formula,
  which forces the QF_UF path, gives the right answer:

  ```lisp
  (declare-sort S 0) (declare-fun z () S) (declare-fun g (S) S)
  (assert (= (g z) z))          ; forces :solver.qf-uf
  (assert (= p q))
  (assert (not (= (f p) (f q))))
  ```
  → Primo `unsat`, matching z3.

- **Not out of scope.** [](architecture.md) states that Bool arguments to
  uninterpreted functions are supported: *"SMT-LIB `Bool` is a first-class
  sort, so QF_UF inputs may pass Boolean formulas as arguments to
  uninterpreted functions."*

- **Not caused or fixed by the proposed dispatch narrowing** in
  [](nelson-oppen-qf-lra-regression.md). That change touches
  `has_euf_predicate`, which -- see below -- is not consulted on this path at
  all.

## Cause: two behaviours compose

**1. Bool-sorted equalities are rewritten before the classifier runs.**
`summarize_formula_features` computes `has_equality` from `Equality` nodes, but
preprocessing rewrites Bool-sorted equalities into Boolean structure first. So
for a formula whose equalities are *all* over Bool -- `(= p q)` and
`(= (f p) (f q))` here -- **`has_equality` is false**.

**2. The QF_UF dispatch keys only on `has_equality`.**

```cpp
if (has_equality) {
  return finish_solve(solve_qf_uf(...));
}
try {
  return finish_solve(solve_boolean_conjunction(...));   // no congruence
} catch (const UnsupportedInput&) { ... solve_qf_uf ... }
```

`has_euf_predicate` is computed and used for the *mixed* EUF/LRA dispatch, but
it is **not consulted here**. So a formula containing a genuine uninterpreted
function is handed to `solve_boolean_conjunction`, which abstracts `(f p)` and
`(f q)` to independent propositional variables and has no congruence rule to
relate them. `p = q` becomes an iff between two SAT literals, which does not
constrain the two abstracted applications at all -- hence `sat`.

Note the direction of the failure: the classifier is **too narrow** here,
which is exactly the unsoundness direction flagged in
[](nelson-oppen-qf-lra-regression.md)'s fix-ordering table. It is already
happening, before any change.

## Why the fuzzers did not catch it, and what was added

The shape has **no coverage in any of the three fuzzing entry points**, which
is why 6,000 gated cases missed a wrong answer.

| entry point | why it cannot produce the shape |
|---|---|
| **FuzzSMT** (generator) | driven by its parameters; the QF_UF/QF_LRA invocations never request Bool-sorted function arguments |
| **YinYang opfuzz/typefuzz** (mutation) | can only mutate what the seeds contain -- and **0 of 200** sampled SMT-LIB QF_UF files declare a function with a `Bool` argument, so the corpus never had one |
| **`qf_uflra_smoke_fuzz.py`** (generator) | emits Bool-*returning* functions (`p (Real) Bool`) but never Bool-*argument* ones |

That the SMT-LIB division itself contains none of these is the root cause: a
mutation corpus built from real benchmarks inherits the corpus's blind spots,
and this is a second instance of the failure
[](fuzz-coverage-qf-lra.md) already records for seed *families*.

### Seeds added

Four seeds in `benchmarks/seeds/qf_uf/`, which both the QF_UF gate and
`yinyang_fuzz.sh` read **by default**, so no gate configuration changes:

| seed | shape |
|---|---|
| `seed_bool_argument_predicate.smt2` | unary and binary Bool-argument predicates, satisfiable |
| `seed_bool_argument_compound.smt2` | *compound* Boolean arguments (`(f (and p q))`), exercising the fresh-constant normalization |
| `seed_bool_argument_mixed_sort.smt2` | Bool arguments alongside an uninterpreted sort, congruence across both |
| `seed_bool_argument_congruence_unsat.smt2` | **unsat purely by Bool-argument congruence** |

The last one is the important one, and the reason it exists is worth stating:
**dropping congruence can only ever produce a spurious `sat`**, so a
satisfiable seed cannot expose this class of bug. Verified -- the pre-fix
binary answers `sat` on it against z3's `unsat`, and the post-fix binary
answers `unsat`. The other three agree with z3 both before and after, which
is exactly why they were not sufficient on their own.

All four are well-sorted (z3 parses and solves each) and far below the
`typefuzz -L` seed-size limit that [](fuzz-coverage-qf-lra.md) records as
having silently dropped 12 seeds once already.

**Still open:** `qf_uflra_smoke_fuzz.py` needs a template with a Bool-argument
function so the *mixed* path gets the same coverage, and FuzzSMT's parameters
should be checked for whether Bool-sorted arguments can be requested at all.

(gate-result)=
## Gate result, and the control run that makes it mean something

**Job 120192** (Slurm, `scripts/slurm_qfuf_fuzz_gate.sbatch`, array 0-8) on the
fixed binary: **3,000 FuzzSMT cases, 3,000 PASS, 0 wrong**, and all three
mutation tasks reporting *7 seeds, 7 valid, 0 invalid, 0 bug triggers*. The
"7 valid" matters -- it confirms the four new seeds were accepted rather than
silently dropped by the `typefuzz -L` limit. Six `summary.csv` files were
counted against six shards, per this repository's own rule that a shard killed
by the wall clock vanishes and leaves the gate looking clean.

**A clean gate on fixed code shows the fix broke nothing. It does not show the
new coverage can catch anything.** So the identical mutation tasks, same seeds,
same iteration counts, were re-run against the **pre-fix** binary (job 120201):

| task | fixed binary | pre-fix binary |
|---|---:|---:|
| opfuzz | 0 bug triggers | **3** |
| typefuzz | 0 | **1** |
| typefuzz | 0 | **3** |

Seven `incorrect-*` cases, and every one derives from a newly added
Bool-argument seed:

| originating seed | bug cases |
|---|---:|
| `seed_bool_argument_compound` | 3 |
| `seed_bool_argument_congruence_unsat` | 2 |
| `seed_bool_argument_predicate` | 2 |
| **the three pre-existing QF_UF seeds** | **0** |

A representative mutant:

```lisp
(assert (f (and p q)))
(assert (and (not (f (or q p))) (f (or p q))))
```

`(or q p)` and `(or p q)` are the same term, so this is unsat; the pre-fix
binary answered `sat`.

So the corpus that existed before **provably could not** catch this, and the
one that exists now does. Worth noting for future seed design: the satisfiable
`seed_bool_argument_predicate` yielded 2 bug cases *once mutated*, so sat seeds
are not useless here even though a sat seed cannot expose this bug class
unmutated.

## Fix direction, and the trap in it

The dispatch must send a formula containing uninterpreted function
applications to `solve_qf_uf` regardless of whether any surviving `Equality`
node remains. `has_application_term` is already computed and is the more
robust test than `has_euf_predicate`, since it does not depend on the
application's sort.

**The trap:** this widens what reaches `solve_qf_uf`, so formulas that are
currently answered by the Boolean solver would move. That is the *sound*
direction, but it is still a dispatch change, with the same
benchmark-comparability hazard: files change solver, not speed. Measure
gained/lost, not PAR2 alone.

**Falsifier:** the reproducer above must answer `unsat`, and the
uninterpreted-sort variant must keep answering `unsat`.

**Regression tests to add:**

1. The reproducer -- `unsat`.
2. `f : Bool -> Bool` with `p`, `q` *not* asserted equal -- `sat`, so the fix
   is not simply forcing unsat.
3. A Bool-argument congruence chain through two applications.
4. A purely propositional formula with no uninterpreted functions -- must
   still reach `solve_boolean_conjunction`, so the widening is not total.
5. `(f (and p q))` -- a *compound* Boolean argument, which
   [](architecture.md) says the frontend normalizes with a fresh constant and
   a propositional equivalence; that path needs its own coverage.

## The same area produced a second wrong answer (2026-08-26)

The fix above landed with five regression tests and a seed corpus. Nineteen
days later the same shape produced another wrong answer:
`bugs/qf-uf-boolean-argument-congruence/README.md`. It is not a
regression of this fix -- forward congruence still works -- but the
**converse**:

```lisp
(declare-sort U 0)
(declare-fun tb (Bool) U)
(assert (= (tb true) (tb false)))     ; tb is constant on Bool
(assert (not (= (tb p) (tb q))))      ; so this cannot hold
```

`tb` sends both elements of `Bool` to the same value, so `tb(p) = tb(q)` for
every `p` and `q`. Deciding that needs the *argument* terms to be known equal
to one of the two constants, which is `|Bool| = 2` reasoning rather than
congruence. Primo answers `sat`.

**Why the tests written for the first bug could not catch the second.** Every
one of them asserts an argument equality and checks that congruence carries it
forward. Look at the list above: "the reproducer", "a Bool-argument congruence
chain", "a compound Boolean argument". Each encodes the shape that was being
fixed. None expresses an argument equality the solver has to *derive*, and the
second bug lives exactly there. The QF_UFLRA seed corpus has the same
property, plus a second filter -- it is all mixed-path.

That is the general trap, and it is worth stating plainly because this project
now has two instances of it: **regression tests written after a fix encode the
shape that was fixed.** They defend the boundary that was already found, and
say nothing about the rest of the surface.

## The harness that answers it

`scripts/qf_uf_boolean_fuzz.py` generates pure-QF_UF cases -- no arithmetic
anywhere, so only the pure path can be exercised -- across the capacities
listed in [](euf-boolean-arguments.md). It compares answers against z3 and,
with `--validate-models`, retains every case and requires Dolmen to accept each
satisfiable model. It exits nonzero on any disagreement, crash, unsupported
input, or model failure. `scripts/slurm_qf_uf_boolean_fuzz_gate.sbatch` enables
model validation and runs one shard per shape, so a failure names the capacity
rather than only a case number.

First run, 600 cases, 2026-08-26: **91 disagreements**, concentrated in
`converse` (65), `mixed-sort` (13), `argument` (12) and `two-valued` (1), with
`result`, `nested` and `compound` clean. All of them reduce to the same
missing fact. The gate therefore **fails on current main by design**; once the
open bug is fixed it becomes a clean gate and any later failure is a real
regression.

`benchmarks/seeds/qf_uf/` gained the distilled shapes: the two-assertion
minimal witness, the converse witness, a pigeonhole case, and -- deliberately
-- two shapes that already pass, so a future fix is measured against what must
not regress rather than only against what currently fails.

## Fixed 2026-08-26 (`fa44295`, corrected in `d8e8595`)

The mechanism, the correctness argument for it, and the invariant the
pure-theory filter has to respect are written up in
[](euf-boolean-arguments.md) -- this section records the history and the
measurements.

`register_boolean_argument_terms()` registers Bool-sorted arguments of
uninterpreted applications as theory atoms; the existing per-atom handling
supplies `t = true/false` to the closure with a one-literal reason. Both paths
now share `apply_boolean_domain_encoding`, so the QF_UF and mixed encodings
cannot diverge a third time.

Measured, not assumed:

| check | result |
| --- | --- |
| Bool harness, 600 cases | 91 disagreements -> **0** |
| SLURM gate, 8 shapes x 1,000 | **8,000 PASS** |
| QF_UF division, pre-fix vs fixed | **0 answer changes** (7,496 -> 7,497 answered) |
| QF_UF, registration vs bridge-atoms | 0 disagreements; registration **3% faster** on the 881 files both solve in >1s |
| QF_UFLRA 1,284 at 120s | 1,272 both arms, identical sat/unsat, **0 disagreements** |

One claim in the seed corpus is wrong and worth correcting here: it says no
SMT-LIB QF_UF file uses Bool-sorted arguments. Sampling 289 division files
found **five** that do -- four `2018-Goel-hwbench` and one `20190906-CLEARSY`.
None of them changed answer, so the hole was reachable but unexercised; the
point is that "no benchmark does this" was an assumption, and the pre-fix
baseline run is what actually established the fix is answer-neutral.

**Reference solvers on the same shapes.** cvc5 1.3.4 and Yices 2.7.0 agree with
z3 on 1,900 generated QF_UF cases and 600 mixed QF_UFLRA cases each. OpenSMT
2.9.2 is clean on pure QF_UF but wrong on **58 of 600** mixed cases,
reproducing the OpenSMT bug report in
`bugs/opensmt-qf-uflra-boolean-disagreement/README.md` at
scale. Two of the five solvers we compare against have had a bug in this exact
corner.
