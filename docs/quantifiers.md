# Quantifiers: parsing and term representation

**Status: design note, no quantifier code yet. The bound-variable representation
is not decided; the proposal at the end is pending review.**

This is step 1 of quantifier support: getting `forall`/`exists` from SMT-LIB into
`core::Term`. It weighs how bound variables should be represented, against the
SMT-LIB standard, the literature on binder representations, the code as it is,
and measurements on the SMT-LIB `UF` benchmarks.

Sources are the SMT-LIB standard and papers only. No other solver's source or
documentation informs this note (clean-room policy, `AGENTS.md`).

## Scope of step 1

In scope:

- parse `forall` and `exists` (SMT-LIB 2.6 section 3.6) into core terms, keeping
  `:pattern` annotations;
- accept the logics needed to reach that code, `UF` first;
- stop every assertion that contains a quantifier **before preprocessing**, with
  `unsupported`, until an instantiation procedure exists.

The last point is a soundness requirement. `TermKind` is referenced about 730
times across 19 files, and preprocessing, clausification, and the solver engines
have `default:` branches that treat a kind they do not recognize as opaque. A
quantified formula that reached clausification as an opaque Boolean atom would
leave the SAT solver free to pick its value, so every `sat` answer would be
unsound.

Out of scope: instantiation (E-matching, model-based, enumerative),
Skolemization, prenexing and miniscoping, `match`, and `lambda`.

## What SMT-LIB 2.6 requires

From *The SMT-LIB Standard, Version 2.6*, revision 2021-05-12:

- **Grammar** (section 3.6): `( forall ( <sorted_var>+ ) <term> )` and the same
  for `exists`, where `<sorted_var> ::= ( <symbol> <sort> )`.
- **Variable lists** (3.6.1): a list abbreviates nested single-variable
  quantifiers, equations (3.1) and (3.2). Names in one list need *not* be
  distinct; the last occurrence shadows the earlier ones.
- **`let`** (3.6.1): parallel, with pairwise distinct names. Its meaning is
  substitution "possibly after a suitable renaming of t's bound variables to
  avoid capturing any variables". So the standard itself flags capture.
- **Shadowing** (3.6.3): lexical scoping. A bound variable shadows any variable
  or user-defined function symbol with the same name from an enclosing scope.
  Remark 5 is the exception: binders cannot shadow theory function symbols.
- **Patterns** (3.6.6): `:pattern` may only annotate the body of a quantified
  formula, `(Q (...) (! phi :pattern (p ...) ...))`. Each `p` is binder-free and
  unannotated.
- **Symbols** (3.1): `abc` and `|abc|` are the same symbol.
- **Closed assertions** (5.2, Constraint 3): scripts assert only closed
  formulas.

## What the choice has to fit

- `core::Term` is **hash-consed**. `TermManager::Key` is
  `(kind, sort, symbol, children, bool_value, rational_value)`. New `TermKind`s
  are appended, never inserted. There is no variable or binder kind, and
  `core::Substitution` is an empty class.
- The frontend **expands `let` eagerly**: a `let` name maps to the `TermId` its
  value built (`let_scopes_`, `TermBuilder::dispatch_let`). A parametric
  `define-fun` rebuilds its stored body with the parameters pushed onto the same
  scope stack (`TermBuilder::dispatch_instantiate_definition`).
- The builder is **iterative** (`TermBuilder`), and any new traversal must be
  too.

Two existing frontend gaps turn into binder bugs once bound variables exist:

1. **`define-fun` bodies are scoped dynamically.** A free name in a body resolves
   through the `let`s in force at the *call site*. Today this is already a wrong
   answer (`unsat` on a satisfiable input), recorded in
   `bugs/define-fun-dynamic-scoping/`. With binders it is capture of bound
   variables.
2. **Quoted symbols are not normalized.** The lexer keeps the bars, so `|a|` and
   `a` are different names. Today that is an `unknown symbol` rejection; z3
   answers both probes conformingly. With binders, `(forall ((|x| U)) (P x))`
   would silently bind the global `x` if one is declared.

## Options

### A. Named variables: one term per (name, sort)

A bound variable is an ordinary term keyed by its name and sort, and the binder
lists its variable terms.

- **For.** Open terms are ordinary terms. Substitution is a memoized
  `TermId -> TermId` map over the DAG, which existing traversals already know how
  to do. Benchmarks reuse variable names consistently, so `(member ?x ?s1)` is
  one term across every axiom that writes it.
- **Against.** Alpha-equivalent formulas that happen to use different names are
  different terms. More importantly, this representation is not closed under what
  the frontend does. A `let` value that mentions `x`, used under an inner binder
  of `x`, is captured. So is a definition argument that mentions a bound
  variable, placed under a binder of the same name in the body. A correct
  implementation needs capture-avoiding substitution, meaning renaming to fresh
  names, and substitution that stops at binders that rebind.

### B. De Bruijn indices

