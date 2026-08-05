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

## 2. The closest named technique: LP presolve

In linear-programming presolve the same observation appears as a **column**
reduction. A column whose entries all have one sign, with no opposing bound,
makes every row containing it redundant for feasibility. The standard
vocabulary is *dominated columns*, *implied free variables*, and *forcing /
redundant constraints*.

The foundational reference is:

> Erling D. Andersen and Knud D. Andersen. "Presolving in linear programming."
> *Mathematical Programming* 71(2):221-245, 1995.

**Status: NOT READ. Do not cite it as an implementation source.** It was
identified through search, and its rule names are quoted here from secondary
descriptions. Before this project describes its pass as implementing an
Andersen-Andersen reduction, someone must fetch and read the paper and confirm
which named rule -- if any -- this is. Per AGENTS.md, a secondary summary is
not an acceptable substitute. Until then the honest description is "the same
observation that LP presolve exploits", not "we implement rule X of [AA95]".

A caveat that matters for the comparison, and is a reason not to lean on this
line too hard: LP presolve is stated for a *conjunctive* system. Primo's
formulas are not conjunctions, so the polarity bookkeeping in section 4 has no
counterpart there.

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

1. **Fetch and read Andersen & Andersen (1995)** and settle whether this is a
   named reduction there. Paywalled at Springer; a copy is needed.
2. If it is, restate section 2 as a real citation with the rule's name, and
   check whether their solution-retrieval ("postsolve") procedure matches
   Primo's model reconstruction -- postsolve for a dominated column ought to be
   the same computation, and if it differs that is worth understanding.
