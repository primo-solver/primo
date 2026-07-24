# A Fast Linear-Arithmetic Solver for DPLL(T)

## Citation

Bruno Dutertre, Leonardo de Moura:  
A Fast Linear-Arithmetic Solver for DPLL(T).

## Role in Primo

This paper is the base algorithm for Primo's QF_LRA support. See
`docs/arithmetic.md` for the current implementation status and
`/home/user/.claude/plans/piped-wondering-fairy.md` for the full milestone plan.

## Implemented (Milestones 1-3a)

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
  the tableau structure once (mirroring `PreparedFlatTheory`).
  `OnlineLraPropagator` (Milestone 3a) is a trail-synced
  `CaDiCaL::ExternalPropagator`: one `LraTableau` per solve, kept in sync
  via `notify_new_decision_level`/`notify_backtrack` calling
  `LraTableau::push()`/`pop()` -- real cross-round trail reuse, not a
  fresh copy every round. `solve_qf_lra` always prefers this when
  available; `find_incremental_lra_theory_conflict` (copy the base tableau,
  reassert every round, mirroring EUF's `find_incremental_theory_conflict`'s
  own `base_closure`-copying style) is now only compiled as the fallback
  for builds without CaDiCaL's external-propagator API
  (`#if !PRIMO_HAVE_IPASIR`).
- **Deliberately not Section 4's propagation half yet.**
  `OnlineLraPropagator::cb_propagate()` always returns 0 -- per explicit
  user direction (2026-07-24), conflict detection alone is sufficient for
  DPLL(T) completeness; proactive bound propagation is a search-efficiency
  optimization layered on top, not a correctness requirement, so it's
  deferred as Milestone 3b (not scheduled) rather than bundled into "the
  online propagator milestone" by default. This mirrors a distinction
  EUF's own propagator already makes: `OnlineEufPropagator::cb_propagate()`
  returns 0 under `EufPropagationMode::Conflict` while still using the
  full trail-synced machinery `EufPropagationMode::Online` uses.

**Disequality (2026-07-24, done)**: rather than adding a case-split
mechanism to `LraTableau`/`OnlineLraPropagator` for negated equality atoms
(what the earlier planning discussion assumed would be needed), `Real`
equality is desugared to `(t1 <= t2) AND (t2 <= t1)` at parse time
(`Frontend::mk_theory_equality` in `check_sat.cpp`) -- valid because Real
is a total order. `NOT(t1=t2)` then reduces to ordinary Boolean structure
(`NOT(AND(Le,Le))`) that `BooleanClausifier` already clausifies correctly,
and CDCL branching over the two `Le` atoms does the case-split for free.
No paper section covers this directly (its theory-atom framing assumes
DPLL(T) delivers each atom's truth value directly, sidestepping the
question of how a compound relation like `=` decomposes at the Boolean
level) -- this is a frontend/encoding decision, not a deviation from the
tableau algorithm itself.

## Implemented subset vs. deviations vs. limitations

- No anti-cycling beyond Bland's rule itself, no alternative pivoting
  rules, no sum-of-infeasibilities-style global objective (see
  `docs/papers/lra-related-work.md`'s SOI-Simplex entry) -- Milestone 4.
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
- ~~LRA5: backtracking~~ -- done (`push`/`pop`, trail-based, and now
  actually exercised via `OnlineLraPropagator`'s trail sync, not just
  tested in isolation).
- LRA6 (not in the original list, added once it became clear propagation
  needed its own milestone): mid-search theory propagation via
  `OnlineLraPropagator::cb_propagate()` -- Milestone 3b of the plan,
  deferred/not scheduled (conflict detection, Milestone 3a, is done and is
  sufficient for completeness). **Priority note (2026-07-24):** a
  real-benchmark evaluation against `tmp/QF_LRA` (1753 real-world files,
  not committed) found genuine but very slow progress on a moderate-size
  real formula without ever finishing -- not a completeness bug, but
  evidence this milestone matters for practical performance well before
  Milestone 4's pivoting-rule/hardening work does.
