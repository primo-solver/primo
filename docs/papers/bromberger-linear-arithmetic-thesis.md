# Decision Procedures for Linear Arithmetic (Bromberger's thesis)

## Citation

Martin Bromberger, *Decision Procedures for Linear Arithmetic*. Dissertation
zur Erlangung des Grades des Doktors der Naturwissenschaften, Fakultät
Mathematik und Informatik, Universität des Saarlandes, Saarbrücken, July 2019;
colloquium 2019-12-10. Reviewers: Pascal Fontaine, Alberto Griggio, Thomas
Sturm, Christoph Weidenbach. 342 pages.

HAL id `tel-02427371`, <https://inria.hal.science/tel-02427371v1> (link checked
2026-08-29, HTTP 200).

Local copy: `docs/papers/Bromberger_Decision_Procedures_for_Linear_Arithmetic.pdf`
-- **untracked**, per the standing practice that reference papers are read
locally and never committed (`.gitignore` excludes `*.pdf`). This note is
original prose written for Primo, not a reproduction of the thesis.

**Read 2026-08-29**: §1.3--1.5 (unboundedness, contributions); Chapter 4
(§4.2--4.6) in full; Chapter 5 (§5.2--5.7) in full including the `EqBasis`,
`FindTBnds`, `Initialize` and `FixEqs` algorithms and the Nelson-Oppen
justification clauses; Chapter 6's introduction, §6.1, Definitions 6.2.1--6.2.3,
§6.3's split-system reduction, and §6.5's experiments; Chapter 7 (§7.1.1--7.1.4,
§7.2.3--7.2.5) in full; Chapter 8 §8.2.2--8.2.4 and every ablation legend in
Chapters 7--8. **Not read**: Chapter 2 (preliminaries) beyond what the above
sections quote, Chapter 3 (CutSat++) beyond its summary in §1.5 and §6.1, and
the detailed Mixed-Echelon-Hermite algorithms in §6.2/§6.4 (Figures 6.4--6.10).

## Why this note exists

