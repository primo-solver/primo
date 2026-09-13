# Fair and Adventurous Enumeration of Quantifier Instantiations

Reference:

- Mikoláš Janota, Haniel Barbosa, Pascal Fontaine and Andrew Reynolds, "Fair
  and Adventurous Enumeration of Quantifier Instantiations", FMCAD 2021, TU Wien
  Academic Press, pp. 256–260,
  DOI: <https://doi.org/10.34727/2021/isbn.978-3-85448-046-4_35> (resolves to
  the reposiTUm record, checked 2026-09-13). Open access as arXiv:2105.13700;
  the PDF is not stored in the repository.

Read in full: Sections I–VI, Table I and Figures 1–3.

**Status: the orders of Section III are implemented as
`--quantifier-tuple-order` (`max`, `max-colex`, `sum`, `leximax`, `deepening`,
`random`) and evaluated on the UF division: `sum` is the best single order,
and the six orders are orthogonal. Two of the three redundancy criteria
exist; the entailment check and fail masks do not.** This note
complements [](enumerative-instantiation.md), which covers the loop, Theorem 1
and strategy u. The instantiation code is described in [](../quantifiers.md).

## Section III: orders on tuples

- **Setup.** Each variable `xi` has a candidate term sequence `Ti` sorted by
  preference, so an instantiation is an `n`-tuple of indices. `(0, …, 0)` is the
  most preferred tuple and the tuple of last indices the least.
- **Pareto order.** `t1` dominates `t2` when `t1 ≠ t2` and `ai ≤ bi` for every
  `i`. In the Pareto graph there is an edge when one index grows by 1.
  - A broad traversal is *fair*: it changes all variables' terms evenly.
  - A deep traversal is *adventurous*: it reaches less preferred tuples early.
- **A. Stages by maximal digit** (cvc5's `u`, the order of the TACAS 2018
  paper). Stage `k` holds the tuples whose largest index is `k`, and within a
  stage tuples are ordered lexicographically. For two variables:
  `(0,0), (1,0), (0,1), (1,1), (2,0), …, (2,2)`. It respects Pareto order and
  runs in constant space. With many variables it is unbalanced: "even with
  M = 1 already 10 variables require 2^10 iterations before the most
  significant digit is changed".
- **B. Stages by sum of digits** (`sum`). This is a breadth-first traversal of
  the Pareto graph: `(0,0,…,0), (1,0,…,0), (0,1,…,0), …, (0,…,0,1), (2,0,…,0),
  (1,1,…,0), (0,2,…,0), …`. It respects Pareto order and runs in constant
  space.
- **C. Leximax** (`lmax`), "arguably the most fair". `t1` is preferred to `t2`
  when `t1` sorted in descending order is lexicographically smaller than `t2`
  sorted the same way. Enumeration stages sorted tuples and lists each one's
  permutations lexicographically. For two variables:
  `(0,0), (0,1), (1,0), (1,1), (0,2), (2,0)`. Constant space.
- **D. Iterative deepening** (`id-n`, depth increment `n`). Depth-first up to a
  maximum depth that grows by `k`. This deliberately violates Pareto order:
  maximum depth 2 gives `(0,0), (0,1), (0,2), (1,1), (1,0), (2,0)`, where
  `(1,0)` dominates `(1,1)` but comes later.
- **Random walk** (`rwlk`). Like depth-first search, but with a set instead of a
  stack, and the next element is chosen at random.

## Section IV: discarding redundant instantiations

An instantiation is redundant if it does not rule out models in the current
context. Three criteria, in increasing cost:

1. **Duplicate term vector.** A trie per formula `φ` holds the term vectors
   already used.
2. **Entailed.** The fast, incomplete entailment check of Reynolds, Barbosa
   and Fontaine, TACAS 2018, Section 4.1: the lemma is already implied by the
   current constraints.
3. **Duplicate formula modulo rewriting.** Normalize `φσ` by rewriting and look
   it up in the set of formulas instantiation has already returned.

**Fail masks.** The last two criteria can show that a whole *class* of
instantiations is redundant.
- **Definition.** A fail mask for `σ = {x1 ↦ t1, …, xn ↦ tn}` is a bit vector.
  Every substitution that extends `{xi ↦ ti | bit i set}` is redundant.
- **Computation, greedy.** Start with all ones. For `i = 1, …, n`, drop
  `xi ↦ ti` from `σ`. If the instantiation is still redundant under criterion 2
  or 3, clear bit `i`; otherwise put the entry back.
- **Example.** With `φ = P(x1,x2) ∨ Q(x2,x3)`, `σ = {x1↦a, x2↦b, x3↦c}` and
  `E = {P(a,b), ¬Q(b,c)}`, the mask is `110`, because `P(a,b)` entails the
  instance for every `x3`.
- **Use during enumeration.** A mask becomes an index pattern with wildcards:
  `(5,4,3)` with mask `101` gives `(5,?,3)`. Patterns go in a trie, where `?`
  matches anything, and enumeration skips tuples that match a stored pattern.

## Section V: experiments (cvc5)

- **Setup.**
  - *Hardware:* Xeon E5-2620, one core, 300 s, 8 GB.
  - *Benchmarks:* TPTP 7.4.0 first-order (18,627 after dropping polymorphic
    problems), and SMT-LIB 2020 UF (7,668), UFLIA (10,137) and UFNIA (13,509).
  - *Configurations:* every cvc5 configuration also uses conflict-based
    instantiation as a fail-fast step.

