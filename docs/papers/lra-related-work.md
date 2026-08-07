# LRA Related Work

Survey of literature beyond Dutertre & de Moura's ["A Fast Linear-Arithmetic
Solver for DPLL(T)"](fast-linear-arithmetic-dpllt.md), the paper Primo's
future LRA theory solver is planned to be based on. Unlike the per-paper
deep-study notes elsewhere in this directory, these entries are scoped from
abstracts/search summaries only — read the source before relying on
implementation details.

**Scope: QF_LRA only.** Primo's near-term arithmetic target is the SMT-LIB
`QF_LRA` logic (quantifier-free linear real arithmetic) — no integers, no
quantifiers. Entries below are ranked and grouped for that scope; LIA,
mixed-integer, and quantified-arithmetic work is included only where it
directly informs a QF_LRA decision procedure, and is marked as out of scope
otherwise so it doesn't get mistaken for near-term reading.

## Access status

Checked by actually fetching each URL, not just search-result snippets.

Freely accessible (fetched and read the actual PDF):
- Monniaux, "On using floating-point computations to help an exact linear
  arithmetic decision procedure" (CAV 2009, arXiv:0904.3525)
- King/Barrett/Dutertre, "Simplex with Sum of Infeasibilities for SMT" (FMCAD 2013)
- King/Barrett/Tinelli, "Leveraging Linear and Mixed Integer Programming for SMT" (FMCAD 2014)
- Nalbach et al., "FMplex" (arXiv 2310.00995)
- Bromberger/Weidenbach, "Computing a Complete Basis for Equalities..." (SMT@IJCAR 2016, MPI repository)
- Bixby, "Solving Real-World Linear Programs" (Operations Research 2002, gwern.net mirror)
- Rueß & Shankar, "Solving Linear Arithmetic Constraints" (CSL-SRI-04-01,
  2004) -- the SRI technical-report URLs on Shankar's page are dead `ftp://`
  links and no open mirror was found; copy supplied by the user, read
  2026-08-01
- "Local Search For SMT On Linear and Multilinear Real Arithmetic" (arXiv 2303.06676)
- Fast Cube Tests for LIA / New Techniques for Linear Arithmetic (MPI-hosted, same publisher pattern as the equalities paper above — not individually re-verified)

Paywalled, no open mirror found — need institutional access or the user to
fetch a copy:
- Korovin, Tsiskaridze, Voronkov, **"Conflict Resolution"** (CP 2009, Springer
  LNCS 5732) — checked Manchester eScholar (dead link) and Semantic Scholar
  (no PDF surfaced); the most important gap since it's the strongest
  non-Simplex alternative listed. Its companion "Implementing Conflict
  Resolution" (Ershov 2011) is equally paywalled.
- Bland, "New Finite Pivoting Rules for the Simplex Method" (Math. of OR, 1977)
- Harris, "Pivot Selection Methods of the Devex LP Code" (Math. Programming, 1973)
- Forrest & Goldfarb, "Steepest-Edge Simplex Algorithms for Linear Programming" (Math. Programming, 1992)
- Maros, *Computational Techniques of the Simplex Method* (Springer, 2003) — a book, not a paper; needs a library/purchase regardless

## Follow-up leads found while reading the above

- SOI-Simplex's own related-work section (Section I) points to **Alberto
  Griggio's PhD thesis** for "a number of details on implementation and
  additional pivoting heuristics" for QF_LRA — likely a strong practical
  reference given MathSAT's QF_LRA solver is Simplex-for-DPLL(T)-based.
  **Located and read 2026-08-04**; supplied by the user as
  `docs/papers/griggio.pdf` (untracked). The lead was correct: its §2.5
  (pp. 47-56) is exactly the promised material. Chapter map and findings are in
  [](griggio-mathsat-thesis.md). **It also contradicts the most-violated
  leaving-rule attribution recorded below** — see that note and the entry's own
  discrepancy section.

## Direct extensions of Simplex-for-DPLL(T)

**David Monniaux. "On using floating-point computations to help an exact
linear arithmetic decision procedure." CAV 2009 (arXiv:0904.3525).**
The paper's headline contribution is a floating-point preprocessing phase
(run GLPK's dual simplex, then "force-pivot" the exact rational tableau to
the basis it found) that is sound and complete because the numerical result
is only ever used to pick a starting configuration. **That contribution is
not implemented in Primo and is not currently planned** -- it needs an
incremental floating-point LP engine alongside the exact tableau.

