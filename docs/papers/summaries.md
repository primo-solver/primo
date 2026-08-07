# Paper summaries

Short summaries of papers this project has **actually read**, so their content
is available without re-reading the source and without opening a full design
note for every one.

## Where this sits

Three tiers, and the difference matters when deciding what a claim can rest on:

| tier | what it is | example |
|---|---|---|
| **Deep note** — `docs/papers/<paper>.md` | Section map plus the implementation checklist AGENTS.md requires before implementing from a paper | [](fast-linear-arithmetic-dpllt.md), [](griggio-mathsat-thesis.md) |
| **This file** | A paragraph or two per paper, read start to finish or in the parts that matter, enough to know what it does and does not contain | below |
| **[](lra-related-work.md)** | Papers scoped from **abstracts and search results only**, explicitly not read | that file's own header says so |

Rules for entries here:

- Say **what was read** and when. "Read" means the source, not a summary of it.
- Say what the paper does **not** cover, when that is what someone will
  actually need. A negative result about a paper's scope is often the useful
  part -- it stops the next person re-deriving it.
- Do not put implementation details here that an implementation would depend
  on. If a paper reaches that point, promote it to a deep note.
- Reference PDFs stay untracked (`.gitignore` excludes `*.pdf`); only these
  summaries are committed.

(andersen-presolving)=
## Andersen & Andersen, "Presolving in Linear Programming" (1995)

> Erling D. Andersen and Knud D. Andersen. *Mathematical Programming*
> 71(2):221-245, 1995. Local copy `docs/papers/Presolving.pdf` (untracked).
> **Read 2026-08-05**: §3 (the reduction catalogue) and §4 (postsolve) in
> full; §6's NETLIB results skimmed.

A survey of the presolve phase of an LP solver -- the analysis done before
optimization to shrink the problem and to detect infeasibility or
unboundedness. Its stated goal is to reduce the size of the constraint matrix
`A` **without creating new nonzeros**, so general linear transformations are
excluded even where they would help; only the objective, right-hand side and
simple bounds may be modified. The procedure makes repeated passes until no
reduction fires.

§3 is the catalogue, and is the reason to open the paper. Roughly in order of
sophistication: empty rows and columns, infeasible and fixed variables,
singleton rows, **free column singletons** (a variable in exactly one row with
no finite bounds, substituted out via its own equation -- called out as
especially valuable because it removes a row *and* a column with no fill-in),
doubleton equations combined with column singletons, **implied free column
singletons** (bounds shown redundant, creating a free singleton), **forcing and
dominated constraints** (row activity bounds `g_i`, `h_i` decide a constraint
is unsatisfiable, forces all its variables to a bound, or is redundant), and
their duals **dominated / weakly dominated / forcing columns** (bounds on the
optimal Lagrange multipliers sign the reduced cost, fixing a variable at a
bound). Duplicate rows and columns close the list.

§4 is **postsolve**, and is the part Primo actually uses. Because "the optimal
solution to the reduced problem is not feasible for the original problem" in
general, reductions are pushed onto a stack during presolve and walked back
afterwards, each undone so that primal and dual feasibility are restored. The
term is credited to Tomlin and Welch; the alternative -- re-solving the
original from a hot start, as Williams did -- is rejected as too slow and as
incompatible with an interior-point method.

**What it does not contain, which is why this summary exists.** Every *column*
reduction in the paper is driven by the objective coefficients `c`. The paper
has no rule for the purely primal, objective-free case Primo cares about: a
variable whose occurrences all push one direction, making its constraints
dischargeable regardless of cost. An earlier guess that Primo's monotone
elimination was this paper's "dominated column" was **wrong**; see
[](monotone-variable-elimination.md) for the full comparison. What the paper
legitimately supplies is the presolve/postsolve vocabulary and the observation
that reduction order matters when reductions are interdependent.

(king-thesis)=
## King, "Effective Algorithms for the Satisfiability of Quantifier-Free Formulas Over Linear Real and Integer Arithmetic" (NYU, 2014)

> Tim King. PhD thesis, New York University, 2014. Local copy
> `docs/papers/king_tim.pdf` (untracked).
> **Read 2026-08-06**: Section 2.2.7 (Tableau) in full, including 2.2.7.1 Row
> Addition, its Figures 2.9 and 2.10, and 2.2.7.3 Complexity. The rest is
> cited elsewhere in this project from summaries only.

### Section 2.2.7 -- the tableau representation

The tableau is a sparse matrix storing only non-zero coefficients. Each entry
is labelled with both its row and its column variable, and is **a member of
two doubly linked lists** -- one for the non-zeros of its row, one for the
non-zeros of its column -- carrying forward and backward pointers for both.
Two arrays of size n hold the list heads. Iterating a row or a column costs
time proportional to its number of non-zeros.

