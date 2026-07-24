# A Fast Linear-Arithmetic Solver for DPLL(T)

## Citation

Bruno Dutertre, Leonardo de Moura:  
A Fast Linear-Arithmetic Solver for DPLL(T).

## Role in Primo

This paper is the base algorithm for Primo's QF_LRA support. See
`docs/arithmetic.md` for the current implementation status and
`/home/user/.claude/plans/piped-wondering-fairy.md` for the full milestone plan.

## Implemented (Milestones 1-3b)

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
- **Section 4's propagation half ("deriving satisfied atoms"), Milestone
  3b (2026-07-24, done).** `PreparedLraTheory` was first changed to
  deduplicate slack variables by *exact* polynomial content (a
  `PolynomialKey`/`PolynomialKeyHash` lookup, `check_sat.cpp`) -- a
  prerequisite, since propagation needs multiple atoms to actually share a
  tableau variable, which essentially never happened under the old
  one-slack-per-atom scheme. `OnlineLraPropagator::notify_assignment_lit`
  then scans the other atoms on the just-asserted atom's slack (via a new
  `atoms_by_slack` index) after each successful Assert-Bound, and eagerly
  queues any now-entailed atom for `cb_propagate()` to deliver, using two
  new `LraTableau::upper_bound()`/`lower_bound()` read accessors compared
  against `true_threshold`/`false_threshold` (the same values
  `assert_lra_atom_bound` itself asserts). Each reason is exactly the
  entailing bound's own asserting literal (a 2-literal Horn clause) --
  simpler than EUF's propagation machinery throughout, since the
  entailment check is exact and O(atoms sharing a slack), not a heuristic
  candidate needing verification, so none of EUF's budget/backoff/
  candidate-queue apparatus was needed. Gated behind
  `CheckSatOptions::lra_propagation` (default on, `--lra-propagation`/
  `--no-lra-propagation`) purely as an A/B performance switch, unlike
  EUF's multi-mode `EufPropagationMode`.

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
- Slack variables are deduplicated by *exact* polynomial content only, not
  scale-invariant (`x<=5` and `2x<=10` still get separate slacks, so
  propagation can't relate them) -- a deliberate scope cut for Milestone
  3b, not an oversight.
- Propagation only relates atoms sharing the exact same slack -- the
  paper's stronger row-based/interval bound propagation across *different*
  variables via a basic row's children is a materially bigger technique,
  out of scope here.
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
- ~~LRA6: mid-search theory propagation~~ -- **done (2026-07-24)**, see
  above. Prioritized over Milestone 4 per real-benchmark evidence (a
  moderate-size real formula made genuine but very slow progress with zero
  theory propagations before this milestone). A 500-case fuzz merge-gate
  found and fixed a real bug (`OnlineLraPropagator` clearing valid
  propagation reasons for still-assigned literals on backtrack, causing a
  CaDiCaL "reason clause must contain the propagated literal" abort) before
  landing clean. A quick 82-file diverse real-benchmark re-run showed no
  material aggregate change on that sample (same-slack propagation didn't
  move the previously-hardest families) -- see `docs/run-log.json` for the
  full picture; the mechanism's correctness is independently confirmed at
  the unit-test level (propagation measurably avoids decisions on hand-
  built cases).