What Primo does take from this paper are three implementation points it
states as ordinary practice rather than as contributions. They are cited
here (and at the corresponding code) because they are the *source* of the
design, even though they are not what the paper is about:

- **Section 4, two-tier rational representation.** "Rational numbers are
  implemented as quotients of two 32-bit numbers; in case of overflow,
  extended precision rationals from the GMP library are used. The reason
  behind a two-tier system is that GMP rationals inherently involve some
  inefficiencies, including dynamic memory allocation, even for small
  numerator and denominator. In many examples arising from verification
  problems, one never needs to call GMP." **Implemented 2026-07-28** in
  `util::Rational` (competitiveness-roadmap H2) -- see `docs/arithmetic.md`
  and `docs/qf-lra-heuristics.md`. *Deviation:* Primo's small form is 64-bit
  rather than 32-bit, using `__int128` intermediates. That width was settled
  empirically rather than argued: building the same commit at both widths,
  32-bit is equal or up to ~13% faster wherever coefficients never leave 32
  bits, but 64-bit is 2.2x faster on the one instance that does overflow
  (12-fold fewer big-form operations), so 64-bit is kept.
  *Independently corroborated:* profiling Primo before implementing it put
  ~42% of runtime in the allocator and ~12% in GMP copy/init against ~5% in
  real bignum arithmetic -- the same diagnosis the paper gives.
- **Section 2.1, atom canonicalization.** Inequalities are scaled to coprime
  integer coefficients and optionally negated so the leading coefficient is
  positive, "so that two inequalities constraining the same direction in
  space appear with the exact same left-hand side"; and "for each left hand
  side that is *not a variable*, a new variable is introduced". The second
  half is the source for item H1a, the first half for item H1b (the
  long-deferred scale-invariant polynomial matching, roadmap item 3).
  **Both implemented 2026-07-28, unconditional.** H1a removed 36-95% of
  tableau rows on the lagging families -- Primo used to introduce a slack for
  *every* distinct polynomial, including bare variables -- and H1b collapsed
  proportional polynomials onto one slack. *Deviation:* our canonical form
  scales to a leading coefficient of 1 rather than to coprime integers, which
  is the same equivalence class.
- **Page 8, sparsity-ordered pivoting.** "We sort the variables ... according
  to the number of nonzero coefficients in the corresponding equation ... the
  pivoting steps are faster when the equation defining the basic variable to
  be pivoted has few nonzero coefficients", and likewise pre-sorting by
  "number of occurrences in equations". Stated there for the forced-pivoting
  phase, but the cost argument is the same one behind heuristics-note item
  H3. **Implemented 2026-07-29 and promoted to default** as
  `LraTableau::PivotingRule::Sparse` -- both criteria: the occurrence count
  prices the entering variable, the row length prices the leaving one. On the
  237-file stratified sample this takes Primo from 144 to 168 solved with zero
  regressions. *Deviation:* applied to the main Check/Pivot loop rather than
  to a forced-pivoting phase, which Primo does not have. See
  `docs/arithmetic.md`'s "Fill-in-aware pivot selection".

Clean-room note: implemented from this paper's prose (plus Knuth TAOCP Vol. 2
Sec. 4.5.1 for the rational arithmetic itself), not from any solver's source.
https://arxiv.org/abs/0904.3525

### Floating-point-guided exact simplex: the rest of the line

Monniaux's CAV 2009 force-pivot is not a one-off, and the follow-up work is
what should calibrate direction D2 in [](../qf-lra-heuristics.md) section 6.5.
Surveyed 2026-07-31; the two papers marked *read* were read for this note, the
rest are cited from their descriptions in those two.

