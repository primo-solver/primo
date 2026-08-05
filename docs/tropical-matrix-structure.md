# The tropical-matrix family: what the formulas actually say

Structural analysis of the `tropical-matrix` QF_LRA family, written 2026-08-04
after reading the benchmark source rather than inferring shape from solver
counters. It exists because every prior attempt on this family
([](qf-lra-heuristics.md)'s T1, the polarity follow-up, and the
row-propagation experiment) treated the instances as an opaque "hard family"
and tuned solver knobs against them. Reading the encoding changes what looks
promising, and rules out one idea that looked obviously right.

All measurements below are **local** (workstation, Release `build/primo`) and
are diagnostic, not benchmark claims. The A/B in [](#unguarding) is the one
that matters and is recorded in `docs/run-log.json`.

(encoding)=
## The encoding is a tropical (arctic) semiring, not general arithmetic

The benchmarks' own `:source` header says they come from **automated program
termination analysis**, submitted by Johannes Waldmann in 2012, and that they
should be satisfiable. The body confirms a specific and very regular encoding.

Every quantity is a **pair of SMT variables**:

```text
(declare-fun f12m () Bool)     ; "this entry is -infinity"
(declare-fun f12c () Real)     ; its finite value, meaningful only when f12m is false
```

On `constraint-199552.smt2` the pairing is exact: 954 `m`/`c` pairs (36 `f*`
and 918 `l*`), 1,908 declarations, no `ite`, and -- worth stating because a
careless `grep` suggests otherwise -- **zero multiplications**. `(*` counted
with a basic regular expression matches "zero or more open parens"; the true
count of `(* ` is 0. The logic really is linear.

`f*` variables are the free unknowns (the matrix coefficients being solved
for). `l*` variables are **derived**: each is one tropical operation applied to
two earlier quantities. Tropical multiplication is ordinary addition, and it
appears as a matched pair of definitions:

```text
(= l51m (or f12m l3m))                    ; -infinity is absorbing  -- unconditional
(or l51m (= l51c (+ f12c l3c)))           ; the value               -- guarded
```

There are 468 of each on this instance. Comparisons take the dual form, where
the guard is a negative conjunct instead of a positive disjunct:

```text
(or l717m (and (not l870m) (>= l717c l870c)))
```

So the family is **a DAG of definitions, not a constraint system**: 918 derived
quantities over 36 genuinely free real unknowns, plus a Boolean layer deciding
which of them are `-infinity`.

(dont-care)=
## The `c` variables are genuine don't-cares under their own flag

The structural property that makes the encoding work: whenever `lKm` is true,
`lKc`'s value is irrelevant *everywhere in the formula*, not just in its own
definition.

Verified rather than assumed, with a checker that inlines the `let` bindings
and walks the term with an explicit stack, treating both guard polarities --
a positive sibling disjunct `lKm` inside an `or`, and a negative sibling
conjunct `(not lKm)` inside an `and`:

| quantity | value |
|---|---:|
| distinct `c` variables occurring | 954 |
| total occurrences | 5,106 |
| occurrences protected by their own flag | 3,774 (73.9%) |
| variables with **every** occurrence protected | 774 |
| variables with some unprotected occurrence | 180 (36 `f*` + 144 `l*`) |
| **guarded arithmetic definitions** | **468** |
| **of those, fully protected** | **468 (all)** |

Handling only one guard polarity gets this badly wrong -- it reports 0 of 468
protected -- which is worth recording as a trap for anyone repeating the
analysis.

So for all 468 definitions, `(or lKm (= lKc e))` may soundly be replaced by the
unconditional `(= lKc e)`: any model of the original with `lKm` true can be
repaired by setting `lKc := e`, since nothing else observes `lKc` in that case.

(unguarding)=
## Unguarding is sound, and 27x slower -- the guards are load-bearing

That soundness argument suggests an obvious preprocessing win: drop the 468
guards, turning Boolean case splits into unconditional definitions that
Gaussian elimination could substitute away. **Measured, it is a large
regression.** Same binary, same workstation, load 2.1 both times:

| version | answer | time | decisions | conflicts | pivots | pivot term ops |
|---|---|---:|---:|---:|---:|---:|
| original (guarded), `--guarded-real-equality-lowering` | sat | **4.13s** | 15,766 | 93 | 7,679 | 128M |
| 468 definitions unguarded | sat | **112s** | 125,603 | 13,359 | 526,566 | 3,831M |

Both answer `sat`, which is the transformation's soundness showing up
empirically. But it is **27x slower**, with 68x the pivots.

The reason is the point of the whole encoding, and it inverts the intuition:
**the guard is what lets the solver avoid the arithmetic.** Setting `lKm` true
discharges the definition at the Boolean level for free. Unguarding forces all
468 equations permanently into the tableau, so every one of them must be
maintained whether or not that entry is `-infinity`. The formulas are already
well-encoded for a lazy SMT solver; the case splits are cheaper than the rows
they avoid.

This is the most useful negative result in this note. It rules out the entire
family of "simplify the guards away" preprocessing ideas, T1's distribution
being the one variant that helps precisely because it *keeps* the guard.

(fill-in)=
## Where the time actually goes: 42x fill-in

Counters from the 4.13s solved run (`constraint-199552`, the only file Primo
solves at 60s):

| counter | value |
|---|---:|
| `:lra.rows` | 1,440 |
| `:lra.row-nonzeros` | 3,519 |
| initial average row length | **2.44** |
| `:lra.pivot-row-updates` / `:lra.pivots` | 164 rows touched per pivot |
| `:lra.pivot-term-ops` / `:lra.pivot-row-updates` | **~102 terms per row update** |
| implied fill-in | **~42x** |

Rows start at 2.44 nonzeros and are ~102 terms long during search. That is the
same diagnosis [](qf-lra-heuristics.md) section 6.2 reached globally, in an
extreme form -- and here the *cause* is legible from the encoding rather than
inferred. Pivoting composes the definition DAG: substituting one definition
into another walks the chain back toward the 36 free `f` variables, so a
fully-composed row spans them all. The dense limit is structural, not
accidental.

Also worth noting: `:lra.fixed-variables 0` and `:lra.elimination-pivots 0` --
**Primo's existing elimination machinery does nothing at all on this family.**

(leads)=
## What this suggests, ranked

Neither is implemented; both are recorded here rather than in
[](qf-lra-heuristics.md)'s item list until one is actually tried.

**L1 -- Decide the `m` skeleton before the arithmetic (cheap).** The Boolean
layer is *self-contained*: 468 unconditional definitions
`(= lKm (or lIm lJm))` over 36 free `f*m` inputs, referring to no arithmetic
whatsoever. Which guards fire -- hence which arithmetic is even relevant -- is
fully determined by those 36 Booleans. Yet Primo's decision order interleaves
`m` variables with theory atoms. The solved run shows 15,766 decisions against
only **93 conflicts**: the search is nearly conflict-free, it is just walking,
and it is doing tableau work along the way that the right `m` assignment would
have made unnecessary. A branching-order preference for `m` variables over
arithmetic atoms is a small change with a clear rationale. Note this is
*variable ordering*, distinct from the *phase* selection already tried in the
polarity follow-up -- and the phase result is corroborating evidence, since
`--theory-phase positive` helped by setting `m` literals true, i.e. by firing
guards.

**The infrastructure already exists, and currently does the exact opposite.**
`--lra-theory-decisions` (item 8) supplies decision literals from
`cb_decide()` instead of leaving them to VSIDS -- but its loop skips every
atom with `!prepared_atom.is_arithmetic`, so it decides *arithmetic* atoms
first, in round-robin, and reaches the Boolean flags only once every
arithmetic atom is assigned. On this family that is precisely the worst
possible order, and it is catastrophic in measurement:

| config on `constraint-199552` | result |
|---|---|
| `--guarded-real-equality-lowering` | **3.5s**, sat |
| the same plus `--lra-theory-decisions` | **timeout at 90s**, after 9,185,550 theory decisions |

9.2M decisions and no answer, against 3.5s. This is a falsification test of the
structural reading in [](#dont-care) that came out the predicted way: forcing
the arithmetic to be decided before the flags that make it relevant is the
pathological case. It also means L1 is a *small* change rather than a new
mechanism -- it is the same `cb_decide()` hook with the filter inverted, and
the existing option is a ready-made control to A/B against.

**L3 -- Extend the pure-theory-filter to LRA atoms (cheapest of the three).**
Primo already has a polarity-based relevance filter: `--pure-theory-filter`
omits a theory literal from the theory assignment when the atom occurs only
with the opposite polarity, on the argument that such an assignment cannot be
what satisfies the Boolean abstraction ([](euf.md), from Sebastiani's survey
§6.10). It is **implemented for QF_UF only** -- equality atoms, Boolean
predicate applications, Boolean constants -- and is inert here:

| counter on `constraint-199552` | value |
|---|---:|
| `:euf.theory-atoms` | 3,846 |
| `:euf.pure-atoms.positive` | **2,892 (75%)** |
| `:euf.pure-atoms.negative` | 0 |
| `:euf.pure-filter.enabled` | **0** |
| `:euf.pure-literals.filtered` | **0** |

So the polarity analysis already runs on this family and already finds that
**three quarters of the theory atoms are pure-positive** -- and then filters
none of them, because the mechanism does not apply to LRA. Ablating
`--pure-theory-filter=on|off` confirms it: 4.152s against 4.150s, no effect
whatsoever.

This is a much smaller step than L2. It is the same soundness argument as the
EUF case (a pure-positive atom assigned false cannot be what satisfies any
clause, so its consistency need not be checked), reusing analysis that is
already computed and already reported. It is a *static polarity* approximation
of L2's *dynamic assignment* relevance -- weaker, but nearly free. The
literature caveat carries over unchanged and is the thing to get right:
filtered literals must never be used as theory-deduced premises.

**L2 -- Relevancy propagation (literature-backed, larger).** In DPLL(T) the SAT
solver assigns *every* variable, including theory atoms sitting in clauses that
are already satisfied by their guard; Primo then asserts those bounds into the
tableau. On this family that is precisely the wasted arithmetic the unguarding
experiment made visible in extreme form. z3's answer is **relevancy
propagation** (de Moura and Bjorner) -- track which atoms are relevant to the
current partial assignment and never assert the irrelevant ones. This is absent
from Primo and from [](papers/lra-related-work.md) entirely, and it targets
exactly the structure documented above. It is the most principled of the three
and much the largest piece of work; it should not be started before L1 and L3
have been tried, and it needs its own paper note first. L3 is its cheap static
approximation and is the honest first step toward it: if filtering on polarity
alone moves this family, dynamic relevance is worth the investment; if it does
not, that is evidence about the family before a large mechanism is built.

**Explicitly not suggested:** any further guard-simplification preprocessing
(closed by [](#unguarding)), and any attempt to exploit the DAG by eager
substitution -- the definitions are guarded, so substitution is
assignment-dependent, which is what makes L2 rather than a preprocessing pass
the right framing.
