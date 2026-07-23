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

## Direct extensions of Simplex-for-DPLL(T)

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

**Tim King, Clark Barrett, Cesare Tinelli. "Leveraging Linear and Mixed
Integer Programming for SMT." FMCAD 2014.**
Replaces/augments the incremental Simplex theory solver with calls to an
external LP/MIP solver. The LP-only mode is in scope for QF_LRA (trades
incrementality for the maturity of industrial LP engines, worth a look if a
from-scratch tableau proves to be a maintenance burden); the MIP
branch-and-cut half is LIA/mixed and out of scope for now (see below).
https://theory.stanford.edu/~barrett/pubs/KBT14.pdf

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