[](lra-related-work.md) has carried Bromberger--Weidenbach entries since
2026-07-31 *scoped from abstracts only*, split across three separate rows
("Computing a Complete Basis for Equalities", "Fast Cube Tests for LIA", "New
Techniques for Linear Arithmetic"), with the cube-test line filed under **out of
scope for QF_LRA**. The thesis is the superset of all three plus two things no
individual paper carries: the bounding transformations of Chapter 6, and --
more valuable than any single algorithm here -- **Chapters 7 and 8, which are a
measured ablation of an entire QF_LIA/QF_LRA solver on the same SMT-LIB
benchmark families Primo runs.**

That last point is why this note is longer than the algorithm content would
justify. Primo's arithmetic work is organised around [](../open-leads.md), and
every entry there states an expected size that is usually *unknown*. Chapters 7
and 8 supply measured sizes for eight mechanisms, several of which Primo has
already implemented, several of which it has deferred, and one of which it has
never considered and which is by a wide margin the largest number in the
document.

## Chapter map

| ch. | subject | value to Primo |
|---|---|---|
| 2 | preliminaries; Dutertre--de Moura simplex restated | none -- Primo already works from [](fast-linear-arithmetic-dpllt.md) |
| 3 | CutSat++ | **none directly.** Bromberger's own §6.1 rejects it as too complicated and too inflexible, and records that no state-of-the-art solver adopted it |
| 4 | largest cube test / unit cube test | corrects lead [12c](../open-leads.md)'s stated blocker |
| 5 | complete basis for implied equalities | the completeness mechanism [](../qf-lra-equality-sharing.md) currently reaches by exact per-pair probes |
| 6 | Double-Bounded reduction + Mixed-Echelon-Hermite | branch-and-bound divergence; +169 solved |
| **7** | **SPASS-IQ implementation** | **row representation, pivoting, backtracking -- with ablations** |
| **8** | **SPASS-SATT: CDCL(LA) + preprocessing** | **+1693 solved on QF_LIA from preprocessing alone** |
| 9 | conclusion, SMT-COMP 2018 results | SPASS-SATT ranked 1st QF_LIA, 2nd QF_LRA |

## The corpora coincide, which is what makes the numbers usable

Chapters 7--8 evaluate on the SMT-LIB QF_LIA and QF_LRA divisions with a
40-minute limit. Primo's `sandbox/non-incremental/QF_LIA` tree is the same
division: `20180326-Bromberger` (806 files here, 806 in the thesis), `nec-smt`
(2780 / "2800"), `CAV_2009_benchmarks` (591 / 591), `dillig` (233 / 233),
`slacks` (233 / 233), `rings` (294), `rings_preprocessed` (294), `convert`
(319 / 319), `prime-cone` (37 / 37), `cut_lemmas` (93 / 93), `arctic-matrix`
(100 / 100), `tropical-matrix` (108 / 107), `calypto` (37 / 37),
`miplib2003` (16 / 16), `tightrhombus` (22).

The thesis's family-level counts are therefore directly comparable to Primo's,
which is unusual and worth exploiting. Note the two caveats: SPASS-SATT's
totals (6947 QF_LIA, 1649 QF_LRA) are the 2018/2019 division, smaller than the
13,306 files Primo now runs, and its 40-minute limit is far above Primo's 60s
benchmark timeout, so a "+N solved" here is an upper bound on what the same
mechanism buys at Primo's timeout, not a prediction.

## Chapter 4 -- the cube tests, and what actually blocks them

### The linear cube transformation (Cor. 4.2.2)

For a cube `C^e(z)` of edge length `e` centred at `z`, and a polyhedron
`Ax <= b`: the cube fits inside the polyhedron **iff** `Az <= b'` where
`b'_i = b_i - (e/2)·||a_i||_1`. The proof is one line of vertex arithmetic --
the maximiser of `a_i^T x` over the cube is the vertex
`z + (e/2)·(sgn(a_i1), ..., sgn(a_in))^T`, whose objective value is
`a_i^T z + (e/2)·||a_i||_1`.

The consequence that matters: because `b'` depends **only on `e`**, the
solutions of `Ax <= b'` are *exactly the centres of all edge-`e` cubes fitting
in `Ax <= b`*. So one rational feasibility check on a uniformly tightened
system answers a question about integer points.

**Unit cube test** (§4.3.2): fix `e = 1`, solve `Ax <= b - (1/2)||A||_1` over
the rationals. Any solution `z` rounds to an integer solution of the original,
because a unit cube always contains a closest integer to its centre. In
Dutertre--de Moura form this is *only a change of row bounds* -- no new rows,
no new columns, and the original system is recovered by reverting the bounds.
Bromberger stresses that this preserves incrementality exactly.

**Largest cube test** (§4.3.1): maximise `x_e` subject to
`Ax + a' (x_e/2) <= b` with `a'_i = ||a_i||_1` and `x_e >= 0`. Needs an
optimisation-capable simplex, which Dutertre--de Moura-style SMT solvers do not
have; Bromberger notes the extra column can be installed from the start at
negligible cost, since `x_e` only ever shrinks the search space and one pivot
sets it to 0.

**Mixed arithmetic** (§4.3.3): use a *flat* cube that extends only along
integer variables. Only the tightening changes: `b'_i = b_i - (e/2)·||s_i||_1`
where `s_i` is the integer-variable part of row `i`.

### The precondition -- this is the correction to lead 12c

[](../open-leads.md) lead 12c currently records cubes as *"deferred behind a
prerequisite ... it needs z3's difference/octagon twist to be worth building"*,
on the 2026-08-21 measurement that 249 of 300 sampled instances contain equality
atoms and an asserted equality fixes a slack, making the tightened system
contradictory.

**The measurement is right and the diagnosis is confirmed by the thesis --
but the prescribed remedy is not the one the primary source gives.**
Bromberger states the failure condition directly, twice:

> "The unit cube test is also guaranteed to fail on problems containing boolean
> variables, i.e., variables that are either 0 or 1 ... Whenever the problem
> contains a boolean variable, it is beneficial to skip the unit cube test."
> (§4.5)

and, in SPASS-IQ's actual waterfall (§7.2.4):

> "We do, however, skip the unit cube test if one of the original integer
> variables has bounds `U(x_i) - L(x_i) = 1` or if one of the slacked variables
> has bounds `U(x_i) - L(x_i) = 0`. ... If some of the original integer
> variables are fixed, i.e. `U(x_i) - L(x_i) = 0`, then we treat them as if
> they were replaced by their constant values. This avoids another case where
> the unit cube test would otherwise automatically fail."

So the fixed-slack case Primo measured is a *known, named, cheaply detected
skip condition*, not a defect to be engineered around. Two things follow.

1. **The octagon twist is not the prerequisite.** It is z3's refinement for
   difference and octagon rows; it does nothing about a fixed slack. The
   thesis's own prerequisite for cubes in the presence of equalities is
   **Chapter 5**: §5.7 lists as application (i) "we can use the equality basis
   to eliminate all equalities from `Ax <= b`. It is, therefore, useful as a
   preprocessing step for our cube tests." Chapter 5 exists *because* equalities
   break Chapter 4 -- §5.2 opens with exactly that sentence.
2. **The cheap version is a guard, not a twist.** Detect the skip condition and
   do not run the test. That is what makes the measured overhead "minor, almost
   immeasurable" in Figure 7.27.

### Size, measured

- Full QF_LIA division, unit cube test on vs. off: **6806 vs. 6750 (+56)**
  (Fig. 7.27). Small, because most absolutely unbounded SMT-LIB instances are
  small enough that ordinary bound propagation and simple rounding already
  handle them.
- The population is not small: **over 1483 QF_LIA instances are absolutely
  unbounded** -- 229/233 `dillig`, 503/591 `CAV_2009_benchmarks`, 229/233
  `slacks`, 503/806 `20180326-Bromberger`, 19/37 `prime-cone` (§7.2.5 fn. 19).
- On the absolutely unbounded instances specifically (Fig. 4.8), SPASS-IQ+uc
  solves 503/503 CAV-2009 in 7.8s total against SPASS-IQ's 502 in 156s, and
  MathSAT5 and Z3 both gained the same way after adopting the test.
- The mechanism also removes the need for branch-and-bound at all: **641 of
  6947** instances need branch-and-bound with the unit cube test, **1912
  without** it (§7.2.5).

## Chapter 5 -- the complete equality basis

### The detector: strict-cube (Lemma 5.2.2)

`Ax <= b` contains a cube of some positive edge length **iff** `Ax <= b^δ` is
satisfiable, where `b^δ` is `b` with every non-strict bound made strict. And
(Lemma 5.2.1) a polyhedron implies an equality `h^T x = g` with `h ≠ 0` **iff**
it contains no cube of positive edge length.

Together: **make every bound strict and run one feasibility check. Satisfiable
means the system implies no equality at all.** No coefficient arithmetic, no
per-pair probing.

### The extractor: equality explanation (Lemma 5.2.3)

If `Ax <= b` is satisfiable but `Ax <= b^δ` is not, then for a minimal
unsatisfiable set `C` of the strict system, **every** `a_i^T x <= b_i^δ` in `C`
gives an implied equality `a_i^T x = b_i` of the original. So the simplex
conflict explanation *is* the equality set -- one infeasibility yields a whole
row of tight variables, not one candidate.

### The tableau form (§5.3.1)

Recast in Dutertre--de Moura terms, an equality basis is exactly *the set of
tightly bounded variables* (`β(x_j)` equals `L(x_j)` or `U(x_j)` in every
satisfiable assignment). `FindTBnds()` finds them all:

1. `Check()`. Unsatisfiable ⇒ return false (the system implies everything).
2. `Initialize()` on the satisfying `β`: drop every bound not tight under `β`
   (`L := -∞` / `U := +∞`), drop every originally strict bound, **pivot as many
   `L = U` variables as possible out of the basis**, then make all remaining
   `L ≠ U` bounds strict.
3. Loop: while `Check()` returns a conflicting basic `y_i`, call `FixEqs(y_i)`,
   which turns every non-basic `z_j` in that row into an explicit equality by
   setting `L(z_j) := U(z_j)` at the appropriate original bound.
4. Restore original bounds for every variable still with `L ≠ U`.

**Incrementality (§5.3.2)** is the load-bearing property: `Check()` never pivots
a non-basic `z_j` with `L(z_j) = U(z_j)` back into the basis, so tight
variables found by an earlier `FindTBnds()` stay non-basic and are never
recomputed. Asserting more bounds can only add tight variables.

### Nelson-Oppen (§5.4) -- the part that bears on Primo directly

> "Linear arithmetic theory solvers sometimes *guess* these equations based on
> one satisfying assignment. Then the equations are transferred according to the
> Nelson-Oppen method without verification. This leads to a backtrack of the
> combination procedure in case the guess was wrong ... With the availability
> of an equality basis, the guesses can be verified directly and efficiently."

The procedure: run `FindTBnds()`, then normalise every variable through the
substitution the tableau already encodes -- a tight non-basic `z_j` normalises
to its constant `L(z_j)`, a non-tight non-basic to itself, a basic `y_i` to its
row with the tight non-basics folded into a constant. **Two variables are
entailed-equal iff their normalised terms are syntactically identical**, so a
hash of normal forms replaces pairwise entailment probing entirely.

The justification clauses are given explicitly (§5.4.2) and are minimal
(removing any literal destroys the tautology). For two basic `y_i`, `y_k`:

```text
( ⋁_{z_j ∈ N, L(z_j)=U(z_j), a_ij ≠ a_kj}  z_j < L(z_j) ∨ z_j > U(z_j) )  ∨  y_i = y_k
```

with the mixed basic/non-basic case obtained by treating `z_k` as `z_k = 1·z_k`.
Note the coefficient-difference filter: a tight bound with `a_ij = a_kj` is
**redundant** in the justification and must be dropped, which is what makes
these clauses minimal.

### How this lands against Primo

[](../qf-lra-equality-sharing.md) documents `entailed_equalities()` (`FixedEq`,
`EqRow`, `EqOffsetRows`, `EqRows`) as deliberately incomplete, with completeness
reached at quiescence by a model-filtered exact fallback that tests each
unresolved shared pair in **both strict orientations** against a reused
`PreparedLraTheory`. Bromberger's construction reaches the same completeness
with one strict-system `Check()` per fixpoint round plus hashing, and it is
incremental across rounds by construction.

The gap that note pins -- `s = x - y` fixed at 0 entails `x ≃ y` but neither
`FixedEq` nor `EqRow` sees it, and `--lra-fixed-elimination` closes it by
pivoting the fixed slack out of the basis -- is **precisely `Initialize()`'s
step 2**, arrived at independently. That is a good sign for the transfer: the
mechanism Primo measured its way to is a named step of the published algorithm.

**Do not overstate what this buys.** §5.6's closing caveat: the method finds
only equalities implied over the *rationals*. Integer/mixed-entailed equalities
that are not rationally implied are outside it. That is fine for QF_UFLRA and
is a real limitation for any integer extension.

## Chapter 6 -- bounding transformations

The framing (§1.3, §2.8, §6.1) classifies problems as *guarded*, *unguarded but
bounded*, *absolutely unbounded*, and *partially unbounded*. Branch-and-bound
terminates on the first two; the cube tests dispose of the third; **partially
unbounded is the case where branch-and-bound diverges**, and Chapter 6 exists
for it alone.

Two transformations, applied only to partially unbounded problems:

- **Double-Bounded reduction (§6.3).** Split the system into an absolutely
  unbounded part `Ax <= b` and a double-bounded part `l <= Dx <= u`
  (Def. 6.3.1). Lemma 6.3.2 shows the split always exists and that the bounded
  part is self-contained. The unbounded part is then simply *dropped*: it can
  shrink the solution set but never empties it, so the whole system is
  equisatisfiable with `l <= Dx <= u` alone.
- **Mixed-Echelon-Hermite transformation (§6.2, from Christ and Hoenicke).**
  Shift variables so `D` becomes lower triangular with gaps; Lemma 6.2.3 then
  gives that every variable is either bounded or absent from the system.
  Bounded system ⇒ branch-and-bound terminates.

**Which inequalities are bounded is decided by Chapter 5, not by optimisation.**
Lemma 5.5.1: `h` is bounded in `Ax <= b` iff `Ax <= 0^m` implies `h^T x = 0`. So
compute an equality basis of the *homogeneous* system and read the bounded basis
off it -- one equality-basis computation instead of `m` linear programs. This is
the second place in the thesis where Chapter 5 turns out to be the enabling
machinery rather than a standalone result.

**Size, measured**: bounding transformations on vs. off, full QF_LIA division:
**6806 vs. 6637 (+169)**, of which **162 are in `20180326-Bromberger`** (which
contains 303 partially unbounded problems), plus 1 `arctic-matrix`, 3
`cut_lemmas`, 1 `slacks`, 2 `tropical-matrix` (§7.2.5 fn. 20). Overhead where
not applicable is described as almost immeasurable, because the applicability
test is the cheap bounded-basis check.

Independent evidence that this is the right target for that family: §6.5 reports
that on `20-14.slacks.smt2` "all other solvers seem to diverge", and that of the
fifteen originally partially unbounded SMT-LIB instances only SPASS-IQ,
Ctrl-Ergo and Yices solved all fifteen.

## Chapter 7 -- SPASS-IQ's implementation, and its ablations

### 7.1.4 Data structures -- a third design point for the row representation

[](../open-leads.md) lead [7](lead-row-representation) and
[](king-thesis) treat this as a two-way fork: King's *unsorted*
doubly linked row/column lists with a dense scratch map, versus Primo's
*sorted contiguous* `vector<pair>` with a two-way merge. SPASS-IQ is a third
point that takes both halves:

| | King | Primo today | **SPASS-IQ** |
|---|---|---|---|
| row storage | doubly linked, **unsorted** | sorted `vector<pair>` | doubly linked, **sorted** |
| column storage | doubly linked | `occurs_in_` index | doubly linked, **explicitly unsorted** |
| row addition | dense O(n) scratch map | two-way merge | **two-way merge** (Fig. 7.17) |
| entry removal | O(1) unlink, pointer in hand | O(1) via cross-links (lead 3b) | O(1) unlink |
| coefficients | rationals | rationals | **integers + one row denominator** |

Three design decisions are stated as deliberate and each has a reason:

1. **Row lists are sorted and kept sorted as an invariant**, by non-basic
   variable id, "because the row ordering allows us to efficiently identify
   which columns are shared by two rows". `AddMultipleOfRowToRow()` is the merge
   step of merge sort, linear in the non-zeros of both rows, and keeps both
   sorted.
2. **Column lists are forbidden any order invariant**, explicitly so that adding
   an entry to a column is O(1) at the tail.
3. **Coefficients are integers with one common denominator per row**, stored in
   `Tableau.rowCoeffs[]`. This is the detail worth taking seriously: a row of
   `L` non-zeros costs `1 + L` big integers instead of `2L`, "almost reduced the
   number of integer operands by half", and it removes rational normalisation
   from the inner loop of every row addition. Credited also to veriT.

Points 1 and 2 together are the argument Primo's `occurs_in_` work
([lead 3b](lead-occurs-in)) arrived at empirically. Point 3 is
**not** something Primo has considered anywhere in [](../qf-lra-heuristics.md),
and it is orthogonal to the representation question.

`VarInfo` (Fig. 7.18) is a second, cheaper idea: redundant per-variable counters
(`numEntries`, `numLow`, `numUpp`, `numTightLow`, `numTightUpp`, a bitfield of
`hasLower`/`hasUpper`/`isTightAtLower`/`isTightAtUpper`/`isViolating`,
the current violation `difference` and `direction`, and the heap position)
maintained incrementally. It buys **O(1) conflict detection** (Fig. 7.19:
`numTightLow == numEntries`) where the naive test is linear in the row.

### 7.1.1 Pivoting -- six strategies, and the result is "it doesn't matter"

Bland, CVC4's rule, SMTInterpol's rule, maximum error, "prefer unbounded", and
SPASS-SATT's own rule are all implemented and measured (Figs. 7.1--7.4). Two
findings:

- Bland alone is much worse than everything else, and **every strategy that does
  not switch to Bland after a finite number of pivots is worse than its
  counterpart that does**. The switch point used is the number of non-basic
  variables.
- Among the greedy rules, "the difference between the best strategies and their
  runner-ups is so small that they could be attributed to performance
  fluctuations of our cluster."

This is directly relevant to [lead 3](lead-leaving-rule)
(MathSAT's least-violated leaving rule, implemented opt-in, 167 vs. 174 on the
promotion bar). The thesis's independent six-way comparison says that outcome is
the expected one: **leaving-rule choice is within noise, and the greedy→Bland
switch is the part that carries the result.** Worth checking that Primo has such
a switch before spending anything further on rule selection.

### 7.1.3 Backtracking through recalculation -- an untried, cheap lead

Dutertre--de Moura's `Backtrack()` reverts bounds *and* restores the backed-up
assignment. `HardBacktrack()` reverts only the bounds and recomputes the
assignment with `Check()`. It is strictly more expensive per call, but:

> "a good continuation point for the simplex algorithm depends not only on the
> current assignment, but also on the current tableau. Recomputing the solution
> with `Check()` keeps the assignment and the tableau in sync ... Reverting to
> the backup solution does the opposite and sometimes disconnects the assignment
> and the tableau."

Measured (Figs. 7.10--7.12): QF_LRA **1607 vs. 1602**; QF_LIA **6806 vs. 6683
(+123)**; and on `convert` alone **319 vs. 202 (+117)**. CVC4 and veriT are said
to do the same.

`convert` is a family Primo runs (319 files) and one where patching already
gained 57 solves.

### 7.1.2 / 7.2.5 Things measured to be worth little

Recorded so they are not proposed as leads:

- **Greedy conflict detection** (prefer a violated variable that is already a
  conflict): 1607 vs. 1604 (LRA), 6806 vs. 6798 (LIA).
- **Returning multiple conflicts per check**: 1607 vs. 1605, 6806 vs. 6797.
  Both described as within cluster noise.
- **Simple rounding**: on QF_LIA, "no impact ... with the exception of one
  single problem".
- **Bound propagation threshold**: any non-zero threshold is equivalent
  (0/1/10/100/1000/∞ all within noise). But with the bounding transformations
  turned off, an unbounded threshold "might even diverge on some instances".
- **FLINT vs. GMP**: 6806 vs. 6792 (LIA), 1607 vs. 1604 (LRA). FLINT wins by
  switching dynamically to hardware types for small numbers. [](../qf-lia-profile.md)
  already measured that 98.3% of `nec-smt` numerator/denominator values fit in
  4 bits, which is exactly the regime this addresses.
- **Unate propagation** (§8.1): 1607 vs. 1608 (LRA), 6806 vs. 6806 (LIA) --
  measurably nothing, despite the `Bound.smaller`/`Bound.larger` ordered
  linked-list machinery built to support it.

### 7.2.4 The waterfall

SPASS-IQ's control flow, which is a cleaner statement of the layering
[](../qf-lia-branch-and-bound.md) describes:

*Preprocessing, once*: simplex for a rational solution (unsat ⇒ done) → simple
rounding (integral ⇒ done) → **unit cube test**, unless a skip condition fires →
if still open, compute the bounded basis; **if partially unbounded, apply the
bounding transformations** → compute branching preferences → branch-and-bound.

*Inprocessing, every node*: bound propagation (capped at 100 bounds per variable
at the root, 10 elsewhere) → prune on contradictory bounds → simplex → prune on
conflict → simple rounding → select branching variable → branch.

One implementation note that is easy to miss and would bite anyone building
this: SPASS-IQ keeps **two `Tableau` instances at all times** -- a *static* one
holding the original, never-pivoted constraints, and a *dynamic* one for simplex
work. The static one is required because simple rounding, the unit cube test's
bound computation, and the Mixed-Echelon-Hermite transformation all need the
*original* coefficient matrix, which pivoting has destroyed in the dynamic
instance. Partially unbounded problems need three more instances.

## Chapter 8 -- preprocessing is the largest measured lever in this document

Full QF_LIA division, all of SPASS-SATT's preprocessing (if-then-else, defined
variables, pseudo-boolean) on vs. off:

