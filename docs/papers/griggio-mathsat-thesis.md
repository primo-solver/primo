# An Effective SMT Engine for Formal Verification (Griggio's thesis)

## Citation

Alberto Griggio, *An Effective SMT Engine for Formal Verification*.
PhD dissertation, International Doctorate School in Information and
Communication Technologies, DISI -- University of Trento, December 2009.
Advisor: Roberto Sebastiani. Co-advisor: Alessandro Cimatti.

Local copy: `external/papers/griggio.pdf` -- **untracked**, per the standing
practice that reference papers are read locally and never committed
(`.gitignore` excludes `*.pdf`). This note is an original-prose map of the
thesis written for Primo; it is not a reproduction of it, and it is the only
part of this material that belongs in the repository.

## Why this note exists

[](lra-related-work.md) has flagged this thesis since 2026-07-31 as a
"likely strong practical reference" for QF_LRA implementation heuristics --
the lead came from the SOI paper's related-work section, which points at it
for "additional pivoting heuristics" -- but it had not been located or read.
It is now read. This note exists so that the 253-page source does not have to
be re-opened from scratch each time a different theory comes up: the chapter
map below is deliberately broader than Primo's current QF_LRA focus, because
the same document also covers EUF, LA(Z), DL, UT VPI, arrays, and theory
combination, all of which Primo will reach eventually.

**Page numbers below are the thesis's own printed page numbers.** The PDF's
front matter is unnumbered, so `pdf page = printed page + 12` (printed page 1,
the Introduction, is PDF page 13). Both are given where a section is likely to
be re-opened.

## Role in Primo

Primary-source confirmation, and in one place correction, of implementation
choices Primo arrived at independently or took from secondary descriptions.
The thesis is explicitly an *engineering* document -- Chapter 2 states that its
contribution is the design choices and heuristics that research papers usually
omit -- which is exactly the register Primo's own heuristics note works in.

**Current mapping, checked 2026-09-25:** the least-violated leaving rule from
§2.5.3 is implemented as the default-off `lra_least_violated_leaving` option.
The default Sparse policy chooses the shortest row first. The supplied PDF's
§2.5 was reread for the replacement chapter; the distinction below is retained
with its implementation status corrected.

(chapter-map)=
## Chapter map

The thesis has three parts. Part I (the solver itself) is the part relevant to
Primo today; Parts II and III are functionality built on top of it.

| part / chapter | printed pp. | pdf pp. | relevance to Primo |
|---|---|---|---|
| Introduction | 1-8 | 13-20 | skim only |
| **Part I -- MathSAT: an Efficient SMT Solver** | 9-73 | 21-85 | -- |
| Ch. 1 Background | 13-38 | 25-50 | standard SMT background; useful definitions |
| Ch. 2 **Details on MathSAT** | 39-73 | 51-85 | **the reason to read this thesis** |
| **Part II -- Extended SMT Functionalities** | 75-177 | 87-189 | -- |
| Ch. 3 Extraction of Unsatisfiable Cores | 79-103 | 91-115 | future, if Primo grows unsat cores |
| Ch. 4 Generation of Craig Interpolants | 105-177 | 117-189 | future, if Primo grows interpolation |
| **Part III -- Exploiting SMT for Software Verification** | 179-213 | 191-225 | -- |
| Ch. 5 Software Model Checking via Large-Block Encoding | 183-213 | 195-225 | out of scope |
| Ch. 6 Conclusions | 215-216 | 227-228 | skim only |
| Bibliography | 217- | 229- | -- |

(ch1)=
### Chapter 1 -- Background (pp. 13-38)

Standard material, drawn by the author's own statement from Sebastiani's lazy
SMT survey and the DTC papers. Worth knowing where things are rather than
reading start to finish.

- **§1.1 The SMT problem (p. 13), §1.1.1 Notation (p. 15).** Sets up the
  `T2B`/`B2T` mapping between theory atoms and the Boolean skeleton, and the
  notion of propositional satisfaction `|=p`. Primo's own atom/literal
  bookkeeping is the same idea under different names.