Following de Bruijn (1972), a bound variable is `(index, sort)`: the number of
binders between the occurrence and its binder. A binder carries only its sorts;
names live outside the hash key, for printing.

- **For.** Alpha-equivalent terms are syntactically identical, so hash-consing
  identifies them, and no renaming or capture can occur. Charguéraud (section
  2.3): "no α-renaming operation is ever needed".
- **Against.** An index depends on context:
  - **Lost sharing.** The same open subterm gets different indices at different
    binder depths, or at different positions in a variable list. Maziarz et al.
    (section 2) call these *false negatives*.
  - **Shifting.** Whenever an open term moves under more binders, its loose
    indices must be shifted. Charguéraud (2.3) calls this the representation's
    "one major drawback". In Primo's frontend it happens at every use of a
    `let` value below the binder depth at which the value was built.
  - **Groundness checks.** "Is this term ground?" needs a bound on its loose
    indices. That bound is cheap to cache at intern time, because interning is
    bottom-up.
  - **Context-dependent coincidences.** Maziarz et al. also call structurally
    equal open subterms under different binders *false positives*. For
    hash-consing that is fine, since they are the same term relative to their
    binders. But a pass that memoizes a *semantic* fact about an open subterm
    must key it by context.

### C. A fresh variable per binder occurrence

Every binder occurrence introduces variables that nothing else shares.

- **For.** No capture, shadowing, or shifting can occur.
- **Against.** Alpha-equivalent formulas are never shared. A `define-fun` body
  containing a quantifier builds new variables on every call. The term DAG is
  about twice the size on `UF` (measurements below).

### D. Locally nameless

This is Charguéraud (2012) and McBride and McKinna (2004). Bound variables are
de Bruijn indices; free variables are names. Two operations cross the boundary:
*open* replaces a binder's index by a term, and *close* abstracts a name into an
index. Charguéraud (2.4): by using indices for bound variables it "avoids the
introduction of α-equivalence classes", and by using names for free variables
"it avoids the need for shifting de Bruijn indices". McBride and McKinna: "de
Bruijn indices eliminate the need for α-conversion", while names support
"arithmetic-free manipulation of terms".

The stored terms are those of B. What changes is the discipline: indices are
touched only inside open and close. The SMT operations line up with that:

- opening a quantifier body with ground terms *is* instantiation;
- opening it with fresh constants *is* Skolemization.

For the frontend, D means building a binder's body with its variables bound to
fresh *free* placeholder terms, and closing once when the binder is finished.
`let` values are then built over placeholders and never need shifting. The price
is that closing traverses the open part of the body once per enclosing binder.
Maziarz et al. (section 2) make the same point about locally nameless hashing:
"as we pass each lambda, we must re-hash the entire body".

## Measurements: SMT-LIB `UF`, 7,590 files

`scripts/binder_features.py` at `43ddd7b`, run on dai-03 with 32 workers. It
took 7 s, and all 7,590 files parsed. The script expands `let` the way the
frontend does and interns each assertion under three encodings: named (A), de
Bruijn (B, which D also stores), and fresh (C). It does not expand `define-fun`
applications or scope `match`, and neither construct changes the picture here.

| feature | files | occurrences |
| --- | ---: | ---: |
| quantifiers | 7,590 | 2,492,357 |
| of which `exists` | 5,975 | 141,260 |
| quantifier nested in a quantifier | 6,395 | 478,460 |
| `:pattern` (all in `20170428-Barrett`, all single-term) | 2,962 | 187,703 |
| `:no-pattern`, `:qid`, `:weight`, other attributes | 0 | 0 |
| duplicate names in one binder, shadowing, quoted bound names | 0 | 0 |
| `let` value mentioning a bound variable | 3,944 | 250,224 |
| use of such a value under more binders (de Bruijn shift) | 601 | 5,991 |
| of those, a capture under named variables | 0 | 0 |
| `define-fun` with a quantified body (`20190906-CLEARSY`) | 36 | 54 |

Maximum quantifier nesting depth, in number of files: depth 1: 1,195; 2: 2,279;
3: 3,295; 4: 522; 5: 299.

| hash-consed size | named (A) | de Bruijn (B/D) | fresh (C) |
| --- | ---: | ---: | ---: |
| DAG nodes, all assertions | 13,093,851 | 13,256,649 (+1.2%) | 26,618,970 (x2.03) |
| distinct quantifier terms | 2,275,497 | 2,268,902 (-0.3%) | 2,492,357 |

What the numbers say:

- **Memory does not decide between A and B/D.** They differ by about 1%. De
  Bruijn is larger in 4,736 files and smaller in 2,644. The lost sharing totals
  238,649 nodes against 75,851 saved.
  - Shifted `let` uses account for 74,123 of the excess.
  - The rest is Maziarz et al.'s false negative, driven by variable-list
    positions. In `misc/set5.smt2`, `(member ?x ?s1)` is `member(%2, %1)` in the
    axioms that bind `?x ?s1 ?s2` together, but `member(%0, %2)` under an inner
    `?x` binder. The named encoding shares one term across both.