King states three constant-time operations explicitly: maintaining each row's
and column's non-zero count; adding an entry known to be currently zero; and
**setting an entry to zero by unlinking it, "given a pointer to the entry"**.
That last clause is the whole point of the structure and the thing Primo
currently lacks.

**The detail that matters most when comparing to Primo, and is easy to miss:**

> "(Note that these lists are not sorted. The elements may appear in any
> order.)"

King's rows are **unsorted**. Primo's `Row` is sorted by variable id -- an
invariant `pivot()`'s two-way merge depends on. These are two different,
internally consistent designs, not one design and a degraded version of it.

### How row addition works without sortedness

`Tk <- Tk + a*Ti` is the core operation (Fig. 2.9). Because the lists are
unsorted, King cannot merge them; instead he loads row `k` into a **temporary
dense partial map** of O(n) entries, then walks row `i`'s non-zeros looking
each up. Cost is O(||Ti||s + ||Tk||s). Crucially, when the sum becomes zero
the algorithm already holds the entry (via the map) and unlinks it in O(1) --
which is exactly the removal Primo currently performs with a linear scan.

Pivot (Fig. 2.10) is then one row addition to build the new row, followed by a
row addition for every `k` with `Tk,j != 0` -- iterating the column list.

A footnote records a refinement: a slightly more involved RowAddition can load
the *added-from* row into the map instead, so `Ti` is loaded only once per
pivot rather than once per affected row. Not asymptotically better, but it is
the version to implement.

### The design fork this exposes

| | King | Primo today |
|---|---|---|
| row storage | unsorted doubly linked list | sorted contiguous `vector<pair>` |
| row addition | dense scratch map | two-way merge (item H8) |
| entry removal | **O(1) unlink, pointer in hand** | **linear scan of the column list** |
| column size | maintained O(1) | `occurs_in_[var].size()` |

Both reach O(||Ti|| + ||Tk||) row addition. King pays an O(n) scratch map and
loses contiguity; Primo pays sortedness maintenance and gets cache-friendly
merges -- and then pays for removal, because without cross-links it cannot
unlink an entry it is already holding.

### Also read

2.2.7.3 argues the pivot is strongly polynomial: tableau form is Gaussian
elimination under a variable order, a pivot is a re-elimination under a new
order, and Schrijver's Theorem 3.3 bounds the coefficient sizes. Lemma 2.19
and Theorem 2.20 carry that through. This is a complexity argument, not an
implementation technique, and does not bear on the representation choice.

**Not established by this section:** anything about fill-in. Searching the
thesis for fill-in, density, Markowitz or refactorization turns up only the
sparse format itself, so the densification measured in
[](../qf-lra-heuristics.md) section 6 is not a problem this source answers.

(jonas-strejcek-unconstrained)=
## Jonáš & Strejček, "On Simplification of Formulas with Unconstrained Variables and Quantifiers" (SAT 2017)

> Martin Jonáš and Jan Strejček. SAT 2017.
> <https://www.fi.muni.cz/~xstrejc/publications/sat2017preprint.pdf>
> **Read 2026-08-05**: abstract, §1, and §4 (Definition 3 and Theorem 2).
> §5-§6 (quantifiers, evaluation) skimmed.

Preprocessing simplification for SMT built on **unconstrained variables** --
in their definition, a variable that **occurs only once** in the formula and
can therefore take any value without disturbing anything else. Terms built
from such variables are *unconstrained terms* and can be replaced by a fresh
variable: their example is `x + (5*y + z) = y*z` collapsing to `u = y*z`. The
technique is credited to two independent PhD theses, Bruttomesso (Trento,
2008) and Brummayer (Linz, 2010), both of which observed that industrial
benchmarks carry a non-trivial number of such variables.

The paper's own contribution is to generalize this in two directions. First,
**partially constrained terms**: Definition 3 calls two terms
`C`-interchangeable when, for every valuation of the constrained variables `C`,
they range over exactly the same set of values, and Theorem 2 licenses
replacing one by the other. The worked example is bit-vector `6*x` being
replaceable by `2*y`, since both range over the even values. Second, the same
machinery is extended to quantified formulas. Evaluation is on bit-vector
benchmarks.

**The precondition that matters when comparing it to anything else** is in
Theorem 2: `vars(s) ∩ vars(ϕ) ⊆ C`, which the proof uses to argue each
variable of `U` "appears either only in `t` or only in `s` and not in any other
part of the formula". The technique is therefore about variables that are
**local** to the rewritten term. It is not a polarity argument and does not
apply to a variable spread across a whole formula, however regularly it
behaves -- see [](monotone-variable-elimination.md).