- **Table I (problems solved).**

  | Library | e | u | id2 | id4 | lmax | sum | rwlk | allu-port | eu-port | eallu-port | z3 |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | TPTP | 7765 | 6989 | 6801 | 6834 | 6832 | 6922 | 6839 | 7330 | 9056 | 9292 | – |
  | UF | 3243 | 3016 | 2975 | 2963 | 2959 | 3009 | 2992 | 3120 | 3433 | 3452 | 2905 |
  | UFLIA | 7424 | 6024 | 6018 | 5897 | 6001 | 5980 | 5994 | 6188 | 7595 | 7615 | 6912 |
  | UFNIA | 5715 | 7458 | 7396 | 7384 | 7426 | 7437 | 7430 | 7620 | 7740 | 7843 | 6491 |

  - *Columns:* `e` is E-matching. `allu-port` is the virtual best of the
    enumerative configurations, `eu-port` of `e` and `u`, and `eallu-port` of
    all cvc5 configurations.

- **Findings.**
  - *Orders:* no alternative order beats `u` alone on UF or UFLIA. The virtual
    best of the enumerative configurations removes almost 3% of the unsolved
    UFNIA problems relative to `u`, and adding them to `eu-port` removes almost
    2%. The authors read this as orthogonality that portfolios should use.
  - *Random strategy choice:* picking a random strategy at each round depends
    strongly on the seed. On UFLIA, seed 0 solves 6,007 and seed 7 solves
    6,047.
  - *Fail masks* (Figure 3, `u` against `u-no-failmask`, all benchmarks): 217
    problems are solved only with them and 31 only without. Many common
    problems speed up considerably. Where the masks do not help, their overhead
    is visible.

## Primo mapping

| Paper element | Primo |
| --- | --- |
| per-variable sequence `Ti` sorted by preference | one pool per sort, shared by every variable of that sort; order of discovery by a depth-first walk of the assertions and drained lemmas; no preference order |
| A. stages by maximal digit | `max` (default): the original order, grouped within a stage by the first position holding the level, then an odometer with the rightmost position fastest, e.g. `(1,0), (1,1), (0,1)`. It is fair but not Pareto-respecting. `max-colex`: the paper's order, colexicographic within a stage, e.g. `(1,0), (0,1), (1,1)` |
| B. sum of digits | `sum`: descending lexicographic within a stage |
| C. leximax | `leximax` |
| D. iterative deepening | `deepening`, with the increment set by `--quantifier-deepening-increment` (default 2) |
| random walk | `random`, seeded per quantifier from `--quantifier-random-seed` |
| orders over growing pools | staged orders resume at the first stage that had a tuple outside the pools; the walks restart when their depth limit was reached or the pools grew, and random-walk successors outside the pools wait until they grow |
| criterion 1, duplicate term vector | `Enumeration::tried` (per atom and polarity), plus the raw instance cache on instance TermId, which also catches different tuples giving one instance |
| criterion 2, entailed | not implemented |
| criterion 3, duplicate modulo rewriting | `InstanceSimplifier` and the canonical cache (per atom and polarity) |
| fail masks | not implemented |
| conflict-based instantiation, E-matching | not implemented |
| portfolio | not implemented |

The paper's counts are not comparable with Primo's. The UF release differs
(7,668 against 7,590 files), the timeout differs (300 s against 60 s), and
every cvc5 configuration adds conflict-based instantiation.

## Proposed work, in order of cost

1. **Selectable tuple orders (implemented, `theory/quantifiers/tuple_orders.*`,
   `tests/unit/tuple_order_tests.cpp`).** `sum`, `lmax`, `id-k` and `rwlk` as
   values of a `--quantifier-tuple-order` option, with `max` as the default. They only
   replace the tuple generator inside `enumerate`. The per-atom tried set stays,
   so any order keeps each tuple to one instance.
   - Evaluate each order against `max` on the UF division.
   - Report the virtual best as well as single-order counts, since the paper's
     gain is orthogonality. The sledgehammer timeouts, with their
     high-arity quantifiers, are the first place to look.
   - A deep or random order gives up the Pareto fairness that Corollary 2 of
     the TACAS paper relies on, so an `unknown` from such an order is weaker
     than one from `max`.
   - **Result** (SLURM job 144606, 60 s; the table is in
     [](../quantifiers.md)). Solved: `max` 1,867, `max-colex` 1,891, `sum`
     1,911, `leximax` 1,876, `deepening` 1,864, `random` 1,846. The virtual
     best of the six is 2,020.
     - *Unlike the paper,* two alternatives beat the default on UF. Primo's
       `max` is not the paper's `u`, and the paper's orders also run with
       conflict-based instantiation.
     - *Like the paper,* the orders are orthogonal. The deep and random
       orders solve the most files that no other order solves (14 and 26),
       and in sledgehammer the virtual best gains 51 over the best single
       order.
2. **Fail masks from criterion 3 alone.** Rewriting a partially instantiated,
   open formula can use the existing normalizer.
   - Criterion 2 is what drives the paper's example, so this may rarely fire.
   - Count how often a partial substitution is redundant before relying on it.
3. **The entailment check, then fail masks from it** (scoped in
   [](../quantifier-entailment-design.md)). A congruence-based check
   of an instance against the current assignment (TACAS 2018, Section 4.1),
   then fail masks derived from it. This is the largest effect in the paper
   (217 against 31 uniquely solved), and also the largest change: the check has
   to read EUF's current classes, which the quantifier component does not see
   today.

## Tests to derive when implementing

- The first tuples of each order for two and three variables, against the
  sequences quoted above. For maximal digit, check the documented within-stage
  order rather than the paper's.
- Every order enumerates each tuple of a finite pool exactly once. The
  Pareto-respecting orders never produce a tuple before one that dominates it.
- A fail mask never skips a tuple whose instance is not redundant; check against
  enumeration without masks on small formulas.