**Tim King, Clark Barrett, Cesare Tinelli. "Leveraging Linear and Mixed
Integer Programming for SMT." FMCAD 2014, 139-146.** *(read)*
<https://theory.stanford.edu/~barrett/pubs/KBT14.pdf>
**The most relevant paper to D2, and a second independent implementation of
Monniaux's idea** -- in CVC4, using GLPK 4.52. Its `SeedExact` procedure takes
the LP solver's final basic set `B̃` and pivots the exact tableau toward it,
repeatedly finding a pair `x_i, x_j` where `x_j` should be basic and `x_i`
should not and `T_i,j != 0`. The paper says so directly: "The SeedExact
procedure can be seen as achieving a similar effect as ForcedPivot in [10]"
-- where [10] is Monniaux CAV 2009.

Three things here matter more to us than the basis transfer itself.

1. **The control structure is a fallback, not a preprocessing phase.**
   `BalancedSolve` runs the *exact* solver first under a pivot cap (k_EX =
   200), and calls the LP solver only when that cap is exceeded. "We thus use
   an exact solver optimized for fast incremental checks as the primary theory
   solver. However, we also instrument this solver so that it can detect when
   it is starting to have difficulty, and in these cases we have it call the
   LP solver." This dissolves D2's Risk B (that a preprocessing phase cannot
   see bounds asserted during search): called mid-search, the bounds are known.
2. **`ImportAssignment`**: the LP solver's floating-point assignment is turned
   back into rationals by Diophantine approximation (continued-fraction
   expansion, denominators capped at D = 2^26), deliberately biased toward
   values that are cheap to represent, "partly because the discarded portion
   often corresponds exactly to a rounding error". D2 would need this and it
   is not obvious.
3. **The measured payoff on QF_LRA is small.** Table I: on 652 QF_LRA
   benchmarks CVC4+MIP solves 645 against plain CVC4's 636 -- "**plus 9
   additional problems** (solving more than any other solver), all from the
   challenging `miplib` family", which after preprocessing is internally mixed
   integer/real, so the gain runs through the *MIP* path rather than the LP
   force-pivot path. The comparison set includes yices2 632, mathsat5 622, z3
   615.

And the sentence that should be read before anyone budgets D2 as a large win:
"In each of these studies, experimental results on SMT-LIB benchmarks show
that existing SMT solvers outperform the experimental solvers modified to use
LP solvers, **even if the LP solver results are not checked for correctness**.
The main reason for this is that for these benchmarks ... solving requires
many related calls to the theory solver, each of which is relatively simple."
KBT's contribution is precisely the balanced control structure that makes it
pay at all.