| | solved |
|---|---:|
| with preprocessing | **6806** |
| without | **5113** |

**+1693**, and significantly faster (>2×) on 3146 further instances. For
comparison, every branch-and-bound extension in Chapter 7 combined is +272, the
bounding transformations are +169, and the unit cube test is +56. On QF_LRA the
same techniques are worth +2.

The gain concentrates in four families, all of which Primo runs:

| family | files here | SPASS-SATT with | without |
|---|---:|---:|---:|
| `nec-smt` | 2780 | 2772 | 1422 |
| `rings` | 294 | 294 | 136 |
| `rings_preprocessed` | 294 | (same techniques) | -- |
| `pb2010` | 81 | 66 | 44 |

### Why `nec-smt` behaves this way

`nec-smt` instances are one enormous assertion of deeply nested `let` and `ite`.
Verified locally on Primo's own copy (see the census below): **2780 of 2780
`nec-smt` files contain both `(let` and `(ite`**; the sampled
`large/checkpass_pwd/prp-24-38.smt2` is 200 KB with **886 `let`s and 5240
`ite`s inside a single `assert`**.

Bromberger's account: eliminating the `let`s without also simplifying the
if-then-else structure produces formulas "too big to be handled within
reasonable memory and time limits". Two techniques together fix it, and
**neither alone helps** -- Figures 8.15 and 8.16 show SPASS-SATT performing
*worse* with only one of the two:

1. **Constant if-then-else simplification (CITE, §8.2.3).** A CITE is an
   arithmetic term whose leaves are all numeric constants. Push constant
   monomials and coefficients to the leaves, then `csimp(o, t1, t2)` pushes a
   comparator recursively down the branches, collapsing a branch to `true` or
   `false` whenever every leaf below it decides the comparison the same way.
   Requires two caches (leaf sets per shared term; `csimp` results per
   `(o, t1, t2)` triple) or it is exponential. Credited to Kim, Somenzi and Jin
   -- **the same paper [](../literature.md) already lists as "efficient
   conversion of term ITEs, not implemented"**.
2. **If-then-else compression (§8.2.3, from Burch).** After CITE simplification,
   chains of the form `(ite c_i t_{i+1} false)` / `(ite c_i false t_{i+1})`
   appear. Each is equivalent to a conjunction, and a whole chain collapses to
   `(and c*_1 ... c*_{n-1} t_n)`. SPASS-SATT does this *selectively*: it extends
   the chain only through subterms occurring **once** in the formula set, then
   names the result with a fresh propositional variable. The selectivity is the
   point -- it strengthens the sharing of the multiply-occurring tail `t_n`
   instead of destroying structure.

Result: "the two preprocessing techniques simplify the input problems so much
that SPASS-SATT does not need branch-and-bound or one of its extensions to solve
these instances. SPASS-SATT's simplex implementation on its own is sufficient."

