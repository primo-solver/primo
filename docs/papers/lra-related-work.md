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
  reference given MathSAT's QF_LRA solver is Simplex-for-DPLL(T)-based; not
  yet located/fetched.

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
  rather than 32-bit, using `__int128` intermediates, which is an empirical
  question the H2 width ablation is meant to settle rather than a claim.
  *Independently corroborated:* profiling Primo before implementing it put
  ~42% of runtime in the allocator and ~12% in GMP copy/init against ~5% in
  real bignum arithmetic -- the same diagnosis the paper gives.
- **Section 2.1, atom canonicalization.** Inequalities are scaled to coprime
  integer coefficients and optionally negated so the leading coefficient is
  positive, "so that two inequalities constraining the same direction in
  space appear with the exact same left-hand side"; and "for each left hand
  side that is *not a variable*, a new variable is introduced". The second
  half is the source for roadmap item H1 (Primo currently introduces a slack
  for *every* distinct polynomial, including bare variables), the first half
  for the long-deferred scale-invariant polynomial matching (item 3).
  **Not yet implemented.**
- **Page 8, sparsity-ordered pivoting.** "We sort the variables ... according
  to the number of nonzero coefficients in the corresponding equation ... the
  pivoting steps are faster when the equation defining the basic variable to
  be pivoted has few nonzero coefficients", and likewise pre-sorting by
  "number of occurrences in equations". Stated there for the forced-pivoting
  phase, but the cost argument is the same one behind roadmap item H3's
  fill-in-aware entering-variable selection. **Not yet implemented.**

Clean-room note: implemented from this paper's prose (plus Knuth TAOCP Vol. 2
Sec. 4.5.1 for the rational arithmetic itself), not from any solver's source.
https://arxiv.org/abs/0904.3525

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

**General engineering (presolve, scaling, degeneracy, refactorization).**
Robert Bixby, "Solving Real-World Linear Programs: A Decade and More of
Progress" (Operations Research 50(1), 2002) — a implementor's-eye survey of
what actually made Simplex fast in practice over ~15 years of commercial LP
codes (presolve, scaling, LU refactorization, dual vs. primal Simplex,
degeneracy handling), useful as an engineering checklist independent of any
single algorithm choice. Istvan Maros, "Computational Techniques of the
Simplex Method" (Springer, 2003) is the standard book-length reference for
the same territory if a deeper dive is ever warranted.

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
