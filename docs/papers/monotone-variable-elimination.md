# Monotone (one-sided) variable elimination: what to cite

## Why this note exists

The rule Primo implements in `eliminate_monotone_variables` is elementary
enough that it clearly is not new, but it sits between three literatures and
is easy to attribute to the wrong one. This note records what was actually
checked, and -- more importantly -- what was *not*, so nobody cites a paper
this project has not read.

**The rule.** If, after accounting for Boolean polarity, every atom mentioning
a real variable `x` pushes `x` the same direction, then some extreme value of
`x` satisfies all of them at once, so every such atom can be replaced by the
constant its own occurrence needs.

## 1. The mathematical fact: Fourier-Motzkin, single-sign variable

Eliminating a variable by Fourier-Motzkin pairs each lower bound with each
upper bound. **A variable occurring with only one sign has no pairs to form**,
so the elimination step produces *no* new constraints and simply deletes every
constraint mentioning it. This is inherent to the method rather than anybody's
contribution, and it is the core of what Primo does.

Standard textbook treatment: Schrijver, *Theory of Linear and Integer
Programming* (Wiley, 1986), the Fourier-Motzkin chapter. Already in this
project's citation graph -- [](griggio-mathsat-thesis.md) cites the same book
(as `[Sch86]`) for branch-and-bound.

**Status: confident.** This is the part the implementation actually rests on,
and it is a textbook fact rather than a result needing verification.

## 2. LP presolve: read, and it is NOT this rule

The obvious guess was that this is a named LP-presolve reduction, and the
foundational survey is:

> Erling D. Andersen and Knud D. Andersen. "Presolving in linear
> programming." *Mathematical Programming* 71(2):221-245, 1995.
> Local copy `docs/papers/Presolving.pdf` (untracked).

**Read 2026-08-05. It does not contain this rule, and an earlier draft of this
note guessing that it did was wrong.** Recorded because the guess is a natural
one and someone will make it again.

What the paper's column reductions actually are:

- **(xi) A dominated column** (p. 228) is a *dual* reduction. It bounds the
  optimal Lagrange multipliers `y*` from column singletons, uses them to sign
  the reduced cost `z_j*`, and then fixes `x_j` at a bound. Its condition
  `c_j - d_j > 0 ∨ c_j - e_j < 0` is stated in terms of the objective
  coefficients. **(xii) weakly dominated** and **(xiii) forcing column** are
  the same family. Primo has no objective -- this is a pure feasibility
  problem -- so none of these apply.
- **(vi) A free column singleton** (p. 225) is the closest in spirit: a
  variable occurring in exactly one row and free of finite bounds is
  *substituted out* via `x_j = (b_i - Σ a_ip x_p) / a_ik`. Two differences,
  both essential: it needs a **singleton** column, whereas the variables this
  pass eliminates occur in many rows; and it *substitutes*, keeping the
  equation, whereas this pass *deletes* every constraint mentioning the
  variable.
- **(x) forcing / dominated constraints** (§3.3) are row-side: row activity
  bounds `g_i, h_i` computed from the variables' simple bounds decide that a
  constraint cannot be violated, so it is dropped. That is redundancy given
  bounds. A constraint discharged by this pass is *not* redundant in that
  sense -- it can be violated -- it is merely satisfiable by pushing one
  variable far enough.

So the paper is a survey of presolve for LP *optimization*, and every column
rule in it is driven by the objective. The primal, objective-free observation
this pass uses has no counterpart there.

### What the paper is genuinely the right citation for

The **presolve/postsolve framing** (§4). Reductions are recorded on a stack
during presolve, and a "postsolve" (their term, credited to Tomlin and Welch)
walks them back to recover a solution to the original problem, since "the
optimal solution to the reduced problem is not feasible for the original
problem" in general. Primo's `apply_monotone_eliminations` is a postsolve in
exactly that sense, and this is the standard name for what it does.

One instructive contrast. Their postsolve undoes reductions in **reverse
order**, because their reductions are interdependent. Primo's need no order at
all (see [](../preprocessing.md)), because a discharged constraint is repaired
by whichever of its eliminated variables is valued last. That difference is
worth knowing rather than assuming the LP machinery carries over.

