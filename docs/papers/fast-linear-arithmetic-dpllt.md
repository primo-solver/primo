# A Fast Linear-Arithmetic Solver for DPLL(T)

## Citation

Bruno Dutertre, Leonardo de Moura:  
A Fast Linear-Arithmetic Solver for DPLL(T).

## Role in Primo

This paper is the base algorithm for Primo's QF_LRA support. See
`docs/arithmetic.md` for the current implementation status and
`/home/user/.claude/plans/piped-wondering-fairy.md` for the full milestone plan.

## Implemented (Milestone 1)

- **Section 2** (normalized linear terms/atoms): `LinearPolynomial`
  (`src/theory/arithmetic/linear_polynomial.{hpp,cpp}`) and the canonical
  "t ~ c" atom form (`lra_atom.{hpp,cpp}`).
- **Section 3** (Assert-Bound, Check, Pivot) and **3.2** (conflict
  explanation via the row coefficients and stuck variables' binding
  bounds -- a Farkas-certificate-style argument): `LraTableau`
  (`src/theory/arithmetic/lra_tableau.{hpp,cpp}`). Bland's rule is used
  for both violated-variable and entering-variable selection, for the
  paper's termination guarantee -- not for speed; see
  `docs/papers/lra-related-work.md` for faster pivoting-rule alternatives
  a later milestone could adopt.
- **Section 3.1's infinitesimal-delta trick** for exact strict-inequality
  handling: `DeltaRational` (q + k*delta, lexicographically ordered), no
  floating point anywhere.
- **DPLL(T) integration** (`check_sat.cpp`'s `solve_qf_lra`): currently the
  *lazy* shape only (rebuild the tableau from the full SAT assignment each
  round), mirroring how EUF's `Lazy` engine works via `find_theory_conflict`
  -- not yet the paper's fully incremental, trail-based integration (that's
  Milestone 2), and not yet mid-search theory propagation via a CaDiCaL
  external propagator (Milestone 3, mirroring `OnlineEufPropagator`).

## Implemented subset vs. deviations vs. limitations

- No backtracking/incrementality within a single theory check yet (batch
  only -- a fresh tableau every SAT round). The paper's backtracking-friendly
  tableau design (referenced in Section 4) is Milestone 2.
- No anti-cycling beyond Bland's rule itself, no alternative pivoting
  rules, no sum-of-infeasibilities-style global objective (see
  `docs/papers/lra-related-work.md`'s SOI-Simplex entry) -- Milestone 4.
- Disequality (a negated equality atom) is explicitly rejected rather than
  handled via case-splitting. The paper doesn't need to address this
  directly (its theory-atom framing assumes DPLL(T) delivers each atom's
  truth value directly), but a real implementation needs *some* strategy
  for it before general formulas work; deferred rather than guessed at.
- One slack variable per *atom*, not deduplicated by polynomial content --
  sound (see `LraTableau`'s empty-row handling) but not the most efficient
  possible encoding; not something the paper prescribes either way.

## Milestones

- ~~LRA0: linear polynomial representation~~ -- done.
- ~~LRA1: normalization of arithmetic atoms~~ -- done.
- ~~LRA2: tableau scaffold~~ -- done (batch/non-incremental).
- ~~LRA3: feasibility checking~~ -- done (Check/Pivot).
- ~~LRA4: conflict explanations~~ -- done (Farkas-certificate style).
- LRA5: backtracking and propagation -- in progress (Milestones 2-3 of the
  plan: incremental trail-based tableau, then `OnlineLraPropagator`).