- **§1.2 T-solvers (p. 16).** The feature list a theory solver is expected to
  provide beyond a satisfiability verdict: model generation, conflict-set
  generation, incrementality and backtrackability, and deduction of unassigned
  literals. This is a compact statement of the interface Primo's theory layer
  targets.
- **§1.3 Modern lazy SMT solvers (p. 18), §1.3.1 the online schema (p. 18).**
  The `T-DPLL` schema, with Early Pruning and T-propagation defined on p. 20.
  Figure 1.1 (p. 19) is the reference diagram the rest of the thesis points back
  to.
- **§1.4 Relevant theories (p. 22).** One subsection each: EUF (p. 22),
  Linear Arithmetic (p. 23), difference logic (p. 24), UT VPI (p. 25), arrays
  (p. 26), bit vectors (p. 27). Each gives the fragment's complexity, convexity
  and stable-infiniteness, and names the standard algorithm. Useful as a
  one-page orientation when Primo opens a new theory.
- **§1.5 Combinations of theories (p. 28).** Nelson-Oppen (§1.5.1, p. 28) and
  Delayed Theory Combination, with Figures 1.2 and 1.3 (pp. 32-33);
  Ackermann's reduction (§1.5.2, p. 37). Complements Primo's existing
  [](nelson-oppen-closure-methods.md) and
  [](model-based-theory-combination.md).

(ch2)=
### Chapter 2 -- Details on MathSAT (pp. 39-73)

The engineering chapter, and the one that justifies having chased this thesis
down. Its stated contribution is design choices, heuristics and implementation
details omitted from research papers -- some peculiar to MathSAT, some folklore
among solver developers but unpublished.

- **§2.1 Overview (p. 39).** Architecture diagram (Figure 2.1, p. 40):
  preprocessor, DPLL engine, layered theory solvers, model generator, proof
  engine.
- **§2.2 The preprocessor (p. 41).** Three stages. (a) Satisfiability-preserving
  simplification -- notably canonicalizing equivalent theory atoms to a unique
  representation, with the example that `2x + y - x + 2 <= 5` becomes
  `x + y <= 3`, plus top-level propagation (`x = 5 /\ f(x) < 3` becomes
  `f(5) < 3`). **This is Primo's H1b scale-canonical polynomial key and its
  top-level rewriting, arrived at independently and confirmed here as ordinary
  MathSAT practice.** (b) CNF conversion: MathSAT kept plain Tseitin, having
  found the more sophisticated SAT-community algorithms gave controversial,
  no-clear-winner results on SMT problems -- a data point directly relevant to
  Primo's own top-level-OR Tseitin experiments. (c) Static learning: adding
  small T-valid lemmas (e.g. transitivity constraints) before search.