## 3. The SMT line that looks like this and is not: unconstrained variables

The technique an SMT reader is most likely to assume this is, and it is a
**different rule**.

> Martin Jonáš and Jan Strejček. "On Simplification of Formulas with
> Unconstrained Variables and Quantifiers." SAT 2017.
> <https://www.fi.muni.cz/~xstrejc/publications/sat2017preprint.pdf>

**Status: read** (abstract, Section 1, and Section 4's Definition 3 /
Theorem 2, 2026-08-05).

That paper defines an *unconstrained variable* as one that **occurs only once
in the formula**, so it can be set freely without disturbing anything else;
terms built from such variables are *unconstrained terms* and can be replaced
by a fresh variable. Its own contribution generalizes this to **partially
constrained terms** via `C`-interchangeability (Definition 3): two terms are
interchangeable if, for every valuation of the constrained variables `C`, they
range over the same value set. Theorem 2's precondition is
`vars(s) ∩ vars(ϕ) ⊆ C`, which the proof uses to argue that every variable of
`U` "appears either only in `t` or only in `s` and not in any other part of the
formula".

**That locality precondition is exactly what Primo's rule does not have.** The
variables this pass eliminates occur *many* times, spread across the whole
formula -- the motivating `motzkin_*` multipliers occur repeatedly -- and are
eliminable not because they are local but because every occurrence agrees on
which direction it pushes them. So the two rules overlap only in the
degenerate single-occurrence case, and this pass is not an instance of theirs.

The paper credits the original unconstrained-variable technique to two
independent PhD theses, both worth knowing as the origin of that line:

- Roberto Bruttomesso. *RTL Verification: From SAT to SMT(BV)*. PhD thesis,
  University of Trento, 2008.
- Robert Brummayer. *Efficient SMT Solving for Bit Vectors and the Extensional
  Theory of Arrays*. PhD thesis, Johannes Kepler University Linz, 2010.

All three are bit-vector-centric; the SAT 2017 evaluation is on bit-vector
formulas. None of them is a source for the arithmetic polarity argument.

**Status of the two theses: NOT READ.** Cited here for provenance of the
unconstrained-variable line only.

## 4. The contrast that keeps this sound: the pure-literal rule

The propositional pure-literal rule (Davis and Putnam, 1960) is the thing this
rule is *not*, and confusing the two produces an unsound solver. Setting a
pure-positive **theory atom** to true and deleting its clause turns sat into
unsat -- see [](../preprocessing.md)'s pure-literal section for the verified
counterexample. Syntactic purity is not semantic independence once atoms have
theory content.

Primo's rule avoids that trap by being a statement about **variables**, not
atoms: it never forces an atom's value on the strength of that atom's own
polarity alone, but on the strength of a whole-formula argument about one
variable's direction.

## What Primo claims

The implemented subset, stated the way AGENTS.md asks:

- **Implemented:** one-sided detection over Le/Lt atoms with polarity tracked
  through `Not`/`Implies` (the input is not in NNF), constant folding of the
  discharged atoms, and model reconstruction for the eliminated variables.
- **Deviation from the LP-presolve setting:** formulas are not conjunctive, so
  direction is computed per *occurrence polarity* rather than per column sign.
- **Limitations:** only 0-ary Real `Constant` leaves qualify; an atom with any
  other leaf disqualifies every variable in it, because model reconstruction
  must be able to evaluate the rest of the row.
- **Not claimed:** this is not an implementation of any specific published
  algorithm, and the code comments say so. It is the Fourier-Motzkin
  single-sign observation applied to a non-conjunctive formula, with the
  bookkeeping that makes that safe.

## Open citation work

Andersen & Andersen is now read and settled: it supplies the postsolve
framing and nothing more. No further citation work is outstanding. If the rule
does appear somewhere under a name, the remaining places to look are the
constraint-programming literature on monotone/one-directional variables and
Williams' presolve work ([24] in that paper), which is where its
dominated-column procedure is credited.
