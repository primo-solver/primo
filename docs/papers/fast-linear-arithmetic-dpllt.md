# A Fast Linear-Arithmetic Solver for DPLL(T)

## Citation

Bruno Dutertre, Leonardo de Moura:  
A Fast Linear-Arithmetic Solver for DPLL(T).

## Role in Primo

This paper is the base algorithm for Primo's QF_LRA support. See
`docs/arithmetic.md` for the current implementation status and
`/home/user/.claude/plans/piped-wondering-fairy.md` for the full milestone plan.

## Implemented (Milestones 1-2)

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
- **Section 4 (backtracking)**: `LraTableau::push()`/`pop(checkpoint)`,
  genuinely trail-based (a `TrailEntry` per bound/assignment/pivot change,
  replayed backwards on pop) -- not full state snapshotting. Pivots are
  undone on pop, since a row's equation and the assignment it was derived
  under are coupled; see the class comment in `lra_tableau.hpp` for why
  this doesn't contradict the paper's point that pivot choice itself isn't
  tied to search depth.
- **DPLL(T) integration** (`check_sat.cpp`): `PreparedLraTheory` precomputes
  the tableau structure once (mirroring `PreparedFlatTheory`);
  `solve_qf_lra`/`find_incremental_lra_theory_conflict` copy that base
  tableau and assert bounds each round, mirroring how EUF's
  `find_incremental_theory_conflict` copies a precomputed `base_closure`
  rather than using `FlatCongruenceClosure`'s own push/pop for cross-round
  reuse. `LraTableau`'s push/pop exist and are tested, but aren't
  exercised by this wiring yet -- real trail-synced reuse across SAT
  decision levels (via `notify_new_decision_level`/`notify_backtrack`) is
  an online-propagator concern, Milestone 3's `OnlineLraPropagator`.

## Implemented subset vs. deviations vs. limitations

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
- `LraTableau`'s pivot-undo rebuilds the `occurs_in_` reverse index from
  scratch on every pop (O(tableau size)), rather than incrementally
  diffing it -- simplicity over speed for this milestone.

## Milestones

- ~~LRA0: linear polynomial representation~~ -- done.
- ~~LRA1: normalization of arithmetic atoms~~ -- done.
- ~~LRA2: tableau scaffold~~ -- done.
- ~~LRA3: feasibility checking~~ -- done (Check/Pivot).
- ~~LRA4: conflict explanations~~ -- done (Farkas-certificate style).
- ~~LRA5: backtracking~~ -- done (`push`/`pop`, trail-based).
- LRA6 (not in the original list, added once it became clear propagation
  needed its own milestone): mid-search theory propagation via
  `OnlineLraPropagator` -- Milestone 3 of the plan, not started.