**Diego Caminha B. de Oliveira, David Monniaux. "Experiments on the
feasibility of using a floating-point simplex in an SMT solver." PAAR 2012.**
<https://easychair.org/publications/paper/NqS>
The direct continuation of Monniaux CAV 2009, in OpenSMT: call the LP solver
first and use the result to seed the exact search. Reported **14% faster
overall** (4h44m45s against the exact simplex's 5h26m55s over 634 benchmarks),
with **11 of 634 (1.7%)** having at least one check where the floating-point
and exact simplexes disagreed -- rare, but the reason the exact solver must
still verify. 14% is the number to weigh D2 against, not the 2-3x that other
entries in section 6.5 offer.

**Germain Faure, Robert Nieuwenhuis, Albert Oliveras, Enric
Rodríguez-Carbonell. "SAT Modulo the Theory of Linear Arithmetic: Exact,
Inexact and Commercial Solvers." SAT 2008, 77-90.**
<https://www.cs.upc.edu/~oliveras/sat08.pdf>
Several LP solvers integrated into Barcelogic, with an exact solver lazily
checking their results for soundness. Analyses why sophisticated LP solvers
are hard to use inside SMT at all -- disequalities, strict inequalities, and
incorrect results from floating-point arithmetic -- and proposes result
checking plus error recovery. The earliest of this line to take unsoundness
seriously.

**Y. Yu, S. Malik. "Lemma Learning in SMT on Linear Constraints." SAT 2006,
142-155.** The earliest use of an LP solver as an SMT theory solver, and
noted by KBT as *not* addressing the possibility of incorrect LP results.
Cited for provenance only.

**Practical note, measured 2026-07-31.** The cvc5 1.3.4 binary this project
benchmarks against reports `glpk : no` in `--show-config`. CVC4's LP
integration is therefore **not** present in the reference solver, and cvc5's
results on the mid-band gap (9 of 59) are not evidence about this technique
in either direction.

**Tim King, Clark Barrett, Bruno Dutertre. "Simplex with Sum of
Infeasibilities for SMT." FMCAD 2013.**
Addresses Dutertre-de Moura's weakest point: its Bland's-rule pivoting has no
global progress criterion and can converge slowly on hard instances.
SOI-Simplex minimizes a single objective — the sum of infeasibilities across
all out-of-bound basic variables — giving pivoting a global direction at the
cost of extra per-pivot analysis. Reported more robust on large/hard
examples. Directly relevant if Primo's tableau implementation later hits the
same slow-convergence pathology Dutertre-de Moura is known for.
https://www.cs.utexas.edu/~hunt/FMCAD/FMCAD13/papers/73-Simplex-Sum-SMT.pdf

**Implemented (2026-07-25, `PivotingRule::Soi`, opt-in via
`--lra-pivoting-rule soi`).** The full algorithm, not a scoped-down
version: the incremental f-row/direction tracking, the breakpoint-sorted
`ΔVio(j,δ)` walk (Section V-A), and the multi-row conflict +
QuickXplain minimization (Section V-B) for when several rows are stuck
simultaneously. Directly verified against the paper's own Fig 6 worked
example (a white-box unit test reproduces the exact `(entering=x2, δ=1,
leaving=x1)` pivot the paper hand-traces). See `docs/arithmetic.md`'s
"SOI-Simplex pivoting" section for the full design, including the one
genuinely new piece beyond the paper (trail/incremental integration with
`push()`/`pop()`, which the paper's single non-incremental `Check` call
doesn't address) and a real heap-use-after-free found there during manual
testing, since fixed. A quick 17-file real-benchmark comparison found
identical results to Bland (7/17 solved, same files) -- didn't move that
particular narrow sample, matching this session's items 1-2 (solve-eqs,
row-based propagation) each individually failing to close the same gap.

**Tim King, Clark Barrett, Cesare Tinelli. "Leveraging Linear and Mixed
Integer Programming for SMT." FMCAD 2014.**
Replaces/augments the incremental Simplex theory solver with calls to an
external LP/MIP solver. The LP-only mode is in scope for QF_LRA (trades
incrementality for the maturity of industrial LP engines, worth a look if a
from-scratch tableau proves to be a maintenance burden); the MIP
branch-and-cut half is LIA/mixed and out of scope for now (see below).
https://theory.stanford.edu/~barrett/pubs/KBT14.pdf

## Standard Simplex heuristics (general LP, not SMT-specific)

Dutertre-de Moura's tableau is a bounded-variable Simplex; how it pivots
(which entering/leaving variable it picks each step) is a classic LP
engineering question, independent of the DPLL(T) integration questions
above. Worth knowing before hand-rolling a pivoting rule from scratch:

**Pivoting/pricing rules.** Robert Bland, "New Finite Pivoting Rules for the
Simplex Method" (Math. of OR, 1977) — the anti-cycling rule (smallest-index
entering/leaving variable) that Dutertre-de Moura's own termination argument
already relies on; worth reading directly rather than only through the SMT
papers that cite it. Dantzig's original largest-coefficient rule is cheapest
per pivot but weakest on degenerate/cycling-prone instances. Devex pricing
(Paula Harris, "Pivot Selection Methods of the Devex LP Code," Math.
Programming 1973) approximates steepest-edge pricing at much lower cost per
pivot and is the de facto industry default (used in CPLEX, and reportedly in
Yices' own Simplex). Forrest & Goldfarb, "Steepest-Edge Simplex Algorithms
for Linear Programming" (Math. Programming 1992) covers the more expensive
but iteration-minimal alternative. A recent comparative benchmark of six
rules (Bland, Dantzig, Greatest Increment, Least-Recently-Considered,
Partial Pricing, Steepest Edge) found Dantzig fastest per-instance wall time
but Steepest Edge fewest iterations, with Bland notably *not* the most
robust choice outside its anti-cycling guarantee — a useful sanity check
before assuming any one rule dominates.

**Tim King. "Effective Algorithms for the Satisfiability of Quantifier-Free
Formulas Over Linear Real and Integer Arithmetic." PhD thesis, New York
University, September 2014.**
<https://cs.nyu.edu/media/publications/king_tim.pdf>
The most complete implementor's account of Simplex-for-DPLL(T) available, and
the reference that settles two questions for us.

- **The column-length entering rule is the field's standard, not a novelty.**
  Section 2.2.12: "The heuristic commonly used in implementations of
  SimplexForSMT for selecting the entering variable is to select the `x_j`
  with minimum column length `|(T^|)_j|` with ties broken by [the variable
  order]. This heuristic works quite well in practice but is not guaranteed to
  terminate. A simple means of ensuring termination is to count the number of
  heuristic selections and switch to Bland's rule once this passes a finite
  cap." That is precisely item H3 *and* its anti-cycling fallback, and King
  attributes the family to MathSAT, OpenSMT, Yices, Yices 2, z3 and CVC4. Our
  own citation of Monniaux (CAV 2009) for the sparsity criterion remains
  accurate as a source, but H3 should not be read as a technique transferred
  from one paper -- it is what everyone in this space already does. King also
  records Griggio's most-violated leaving-variable rule (used by CVC4 and
  MathSAT5), which is our Dantzig-style leaving choice; our opt-in
  *shortest-row* leaving rule (`--lra-sparse-leaving`) is the part with no
  counterpart described here.

  **Corrected 2026-08-04, on reading Griggio's thesis itself.** The primary
  source states the *opposite* rule: §2.5.3 (p. 53) picks the violated basic
  variable **closest** to its violated bound -- least-violated, not most --
  and uses the short-row criterion only as a tie-break. So neither of Primo's
  two leaving policies is MathSAT's: our default inverts its primary key and
  our opt-in promotes its tie-break. Note the version caveat before treating
  King as simply wrong: the thesis is Dec 2009 and describes MathSAT **4**,
  while King (2014) attributes most-violated to MathSAT**5**, so the rule may
  have changed between them and the error may be only in our conflating the
  two. Details, and the resulting experiment, in
  [](griggio-mathsat-thesis.md).