- **§2.3 DPLL/T-solver interaction (p. 42); §2.3.1 Adaptive Early Pruning
  (p. 42).** AEP is presented as previously undescribed elsewhere. Instead of
  the usual "weak/approximate early pruning" (a cheap incomplete solver for EP
  calls), MathSAT *adapts the frequency* of EP calls to their measured
  usefulness, with a `tsolvers_call_interval` counter: reset to 1 on a
  T-conflict, halved when the call produced T-deductions, doubled when two
  consecutive calls produced neither, otherwise unchanged. Reported effect
  (Figure 2.2, p. 44, SMT-COMP'09 instances, 900s): 4 more unsatisfiable
  instances solved and up to ~20% less total time on both sat and unsat sets.
  **Directly relevant to Primo's online-check-call volume**, and a cheap idea:
  it is a counter and four rules, not a new algorithm.
- **§2.4 The EUF-solver (p. 45).** Congruence closure following Simplify's
  E-graph, with explanation generation adapted from Nieuwenhuis-Oliveras. Two
  points Primo can use. First, the layering trick: the EUF-solver is run as a
  cheap incomplete pre-check for *other* theories by treating their symbols as
  uninterpreted, and is made better at that by teaching it that distinct
  numeric constants can never be congruent, and that predicates like `x < y` or
  `!(x <= y)` imply `x != y`. The author calls these trivial to implement,
  zero-overhead, and always enabled. Second (p. 47): a custom memory allocator
  that keeps frequently co-accessed small objects contiguous gave a significant
  EUF speedup -- the same allocation-locality diagnosis Primo's own profiling
  reached from the opposite direction.
- **§2.5 The LA(Q)-solver (pp. 47-56).** See [](#la-q) below -- this is the
  section Primo actually came for.
- **§2.6 The LA(Z)-solver (pp. 57-70).** Heavily layered (Figure 2.5, p. 57):
  real relaxation via the LA(Q)-solver first, then a Diophantine equation
  handler (§2.6.1, p. 58) resembling the equality-elimination half of the Omega
  test, then normalization/tightening back through LA(Q), then branch and bound
  (§2.6.2, p. 69). Two things stand out for later. Branch and bound is done by
  *splitting on demand*: rather than reasoning disjunctively inside the theory
  solver, it emits the lemma `(x_k <= floor(q_k)) \/ (x_k >= ceil(q_k))` to the
  DPLL engine and lets the Boolean layer do the search, inheriting backjumping
  and learning for free. And the explanation machinery (pp. 68-69) works by
  labelling equations and tracking which linear combinations produced each
  derived inequality, so conflicts can be mapped back to original constraints.
- **§2.7 Other theory solvers (p. 71).** Arrays (§2.7.1, p. 71) via lazy axiom
  instantiation on top of EUF, with the strategy of deferring axiom
  instantiation until an EUF-consistent complete assignment exists and
  preferring read-over-write axioms over extensionality. DL (§2.7.2, p. 71) via
  Cotton-Maler negative-cycle detection -- the same paper behind Primo's
  [](difference-constraint-propagation.md). UT VPI (§2.7.3, p. 72) by encoding
  to DL, with an extra integer check when the rational check returns sat.
  **A useful DL-specific trick on p. 72:** for difference logic you can decide
  *statically*, before search, whether infinite precision is needed at all, by
  summing the absolute values of all constants in the atoms and checking for
  overflow. Primo has no equivalent static check.
- **§2.8 Combination of theories (p. 72).** DTC or Ackermann's reduction.
  Improvements over the original DTC: interface equalities introduced only when
  a per-theory-consistent assignment is found rather than upfront; interface
  equalities never chosen for case splitting while any other atom is unassigned,
  and assigned false first, while still allowing theory solvers to deduce them
  at any time. Also a mixed DTC/Ackermann strategy via dynamic ackermannization.
  Closes by naming model-based theory combination as the promising direction --
  which is what Primo's [](model-based-theory-combination.md) already tracks.

(ch3)=
### Chapter 3 -- Extraction of Unsatisfiable Cores (pp. 79-103)

Not currently a Primo feature; read this if that changes.

The contribution is the **Lemma-Lifting** approach (§3.2, p. 86): rather than
building a proof or instrumenting the DPLL engine, run the SMT solver, keep the
theory lemmas it generated, hand the Boolean skeleton of the original problem
*plus those lemmas* to an off-the-shelf propositional unsat-core extractor, then
delete the lemmas from the resulting core and map what remains back to original
clauses. It rests on two facts (p. 87): theory lemmas are T-valid so they cannot
change T-satisfiability, and the skeleton conjoined with all theory lemmas is
propositionally unsatisfiable -- which is just the termination condition of any
lazy SMT solver on an unsat input. The appeal is that it decouples core quality
from the SMT solver entirely and inherits improvements in Boolean core
extractors for free.

§3.1 (p. 80) surveys SAT and SMT core techniques; §3.3 (p. 93) evaluates, and
as a byproduct compares the available Boolean core extractors against each
other (§3.3.2, p. 98).

(ch4)=
### Chapter 4 -- Generation of Craig Interpolants (pp. 105-177)

The largest chapter, and out of scope unless Primo grows interpolation. Its
framing argument (pp. 105-106) is worth knowing even so: interpolation tools of
the time deviated from state-of-the-art SMT practice and paid for it -- e.g.
encoding strict inequalities as a weak inequality conjoined with a disequality,
which is sound but destroys constraint structure and forces reasoning in
`LA(Q) ∪ EUF`. The chapter's stated contributions (p. 107) are interpolation
algorithms for LA(Q) built on the Dutertre-de Moura algorithm without needing
theory combination (§4.2, p. 114), for DL (§4.3, p. 128) and UT VPI (§4.4,
p. 131) that keep interpolants inside the respective fragment, and for combined
theories via DTC rather than Nelson-Oppen (§4.5, p. 147).

One item is relevant to Primo *outside* interpolation: **§4.2.1 (p. 115) spells
out the conflict set the Dutertre-de Moura algorithm produces** -- for a basic
variable `x_i` whose lower bound is violated, the conflict is the upper bounds
of the positive-coefficient non-basic variables in its row, the lower bounds of
the negative-coefficient ones, and `x_i >= l_i` itself. That is the Farkas-style
explanation Primo already builds in `build_conflict`; this is a second primary
source stating it precisely, and the surrounding discussion of what the
algorithm does *not* record is a useful check if explanation minimality ever
becomes a question.

(ch5)=
### Chapter 5 -- Software Model Checking via Large-Block Encoding (pp. 183-213)

Out of scope for Primo. Joint work with Beyer and Keremoglu on CPAchecker.
The idea (§5.2, p. 191) is to summarize whole regions of a control-flow
automaton into single ART edges, instead of one edge per program operation, so
that the model checker enumerates far fewer paths at the cost of much larger and
more complex SMT queries -- deliberately moving the bottleneck from the number
of decision-procedure calls to their individual cost, on the argument that
modern SMT solvers are the right thing to bet on. Evaluated against Blast
(§5.4.2, p. 202) and SatAbs (§5.4.4, p. 211).

(la-q)=
## §2.5 in detail -- the LA(Q)-solver (pp. 47-56)

This is the section [](lra-related-work.md) was after. The author's own framing
(p. 48) is that the Dutertre-de Moura description is high-level and leaves room
for implementation choices with very large performance consequences, and that
MathSAT's version is the result of months of profiling and tuning. That is the
same premise as Primo's [](../qf-lra-heuristics.md).

**§2.5.1 High-level view (p. 48).** A restatement of the Dutertre-de Moura
algorithm: the a-priori partition into initially-basic and initially-non-basic
variables, the requirement that only elementary atoms (`x_j >= l_j`,
`x_j <= u_j`) are asserted and retracted during search while the equations never
change, the tableau invariants for the candidate model `β` (2.2, p. 49), and
pivoting (2.3-2.5, pp. 49-50). Nothing new relative to Primo's existing
[](fast-linear-arithmetic-dpllt.md), but p. 50 states the cost model bluntly:
pivoting steps are the most expensive operation and the main bottleneck, and the
reason add/backtrack are cheap is precisely that they need no pivoting at all.

**§2.5.2 Reducing the cost of pivoting (p. 51).** Two choices, both of which
Primo has independently made:

- *Number representation.* A custom rational library using native integers where
  possible and falling back to GMP only on overflow. The thesis notes this was
  described by others (citing Monniaux 2009 and Dillig-Dillig-Aiken 2009) and
  was in use by Yices -- credited as first -- BarceLogic, OpenSMT and Z3. This
  is Primo's H2, and this is a **third** independent primary source for it.
- *Tableau representation.* Sparse, not a dense matrix. Notably, the thesis
  reports having tried both hash tables per equation and **sorted arrays of
  (variable, coefficient) pairs**, and having found the arrays better overall
  despite logarithmic rather than amortized-constant coefficient lookup --
  because of contiguous memory layout and faster iteration, iteration being what
  pivoting actually does. Primo's row representation should be compared against
  this deliberately; the thesis is unusual in reporting the losing alternative.

**§2.5.3 Reducing the number of pivoting steps (p. 52).** The heart of it, and
the part with an actionable discrepancy -- see below. MathSAT's greedy rule,
stated on p. 53, aims to minimize the changes needed to the tableau and to `β`:

- **Leaving (basic) variable:** pick the violated basic variable whose `β(x_i)`
  is *closest* to its violated bound -- smallest `l_i - β(x_i)` or
  `β(x_i) - u_i`. **Tie-break:** the one whose row has fewer non-basic variables
  with non-zero coefficient.
- **Entering (non-basic) variable:** among those eligible, the one occurring
  with non-zero coefficient in the *fewest rows*, to minimize the number of row
  updates performed during pivoting.
- **Anti-cycling:** the greedy rule can cycle, so it is applied only up to a
  threshold number of pivots, after which the rule reverts to Bland's. The
  threshold used is *the number of variables in the input problem* (p. 54).

**§2.5.4 Experimental evaluation (pp. 54-56).** Ablation on SMT-LIB QF_LRA,
1200s timeout, each optimization disabled individually and then all together.
Median / mean time ratio against the default configuration (Figure 2.4, p. 56):
custom numbers off 2.50 / 2.38; equations as hash tables instead of sorted
arrays 1.20 / 1.30; Bland's rule instead of the greedy strategy 1.42 / 2.77
(9th percentile 6.67); everything off 4.48 / 8.26 (9th percentile 17.56), with
21 more instances unsolved.

Two readings matter for Primo. First, these are the same three levers Primo
identified independently (H2, row representation, H3), and the thesis's ranking
of them by median effect -- numbers > pivot rule > row layout -- is consistent
with Primo's own profile-driven ordering. Second, and more soberly: **the whole
ablation spans a mean factor of about 8**. That is the same order as, not larger
than, the factor Primo's section 6.1a estimates for its remaining z3 gap, which
suggests these levers are the right ones but that none of them individually is
the missing 2-5x.

(leaving-rule-discrepancy)=
## The one correction: Primo's docs describe the wrong leaving rule

[](../qf-lra-heuristics.md) (its terminology table, and item H3's prior-art
note) and [](lra-related-work.md) both attribute to Griggio a **most-violated**
leaving-variable rule, sourced from King's thesis §2.2.12 and equated there with
Primo's Dantzig choice. **The primary source says the opposite.** Page 53 selects
the basic variable *closest* to its violated bound -- the least-violated one --
and uses the short-row criterion only as a tie-break.

The initial reading identified a missing comparison. The current
`LraTableau::check` now offers all three policies:

- Dantzig-style **most-violated** first, tie-broken by smallest index
  (`lra_tableau.cpp`, the `violation > best_violation` branch);
- **shortest-row** first with larger violation as the tie-break, the default
  under Sparse;
- **least-violated** first, shortest row as tie-break, enabled by
  `--lra-least-violated-leaving` under Sparse.

The least-violated alternative has been implemented and measured; it was not
promoted to default. The dated comparison remains in the QF_LRA options table
in [](../arithmetic.md). Re-reading the
thesis supplies no new performance evidence.

Two caveats before anyone treats the docs as simply wrong:

1. **Version.** This thesis is December 2009 and describes MathSAT 4. King's
   thesis is 2014 and attributes the most-violated rule to *MathSAT5* alongside
   CVC4. The rule may genuinely have changed between the two, in which case both
   sources are right about different solvers and only Primo's *conflation* of
   them is wrong.
2. **Primo's tie-break is not MathSAT's.** Primo's `sparse_leaving` tie-break
   prefers the *larger* violation among equal-length rows; MathSAT's primary key
   prefers the smaller. Any A/B must implement the rule as stated rather than
   flipping one comparator and assuming that reproduces it.

The unrelated smaller difference: MathSAT reverts to Bland after a threshold
equal to the number of input variables, where Primo uses `4 * vars_.size() + 64`
(`lra_tableau.cpp`). Primo's is roughly four times more permissive. Not worth a
standalone experiment, but worth varying alongside the leaving rule since the
two interact.

## What this thesis does *not* answer

Recorded so the source is not re-opened expecting these:

- **No floating-point-guided basis selection.** Direction D2 in
  [](../qf-lra-heuristics.md) gets nothing here; the thesis cites Monniaux 2009
  only for the number representation and for the observation that the
  Dutertre-de Moura description leaves implementation room.
- **No revised simplex or LU factorization** (direction D4). The tableau is
  maintained explicitly throughout.
- **No treatment of fill-in as such.** The pivot rules are justified as
  minimizing tableau/`β` changes and the number of row updates, which is the
  same cost intuition behind Primo's Markowitz-style pricing, but the thesis
  never measures row density or discusses fill-in growth during search -- the
  effect Primo's section 1.3 instrumentation found dominant.
- **Nothing on the tropical-matrix family** or on structural phase-selection
  heuristics for guarded arithmetic chains. The open question that prompted
  this whole reading path remains open.

## Follow-up leads

- The thesis's own citation for the sorted-array-vs-hash-table finding is
  internal (its §2.5.4 experiments), so there is no further source to chase.
- Static learning (§2.2, p. 42) is cited to the MathSAT and Yices-lineage
  papers; if Primo pursues it, start there rather than here, since this thesis
  describes it in one paragraph.
- The AEP idea (§2.3.1) is stated as unpublished elsewhere as of 2009. If it is
  worth implementing, this section *is* the primary source and there is nothing
  more detailed to find.

## Section 1.5.1, purification -- read 2026-09-06, and it answers an open bug

This note was written for the arithmetic chapters and skipped the combination
background. That background is the primary source for a defect this project had
been treating as its own puzzle: `bugs/qf-uflia-nested-application-model`,
where Primo prints a QF_UFLIA model whose UF table does not satisfy the input.

**What §1.5.1 (p. 30) says.** A mixed formula is made pure by labelling every
alien subterm with a fresh variable and asserting the label:

```
f(x + 3y) = g(2x - y)
  ==>  f(v1) = g(v2)  /\  v1 = x + 3y  /\  v2 = 2x - y
```

Purification is linear, and the labels are the **interface variables** -- those
occurring in both pure parts. Nelson-Oppen (§1.5.1, citing [NO79]) then reduces
satisfiability to finding an *arrangement*: an equivalence relation over the
shared variables consistent with both pure parts.

**The consequence for model construction**, which the thesis does not spell out
because it is immediate: in the purified formula every UF application has the
form `f(v_1, ..., v_n)` and is itself labelled by some `v_f`. All of those are
interface variables, so the arithmetic model assigns every one of them. The UF
table row is therefore `(val(v_1), ..., val(v_n)) -> val(v_f)`, read off rather
than searched for, and **no value can be missing**. A model-construction
procedure that has to *look for* the value of a UF application has already lost
the purification map.

**Historical diagnosis (superseded by typed model reconstruction).** It purifies -- `:no.purification.arithmetic-uf-definitions`
reports exactly one definition per Int-sorted UF application -- but
`MixedTheoryPurificationResult` returns only counts and the rewritten
assertions, with no label-to-application map. `qf_uflia_int_values` then
rediscovers the correspondence from asserted equality literals and from merging
tableau terms that share a value, and on three levels of nesting that
reconstruction fails: on `witness.smt2`, six Int application terms exist in the
arena (originals plus purified copies) and only three carry a value.

**Implemented follow-up.** `PurificationProxyConversion` now retains the map,
and the shared typed model adapter uses it as a base-model equality constraint.
`qf_uflia_int_values` and printer-side row arbitration have been removed. See
[](../model-interpretation.md) for the current implementation and acceptance
status, and the bug file for the historical measurements.