### `rings_preprocessed` and if-then-else reconstruction

`rings_preprocessed` is `rings` with the if-then-elses already eliminated by the
standard fresh-variable-plus-two-guarded-equalities encoding -- **the exact
encoding Primo's frontend uses** ([](../preprocessing.md), "ITE Lowering"). The
thesis treats that as damage to be undone: scan for clause sets matching
`{(or c (= y t3)), (or (not c) (= y t2)), (or (= y t2) (= y t3))}` with at least
two members present, delete them, and substitute `y := (ite c t2 t3)` back in.
Then apply CITE and compression.

The census confirms the shape locally: **`rings_preprocessed` contains 0 files
with `(ite`** against `rings`'s 245 of 294 -- the two families are otherwise the
same problems.

### `pb2010`

81 files, **no `ite` and no `let` at all** -- so the relevant technique here is
the separate pseudo-boolean pass (§8.2.4), which rewrites linear pseudo-boolean
inequalities over at most three variables into clauses. 66 vs. 44.

### Small CNF (§8.2.4)

Nonnengart and Weidenbach's transformation: rename a subformula only where
renaming yields a smaller result than the plain Tseitin/standard expansion,
which introduces at most as many fresh propositional variables as greedy
renaming and usually fewer. Measured 6806 vs. 6678 on QF_LIA (**+128**), 1607
vs. 1605 on QF_LRA, and **319 vs. 218 on `convert`**.