- **The tableau is stored explicitly, and fill-in growth is not addressed.**
  Section 2.2.7 describes a sparse matrix with each nonzero entry linked into
  both a row list and a column list. Searching the thesis for fill-in, density,
  Markowitz or refactorization finds only the description of the sparse format
  itself -- there is no mechanism for controlling densification during search.
  So the fill-in problem measured in [](../qf-lra-heuristics.md)'s section 6 is
  not one this literature offers a ready answer to.

**Erling D. Andersen, Knud D. Andersen. "Presolving in linear programming."
Mathematical Programming 71 (1995), 221-245.**
<https://link.springer.com/article/10.1007/BF01586000>
The canonical reference for LP presolve, including removal of fixed
variables -- the prior art for heuristics-note item H7, whose header
previously called it "the standard eliminate-fixed-variables step" with no
citation at all. Note the difference in setting, which is the whole reason H7
did not pay: presolve removes fixed variables **once, before solving**, from a
problem whose bounds are all known. In DPLL(T) a variable becomes fixed
*during* search, when two bounds asserted at some decision level happen to
coincide, and must be restored on backtracking. Paper not read in full; cited
for the technique's provenance, not for a result.

**Harald Rueß, Natarajan Shankar. "Solving Linear Arithmetic Constraints."
CSL Technical Report CSL-SRI-04-01, SRI International, 15 January 2004.**
*(Read 2026-08-01. This entry previously characterised the paper from how
Dutertre & de Moura cite it; the corrections below are what reading it
changed.)*