- **Fresh variables double the DAG.** The ratio is 2.00 in `sledgehammer` and
  2.10 in `20170428-Barrett`, but only 1.29 in `grasshopper` and 1.02 in
  `20190906-CLEARSY`. The cause is lost sharing of open subterms across
  quantifiers. In `misc/set5.smt2` (43 nodes named, 75 fresh), `(member ?x ?s1)`
  is one node named but six fresh, and the three variable leaves become 20. Node
  count is a memory proxy, not solving time, so this is a cost to weigh against
  C's simplicity (no capture, shadowing, or shifting), not a disqualification.
- **Alpha-equivalence buys little in `UF`.** Distinct quantifiers fall by 0.3%
  under de Bruijn, because benchmark generators already name variables
  consistently.
- **`UF` never exercises A's failure modes.** There is no shadowing and no
  capture. The risk is conforming input outside this set, and our own rewrites
  later (definition expansion, miniscoping, instantiation into nested
  quantifiers).
- **Shifting is real but rare**: 5,991 uses in 601 files, all in `sledgehammer`.

## Proposal (pending)

Store **de Bruijn indices**, and construct and consume terms **locally
nameless** (option D):

1. **Fix the two frontend gaps first**: lexical scoping for `define-fun` bodies,
   and quoted-symbol normalization. Both are wrong or unsupported today,
   independent of quantifiers.
2. **Append kinds** for a bound variable (index and sort) and for `forall` and
   `exists`. A quantifier carries its body, its variable sorts, and its
   patterns. Variable names stay out of the hash key, in a side table used for
   printing and diagnostics.
3. **Frontend**: bind a binder's variables lexically to fresh placeholder terms,
   build the body, then close it into indices once, with a memoized
   explicit-stack traversal that skips subterms containing no placeholder.
4. **Gate**: any assertion containing a quantifier returns `unsupported` before
   preprocessing.

Why D over A: correctness does not depend on renaming being done at every place
that moves an open term, and instantiation and Skolemization *are* the open
operation. The cost is about 1% more DAG nodes than A on `UF`, and one closing
pass per binder level (at most 5 in `UF`).

A remains a reasonable choice if maximal sharing matters more than removing
capture by construction. Its obligation is capture-avoiding substitution
everywhere open terms move.

### Open questions

1. A versus D.
2. Where the index and the variable sorts live. `Term` has no free field.
   `Times`, `IntDiv` and `IntMod` already put their literal in `rational_value`,
   so the index could follow that; the alternatives are a new field on every term
   or a side table.
3. What a placeholder is during parsing: a reserved internal constant, or a
   distinct kind that never survives closing.
4. Whether the `define-fun` scoping fix lands on this branch or separately on
   `main`, since it is a wrong answer there.

## References

DOIs checked to resolve at doi.org on 2026-09-11.

- C. Barrett, P. Fontaine, C. Tinelli. *The SMT-LIB Standard: Version 2.6*,
  revision 2021-05-12.
  <https://smt-lib.org/papers/smt-lib-reference-v2.6-r2021-05-12.pdf>. Read:
  sections 3.1, 3.6, 5.2.
- A. Charguéraud. The Locally Nameless Representation. *Journal of Automated
  Reasoning*, online 2011. doi:[10.1007/s10817-011-9225-2](https://doi.org/10.1007/s10817-011-9225-2).
  Read: sections 1–2.4, in the author's manuscript
  <https://www.chargueraud.org/research/2009/ln/main.pdf>.
- C. McBride, J. McKinna. Functional pearl: I am not a number—I am a free
  variable. *Haskell Workshop 2004*.
  doi:[10.1145/1017472.1017477](https://doi.org/10.1145/1017472.1017477). Read:
  abstract and introduction, <http://www.e-pig.org/downloads/notanum.pdf>.
- K. Maziarz, T. Ellis, A. Lawrence, A. Fitzgibbon, S. Peyton Jones. Hashing
  modulo alpha-equivalence. *PLDI 2021*.
  doi:[10.1145/3453483.3454088](https://doi.org/10.1145/3453483.3454088),
  arXiv:2105.02856. Read: section 2.
- N. G. de Bruijn. Lambda calculus notation with nameless dummies, a tool for
  automatic formula manipulation. *Indagationes Mathematicae (Proceedings)*,
  1972. doi:[10.1016/1385-7258(72)90034-0](https://doi.org/10.1016/1385-7258(72)90034-0).
  **Not read** (paywalled). Cited as the origin of the notation, as Charguéraud
  and McBride–McKinna cite it.
- Secondhand only: T. Murphy, *The Wizard of TILT*, CMU-CS-02-120, 2002. Maziarz
  et al. cite it as concluding that "the costs of a de Bruijn representation
  exceed the benefits" in a compiler. **Not read.**