### Defined-variable simplification

6806 vs. 6799 (LIA), 1607 vs. 1603 (LRA) -- small. Worth noting because Primo's
`substitute_ground_unit_equalities` ([](../preprocessing.md)) is the same idea,
and the thesis's number is a reasonable prior for what it can be worth by
itself.

## Local census supporting the Chapter 8 claims

Run 2026-08-29 on this workspace, `sandbox/non-incremental/QF_LIA`, pure
`grep`/`find` counting (no solver involved), recorded in
[](../run-log.md) as `2026-08-29-qflia-ite-let-census`:

| family | files | with `(ite` | with `(let` |
|---|---:|---:|---:|
| `nec-smt` | 2780 | **2780** | **2780** |
| `rings` | 294 | 245 | 294 |
| `rings_preprocessed` | 294 | **0** | 294 |
| `pb2010` | 81 | 0 | 0 |
| `convert` | 319 | 17 | 319 |
| `calypto` | 37 | 37 | 35 |

## What this thesis does not answer

- **Nothing for QF_UF.** Arithmetic only; no congruence closure, no theory
  combination beyond §5.4's arithmetic-side contract.
- **No proof or interpolant production.** §6 produces certificates of
  (un)satisfiability *for its own transformations*, not proofs in any
  general sense.
- **CutSat++ is not recommended by its own author** for implementation
  (§6.1's four objections). Chapter 3 is 68 pages and can be skipped.
- **No fill-in or refactorisation analysis.** Same gap
  [](king-thesis) has: densification during pivoting, the
  effect measured in [](../qf-lra-heuristics.md) §6, is not discussed. Chapter 7
  optimises the constant factors of row addition, not the number of non-zeros.
- **No sum-of-infeasibilities simplex.** §7.1.1 fn. 2 notes CVC4 normally uses
  SOI and that its pivoting rule could not be reproduced in the
  Dutertre--de Moura setting.
- **The QF_LRA half is thin.** Almost every large effect in Chapters 7--8 is a
  QF_LIA effect; on QF_LRA the biggest single number in the whole document is
  `HardBacktrack`'s +5.

## Follow-up leads

Ranked by measured size per unit of work, per [](../open-leads.md)'s ranking
rule. All of these are registered there; this list is the ordering argument.

1. **If-then-else preprocessing for `nec-smt` and `rings`** -- lead
   [16](lead-ite-preprocessing). Largest measured number in the
   literature Primo has read (+1693 at 40 min), 3449 files in Primo's corpus,
   and Primo currently does the one thing the thesis identifies as harmful
   (fresh-variable ITE elimination) with none of the three repairs. Requires
   CITE simplification *and* compression together; either alone is measured to
   be worse than neither.
2. **Correct lead 12c's cube prerequisite and build the guard** -- the skip
   condition is `U - L ∈ {0, 1}` on integer/slack variables, which is cheap;
   the real enabler for the equality-heavy majority is Chapter 5, not z3's
   octagon twist.
3. **Chapter 5 equality basis as the QF_UFLRA completeness mechanism** -- lead
   [17](lead-equality-basis). Replaces per-pair
   two-orientation probing with one strict-system check plus normal-form
   hashing, and is incremental across fixpoint rounds by construction.
4. **`HardBacktrack`** -- lead [18](lead-hard-backtrack).
   +123 QF_LIA, +117 on `convert` alone, and it is a *deletion*: stop restoring
   the backup assignment, call `Check()` instead.
5. **Integer coefficients with a per-row denominator** -- worth a census of
   Primo's `Rational` traffic in `pivot` first; [](../qf-lia-profile.md)'s
   4-bit finding suggests the same regime FLINT exploits.
6. **Bounding transformations** -- +169, concentrated in `20180326-Bromberger`
   (806 files here). Large work, and gated on 3: the applicability test *is*
   the equality basis of the homogeneous system.

Explicitly **not** leads, on this thesis's own measurements: greedy conflict
detection, multiple conflicts per check, further leaving-rule tuning, unate
propagation, and bound-propagation threshold tuning.