It is reference [15] of the Dutertre & de Moura technical report, cited
three times: for the incremental-Simplex family generally ("Tools based on
this approach include our own tools, Yices and Simplics"), for *zero
variables* in explanations, and for the *zero-detection procedure* used on
disequalities. It extends **Nelson's** simplex method with a better
treatment of restricted (non-negative slack) versus unrestricted variables,
a way to add equalities *and disequalities* to a tableau, optimised equality
propagation, and proof/model generation.

**Correction: the backtracking design Primo used to have is Nelson's, not
this paper's.** This entry previously said Primo had "independently
re-invented" Rueß & Shankar's design. That overreached. Their related-work
section attributes it elsewhere: of Nelson's method, "**Retraction of
assertions is carried out by recording and undoing pivoting steps**" -- which
is precisely what Primo did until item [H9](#h9). They
also note Cassowary "introduces a simple and elegant mechanism for retracting
assertions". So the lineage of the scheme Primo removed runs to Nelson, and
this paper is where that characterisation is written down rather than where
the design originates.

**What the zero test actually is (Chapter 6, "Detecting and Propagating
Implicit Equalities").** A *zero-tableau* `Z` collects the slack variables
explicitly equal to 0. A variable is *implicitly* equal to 0 when the
constraints force it there without any equality having been asserted, and the
chapter's contribution is finding those "without actually maximizing each
variable" -- via a stratification order on the non-zero variables and the
iterated `zbnd*` construction. Detecting implicit equalities is what makes
two things possible: deciding a disequality `a != 0` (you must know whether
`a = 0` is implied), and **propagating derived equalities to other decision
procedures**, which their introduction lists as a requirement: "Equality
information derived by the inequality solver must be propagated to other
inference procedures."

**What Primo does instead, and where that stops working.** Primo implements
none of this, deliberately. Real equalities are lowered to two inequalities
in preprocessing (`lower_real_equalities`), so a disequality becomes ordinary
Boolean structure and CDCL performs the case split -- no zero test needed.
`assert_lra_atom_bound` keeps a guard that throws on a negated `Eq` atom, as
a backstop for one arriving unlowered.

That works because **QF_LRA is a single theory**. It stops working for
QF_UFLRA, which `check_sat.cpp` currently rejects outright ("combined
equality and arithmetic (QF_UFLRA) is not supported ... would need real
Nelson-Oppen"). A Nelson-Oppen combination has to exchange *implied*
equalities between the arithmetic and equality solvers, and Primo has no
mechanism for deriving them -- there is no equality propagation out of
`LraTableau` at all. **Chapter 6 is the algorithm that gap needs**, so this
paper is worth reading against the Nelson-Oppen work rather than the QF_LRA
work.

**One aside relevant to [H3](#h3).** On Hentenryck &
Graf's SF2 normal form they observe that "lexicographic pivoting is an
alternative to Bland's rule for avoiding pivoting cycles, but can be more
expensive, particularly in situations where implicit equalities are rare" --
a third anti-cycling option beside the Bland fallback Primo uses.

**General engineering (presolve, scaling, degeneracy, refactorization).**
Robert Bixby, "Solving Real-World Linear Programs: A Decade and More of
Progress" (Operations Research 50(1), 2002) — a implementor's-eye survey of
what actually made Simplex fast in practice over ~15 years of commercial LP
codes (presolve, scaling, LU refactorization, dual vs. primal Simplex,
degeneracy handling), useful as an engineering checklist independent of any
single algorithm choice. Istvan Maros, "Computational Techniques of the
Simplex Method" (Springer, 2003) is the standard book-length reference for
the same territory if a deeper dive is ever warranted.

**Exact-arithmetic factorization and coefficient growth.** The reason a
factored basis is not obviously a win in exact arithmetic: LU factors suffer
coefficient growth, so entries become large rationals and a sparser
representation can still lose to a denser one. Integer-preserving
(Bareiss-style) factorization exists to address exactly this. Relevant if D4
in [](../qf-lra-heuristics.md) section 6.5 is ever pursued:

- "Expediting exact linear programming solvers via integer preserving
  factorization", Operations Research Letters (2025) --
  <https://www.sciencedirect.com/science/article/abs/pii/S0167637725001646>.
  Reports SoPlex with faster exact integer factorizations at 3.33x (average)
  and 1.60x (geometric mean) over baseline. Not read in full; found while
  checking whether refactorization transfers.
- Applegate, Cook, Dash, Espinoza, "Exact solutions to linear programming
  problems" -- <https://www.math.uwaterloo.ca/~bico/papers/exact_simplex.pdf>.
  The QSopt_ex approach: use floating point for speed and confine exact
  rational work to what is needed for correctness. Same philosophy as
  Monniaux's CAV 2009 contribution, from the LP side.

**Why LU refactorization does not transfer to Primo's representation.** In the
*revised* simplex method the basis is held as an LU factorization that is
updated per pivot (Forrest-Tomlin, Bartels-Golub); those updates accumulate
both fill-in and floating-point error beyond what a fresh factorization would
need, which is exactly what periodic refactorization discards. Primo, like
every Simplex-for-DPLL(T) implementation King describes, stores the **explicit
tableau** instead. For a given basic/non-basic partition that tableau is
`B^-1 N`, which is *uniquely determined*: recomputing it from the original rows
yields the identical matrix, with identical density. There is no
representational slack to reclaim, so "refactor periodically to discard
accumulated fill-in" is void here -- the density measured in
[](../qf-lra-heuristics.md) section 6.2 is a property of the basis reached, not
of the pivot path taken to it. Reducing it therefore means either reaching
sparser bases or changing representation to revised simplex, and only the
latter makes refactorization meaningful.

## Alternative (non-Simplex) decision procedures

**Konstantin Korovin, Nestan Tsiskaridze, Andrei Voronkov. "Conflict
Resolution." CP 2009.**
A fundamentally different approach to solving conjunctions of linear
rational/real constraints: a conflict-driven method (in the CDCL sense, not
Simplex pivoting) that resolves "conflicting" bound derivations directly,
producing explanations more naturally suited to DPLL(T) integration than a
Simplex tableau's implicit ones. Worth reading before committing to
Simplex if proof/explanation quality (analogous to the
[[fast-congruence-closure]] Explain machinery) turns out to matter for LRA
the way it did for EUF.
https://link.springer.com/chapter/10.1007/978-3-642-04244-7_41

**Jasper Nalbach, Valentin Promies, Erika Ábrahám, Paul Kobialka. "FMplex: A
Novel Method for Solving Linear Real Arithmetic Problems." 2023 (extended
from GandALF 2023; also circulated as "FMplex: Exploring a Bridge between
Fourier-Motzkin and Simplex," LMCS).**
Revisits Fourier-Motzkin elimination — usually dismissed as worse than
Simplex — and shows that case-splitting during elimination drops its
worst-case complexity from doubly to singly exponential, with a structural
correspondence to Simplex pivoting close enough to motivate the name. Recent
(2023) and not yet a mainstream SMT-solver default; a candidate to watch
rather than to implement first.
https://arxiv.org/abs/2310.00995

**Local Search For SMT On Linear and Multilinear Real Arithmetic.
arXiv:2303.06676 (2023).**
A local-search theory solver for LRA, sitting outside the DPLL(T)/Simplex
lineage entirely. Relevant primarily as a fallback/portfolio strategy for
instances where systematic Simplex search struggles, not as a primary
decision procedure.
https://arxiv.org/pdf/2303.06676

## QF_LRA-relevant despite an LIA framing

**Martin Bromberger, Christoph Weidenbach et al. "Computing a Complete Basis
for Equalities Implied by a System of LRA Constraints" (SMT@IJCAR 2016
workshop paper).**
LRA-specific (not the LIA cube-test papers by the same authors, which are
out of scope below): derives all equalities implied by a system of LRA
constraints. Could feed Primo's equality-propagation path (cf. `--euf-prop`
/ eq-bridge machinery) once arithmetic and EUF interact.
https://pure.mpg.de/rest/items/item_2496284_6/component/file_2496283/content

## Out of scope for QF_LRA (LIA/mixed/quantified — for later, if ever)

- Bromberger & Weidenbach, "Fast Cube Tests for LIA Constraint Solving"
  (IJCAR 2016) / "New Techniques for Linear Arithmetic: Cubes and
  Equalities" (FMSD 2017) — the cube-test method itself is for integer
  points, not QF_LRA's rational/real domain.
- Jovanović & de Moura, "Cutting to the Chase: Solving Linear Integer
  Arithmetic" (CADE 2011) — LIA successor line to the Simplex-for-DPLL(T)
  family (used in Yices2); relevant only if scope ever extends to
  mixed/integer.
- King, Barrett, Tinelli's MIP branch-and-cut usage (see above) is only
  in scope for its pure-LP mode; its integer-programming half is LIA/mixed.
- Cimatti, Griggio, Sebastiani, "Efficient Interpolant Generation in
  Satisfiability Modulo Linear Arithmetic" (TACAS 2008) — only relevant if
  Primo grows an interpolation feature, and would need re-checking against
  the QF_LRA fragment specifically.
