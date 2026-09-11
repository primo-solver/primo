# Quantifiers: parsing and term representation

**Status: design note, no quantifier code yet. The proposal at the end (revised
2026-09-11) is pending review.**

This is step 1 of quantifier support: getting `forall`/`exists` from SMT-LIB into
`core::Term`. It weighs how bound variables should be represented, against the
SMT-LIB standard, the literature on binder representations, what the SMT
literature says about the choice, the code as it is, and measurements on the
SMT-LIB `UF` benchmarks.

Sources are the SMT-LIB standard, papers, and PhD theses. No other solver's
source or documentation informs this note (clean-room policy, `AGENTS.md`).

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
- `core::SymbolTable` gives every symbol a `Symbol` record with a name and a sort
  (`result_sort`), and indexes user symbols by name (`symbol_by_name_`).
- The frontend **expands `let` eagerly**: a `let` name maps to the `TermId` its
  value built (`let_scopes_`, `TermBuilder::dispatch_let`). A parametric
  `define-fun` rebuilds its stored body with the parameters pushed onto the same
  scope stack (`TermBuilder::dispatch_instantiate_definition`).
- The builder is **iterative** (`TermBuilder`), and any new traversal must be
  too.

Two frontend gaps in `main` would turn into binder bugs once bound variables
exist. Both are already fixed on `feature/incremental-smt-sessions`, which is to
be merged into `main` before quantifier parsing lands:

1. **`define-fun` bodies are scoped dynamically in `main`.** A free name in a
   parameterized body resolves through the `let`s, and enclosing definition
   parameters, in force at the *call site*. That is already a wrong answer in
   both directions (`bugs/define-fun-dynamic-scoping/`); with binders it would
   be capture of bound variables. The branch fixes it in `5bc811b`.
2. **Quoted symbols are not normalized in `main`.** The lexer keeps the bars, so
   `|a|` and `a` are different names. Today that is an `unknown symbol`
   rejection, while z3 answers both probes conformingly. With binders,
   `(forall ((|x| U)) (P x))` would silently bind a declared global `x`. The
   branch fixes it in `8e9dc59`.

## Options

### A. Named variables: one term per (name, sort)

A bound variable is an ordinary term keyed by its name and sort, and the binder
lists its variable terms.

- **For.** Open terms are ordinary terms. Substitution is a memoized
  `TermId -> TermId` map over the DAG, which existing traversals already know how
  to do. Benchmarks reuse variable names consistently, so `(member ?x ?s1)` is
  one term across every axiom that writes it.
- **Against.** Alpha-equivalent formulas that happen to use different names are
  different terms. More importantly, taken literally this representation is not
  closed under what the frontend does:
  - a `let` value that mentions `x`, used under an inner binder of `x`, is
    captured;
  - so is a definition argument that mentions a bound variable, placed under a
    binder of the same name in the body.

  Some renaming is required. The proposal below confines it to conflicts
  detected while terms are built.

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
- **Against.** Alpha-equivalent formulas are never shared, and neither are
  textually repeated ones. A `define-fun` body containing a quantifier builds new
  variables on every call. The term DAG is about twice the size on `UF`
  (measurements below).

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
touched only inside open and close. Opening a quantifier body with ground terms
is instantiation.

The discipline has limits. Any rewrite that places a *non-ground* term under a
binder needs index arithmetic, or an open with fresh names followed by a close.
Examples are Skolemizing an `exists` under a `forall` (which substitutes `f(x)`
with `x` bound outside) and miniscoping or prenexing. Each close traverses the
open part of the body again. Maziarz et al. (section 2) make the same point
about locally nameless hashing: "as we pass each lambda, we must re-hash the
entire body".

## What the representation affects

In an instantiation-based SMT solver, quantified formulas are abstracted as
propositional variables. The instantiation module adds lemmas of the form
`∀x̄. ϕ ⇒ ϕσ`, where "σ is a substitution from the variables in ϕ to ground
terms" (Reynolds, Barbosa, Fontaine 2018, section 2).

**An instance `ϕσ` is ground.** Hash-consing therefore produces the same term
for it whether the body was written with names, indices, or fresh variables.
After instantiation the representation stops mattering.

It matters only while a formula still has binders:

- parsing, and `let` and `define-fun` expansion;
- the number of distinct quantified formulas, which is the number of
  abstraction atoms and of separately instantiated formulas;
- pattern terms;
- rewrites that act under binders (Skolemization, miniscoping, prenexing).

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
| open argument passed to a quantified `define-fun` under a binder | 0 | 0 |

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
- **Fresh variables double the DAG.**
  - **By family:** the ratio is 2.00 in `sledgehammer` and 2.10 in
    `20170428-Barrett`, but only 1.29 in `grasshopper` and 1.02 in
    `20190906-CLEARSY`.
  - **Cause:** lost sharing of open subterms across quantifiers. In
    `misc/set5.smt2` (43 nodes named, 75 fresh), `(member ?x ?s1)` is one node
    named but six fresh, and the three variable leaves become 20.
  - **Weight:** node count is a memory proxy, not solving time, so this is a
    cost to weigh against C's simplicity, not a disqualification.
- **Fresh variables also split repeated quantifiers.** 8.7% of quantifier
  occurrences (216,860 of 2,492,357) repeat, after `let` expansion, a quantifier
  earlier in the same file, names included. Named variables merge them; fresh
  variables keep each as its own abstraction atom.
- **Alpha-equivalence buys little in `UF`.** De Bruijn merges only another 0.3%
  (6,595 quantifier terms) beyond named variables, because benchmark generators
  already name variables consistently.
- **`UF` never exercises A's failure modes.** There is no shadowing, no capture
  through `let`, and no open argument into a quantified definition. The risk is
  conforming input outside this set, and our own rewrites later.
- **Shifting is real but rare**: 5,991 uses in 601 files, all in `sledgehammer`.

## What the SMT literature says about the choice

None of the SMT sources read here states how bound variables are represented in
cvc5. One of them does state a representation for veriT.

**cvc5** (Barbosa et al., TACAS 2022). Section 2.3 says terms are nodes in a
hash-consed DAG and that a node's kind "can be an operator kind, e.g., addition,
or a leaf kind, e.g., a variable". It says nothing about bound variables.
Quantified reasoning is described by pointing to the papers below.

**The instantiation papers** use the textbook named convention and describe no
implementation representation:

- Reynolds, Tinelli and de Moura (FMCAD 2014, section II) assume "an infinite
  set X_S of variables of sort S" with "the sets X_S ... pairwise disjoint".
- Barbosa, Fontaine and Reynolds (TACAS 2017, section 2) take variables from a
  set X, and substitutions map variables to terms.
- Reynolds, Barbosa and Fontaine (TACAS 2018, section 2) give the ground-instance
  form quoted above.
- de Moura and Bjørner (CADE 2007, section 3) compile a pattern with a mapping
  "from variables to register indices". A matcher needs variables to be
  distinguishable within a pattern, nothing more.
- A text search of Reynolds's thesis (Iowa, 2013) found no description either.

**veriT** (Barbosa, PhD thesis, 2017) does describe one:

- Section 5.1: terms are "represented internally as directed acyclic graphs
  (DAGs) with maximal sharing, also known as 'hash-consing'".
- Section 7.1.1: the `let`-expansion pass "renames bound variables
  systematically to avoid capture". For every quantifier it picks
  "y ← fresh variable" and maps `x ↦ y`.
- Section 7.1.2: Skolemization "assumes that 'let' expressions have been
  expanded and bound variables have been renamed apart". The correctness
  argument that follows rests on the input being shadowing-free.
- Observation 7.3: the pass visits each subterm at most once, and is linear given
  constant-time context operations.
- The thesis says chapters 6 and 7 "led to a joint publication", Barbosa,
  Blanchette and Fontaine (CADE 2017), which is not read here.

So the one SMT precedent found is **named variables made capture-safe by an
invariant established during `let` expansion**, with a fresh variable for every
quantifier (option C's allocation). The proposal keeps an invariant of that kind
but renames only on conflict, which is what keeps A's sharing.

## Proposal (pending)

**Variables with identity; the name is display only.** There is one local
invariant, enforced where quantifier terms are constructed, and a parser
allocation rule that satisfies it without retrying.

### Representation

1. **Build on the frontend fixes of `feature/incremental-smt-sessions`**:
   lexical scoping for `define-fun` bodies (`5bc811b`) and quoted-symbol
   normalization (`8e9dc59`). That branch is to be merged into `main` first.
2. **Variables.** A bound variable is a term of a new `Variable` kind whose
   `symbol` is a `SymbolId` of a new variable symbol kind. The existing `Symbol`
   record already carries the display name and the sort. Variable symbols are
   not entered into name lookup, so several may share a name without clashing
   with each other or with user symbols. Identity is the `SymbolId`, so it is
   part of the hash key; the name is not.
3. **Quantifiers.** `Forall` and `Exists` kinds carry their variables, their
   body, and their patterns.
4. **Per-term caches**, computed at intern time bottom-up: `FV(t)`, `BV(t)`, and
   whether `t` contains a quantifier. All three are empty on quantifier-free
   terms.

### Invariants

For a term `s`, `FV(s)` is its set of free variables, and `BV(s)` is the set of
variables bound by quantifier nodes inside `s`.

**W (no rebinding).** Every quantifier node `Q(v1, …, vn). b` in the term store
has pairwise distinct `v1, …, vn`, and `{v1, …, vn} ∩ BV(b) = ∅`.

- It is a precondition of the quantifier constructors, checked against the cached
  `BV(b)`. A violation is a programming error, not unsupported input.
- It is local: it depends only on the node and its body, so hash-consed sharing
  cannot break it.
- It is equivalent to: no root-to-leaf path passes through two binders of the
  same variable.

**C (closed formulas).** Every assertion and every lemma handed to the solver is
closed.

Consequences, for a node `Q(v̄). b`:

1. **Plain replacement.** Every occurrence of a `vi` in `b` is bound by this
   node. Substituting for `vi` in `b` is plain replacement, memoized over the
   DAG, skipping subterms where `vi ∉ FV`.
2. **No capture.** Substituting a term `s` for `vi` in `b` cannot capture when
   every variable in `FV(s)` is bound by a binder enclosing this node; ground `s`
   is the special case. A capture would need a binder of some `w ∈ FV(s)`
   inside `b`. But `w` is bound by an enclosing binder `E`, and W at `E` rules
   out a binder of `w` anywhere in `E`'s body, which contains `b`.
3. **W is preserved.** The result still satisfies W when `s` contains no
   binders.

Deliberately **not** invariants:

- **Shared variables.** Distinct closed formulas may share variables: every
  instance of a quantifier shares its inner binders' variables with it. A
  technique that treats several quantified formulas jointly renames them apart
  itself.
- **Sibling binders.** The same variable may be bound by sibling binders.
- **Names.** A name says nothing about identity.

### Worked example

Take `Q = ∀x. ∃y. ∀z. F` and instantiate `x := t`, where `t` is ground and
binder-free.

- **The instance** is `I = ∃y. ∀z. F[x:=t]`, keeping the same `y` and `z`. There
  is no renaming: `I` satisfies W by consequence 3 and is closed. Patterns on
  `∀z` that mention `x` get `t` as well.
- **The lemma `Q ⇒ I`** satisfies W: `Q` and `I` are siblings under `⇒`, not
  nested. Instantiating again with the same `t` yields the same term.
- **Skolemizing `∃y` in `I`**, which is positive and outermost, gives
  `∀z. F[t][y:=c]` for a fresh constant `c`. W and C hold.
- **Skolemizing before instantiating** gives `∀x. ∀z. F[y:=f(x)]`, capture-free
  by consequence 2. The rebuilt `∀x` node satisfies W because its body still has
  no binder of `x`.

### When renaming happens

Constructors never rename. A caller about to build a node that would violate W
picks a fresh variable for its own binder. Renaming `x` to `x'` in the body `A`
of an existing `∀x. A` is plain replacement, because W already rules out a binder
of `x` inside `A`.

- **Prenexing** `(∀x. A) ∧ (∀x. B)` into one prefix would put a binder of `x`
  under another, so one side is renamed first.
- **Parsing** chooses variables so that the precondition holds without retrying:
  - **State.** The frontend keeps a pool of variables per (name, sort), and a
    stack of the variables bound by binders under construction, across
    `define-fun` expansion. It also keeps `S`, the union of `BV` over every value
    that can be spliced in by name at this point: `let` values in scope, argument
    values of definitions being expanded, and compiled nullary definitions.
  - **Choice.** At a binder, each declared variable takes the first variable in
    its pool that is in neither the stack nor `S`. The lexical scope then maps
    the name to it.
  - **Why the chosen `v` is not in `BV(body)`.** Every binder in the body is
    either built inside the body, so its variable avoids the stack (which holds
    `v`), or comes from a spliced value, whose binders are in `S`. Constructors
    return only their arguments or new nodes, so nothing else brings binders in.
  - **Why this also prevents capture.** Assertions are closed, so the free
    variables of spliced values are bound by open binders, which are on the
    stack. For example, `(define-fun g ((y U)) Bool (forall ((x U)) (p x y)))`
    used as `(forall ((x U)) (g x))` gives the body's binder a second variable
    named `x`.
  - **A closed quantifier spliced in by `let`.** In
    `(let ((T (forall ((x U)) (P x)))) (forall ((x U)) (and (Q x) T)))`, `T`'s
    variable is in `S`, so the outer binder gets another variable and `T` stays
    shared.
  - **Duplicate names in one list** (3.6.1). The later occurrence avoids the
    earlier one on the stack, matching the nested reading.
  - **Rejected alternative: rebuild on conflict.** Nested conflicts multiply, so
    a body at nesting depth `d` can be rebuilt `2^d` times. Discarded attempts
    also stay interned and shift TermIds, which
    `docs/termid-order-sensitivity.md` tracks as a source of behaviour changes.
  - **Cost.** `S` is conservative: a variable changes even when the spliced
    value is not used in this body, and sharing is lost in that case.
  - **Result.** Textually repeated quantifiers, and repeated expansions of a
    quantified definition in the same context, intern to the same term.
  - **Evidence from `UF`, and its gaps.** There is no lexical shadowing, and no
    open argument reaches a quantified definition. Not measured, because the
    scan tracks neither: closed quantifiers spliced in by `let` or nullary
    definitions under a binder with the same name and sort, and quantified
    definition bodies (54 in 36 `20190906-CLEARSY` files) expanded under such a
    binder.

### Gate

Any assertion whose contains-a-quantifier flag is set returns `unsupported`
before preprocessing. The flag is cached, so quantifiers reached through `let`,
definitions, Boolean-argument side assertions, or `ite` are caught without a
syntactic search.

### Why not D

Its extra gains are small or can be had without indices:

- **Instances are ground** and identical under every representation.
- **Alpha-equivalence merges 0.3%** of `UF` quantifiers beyond named variables.
- **Capture safety** comes from W and the allocation rule instead.

What D would cost is index arithmetic, or open-and-close with rehashing, in every
non-ground rewrite. Variables that are ordinary terms also suit E-matching's
variable-to-register mapping, substitution maps, and printing.

### Why not C

C is this same representation with an "always fresh" allocation policy, which is
veriT's choice. On `UF` it doubles the DAG and splits the 8.7% of repeated
quantifiers into separate atoms. Because the representation is shared, switching
policy is a frontend change, and it can be measured if a technique later wants
variables renamed apart everywhere.

## Risks

The representation is small. The risk is in the code around it, most of which
was written for quantifier-free terms and is kept away from quantifiers only by
the gate.

1. **Passes that do not know about binders.**
   - **Kind-agnostic traversals** descend into quantifier bodies.
     `summarize_formula_features` (`formula_analysis.cpp`) summarizes every child
     this way, so an open `P(y)` would count as an EUF predicate. Any pass that
     registers atoms by walking children would likewise hand open atoms, and
     variables that look like constants, to theory solvers.
   - **Exhaustive switches without `default:`.** The clausifier's assertion and
     encoding switches list every kind explicitly. A new kind matches no case:
     at the assertion root it would add no clause, silently dropping the
     assertion. `-Wall` reports each such switch (`-Wswitch`), which gives an
     audit list, but that is a build error only with `PRIMO_WARNINGS_AS_ERRORS`.
   - **Naming passes** replace a subterm by a fresh constant plus a defining
     assertion. The frontend does this for every non-constant Boolean argument
     of an application (`bind_bool_argument_if_needed`), and preprocessing
     receives a fresh-constant factory through `TacticContext`. Applied to an
     open subterm, the defining assertion has free variables, breaking C. The
     constant also cannot depend on the bound variable, so the formula's meaning
     changes. Step 1 must make the frontend skip naming for open subterms;
     instances are ground and can be named after instantiation.
   - **Generic rebuilders.** `rebuild_lowered_node` (`preprocessing.cpp`)
     rebuilds a node from rewritten children for the simplification and lowering
     passes. A quantifier reaching such a helper must be rebuilt through the
     checked constructors, and the rewrites applied to its body must preserve W.

   *Mitigation:* the gate makes all of this unreachable in step 1. Lifting the
   gate requires an audit of every pass, a distinct `Variable` kind so that no
   `Constant` case matches a variable, and assertions in the theory solvers that
   no variable reaches them.
2. **Context-dependent facts on shared open subterms.** Reusing variables shares
   open subterms such as `(member ?x ?s1)` across quantifiers. A memo keyed only
   by `TermId` is wrong for such a subterm if the fact depends on its context:
   polarity, relevance, a candidate value for a variable, or a simplification
   under assumptions.

   *Mitigation:* such memos key on the context too, or apply only to ground
   terms. The clausifier's polarity memo, keyed by term and polarity, has the
   right shape.
3. **Constructing quantifiers around the check.** W holds only if every
   quantifier node goes through the checked constructors.

   *Mitigation:* offer no other way to intern `Forall` or `Exists`, and run a
   whole-store invariant checker (explicit stack) in tests and fuzzing.
4. **An incomplete gate.** A syntactic search would miss quantifiers reached
   through definitions or side assertions.

   *Mitigation:* the cached flag, as above.
5. **Silent unsoundness that tests miss.** A capture or a leaked context yields a
   different formula, not a crash. `sat` answers on quantified input cannot be
   validated by evaluating a finite model, the way the existing quantifier-free
   model gates do, and `unsat` answers need proofs.

   *Mitigation:*
   - unit tests per shape: shadowing, duplicate names, a spliced closed
     quantifier, definition arguments, prenex renaming, nested instantiation;
   - the invariant checker;
   - differential runs against reference solvers used as black boxes.
6. **Cost on quantifier-free input.** `FV`/`BV` caches on every term would tax
   large inputs that have no quantifiers.

   *Mitigation:* store interned sets, one id per term, and take a fast path when
   no quantifier has been interned, as `extended_term_count()` does for extended
   terms.
7. **Ambiguous printing.** Two distinct variables with the same display name in
   one printed term are ambiguous to anyone reading dumped terms, instances, or
   proofs.

   *Mitigation:* add a disambiguating suffix on collision.

### Open questions

1. Node layout for quantifiers:
   - how the variables are held: a variable-list child, or a count;
   - whether patterns are children inside the hash key (two formulas that differ
     only in patterns become different terms) or a side table keyed by the
     quantifier.
2. Whether variable symbols live in `core::SymbolTable` under a new symbol kind
   kept out of `symbol_by_name_`, or in a separate table.
3. The form of the `FV`/`BV` caches: interned sets, bitsets, or bounds.
4. How naming passes treat open subterms. The proposal is to skip them under
   binders and name instances; the alternative is naming with Skolem functions
   of the enclosing variables.
5. Sequencing: quantifier parsing lands after `feature/incremental-smt-sessions`
   is merged into `main`, on top of its frontend.

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
- H. Barbosa et al. cvc5: A Versatile and Industrial-Strength SMT Solver.
  *TACAS 2022*.
  doi:[10.1007/978-3-030-99524-9_24](https://doi.org/10.1007/978-3-030-99524-9_24).
  Read: section 2 (architecture, quantifiers, Node Manager); searched in full.
- A. Reynolds, C. Tinelli, L. de Moura. Finding conflicting instances of
  quantified formulas in SMT. *FMCAD 2014*, pp. 195–202.
  doi:[10.1109/FMCAD.2014.6987613](https://doi.org/10.1109/FMCAD.2014.6987613).
  Read: section II; searched in full.
  <http://homepage.divms.uiowa.edu/~ajreynol/fmcad14.pdf>.
- H. Barbosa, P. Fontaine, A. Reynolds. Congruence closure with free variables.
  *TACAS 2017*, LNCS 10206, pp. 214–230.
  doi:[10.1007/978-3-662-54580-5_13](https://doi.org/10.1007/978-3-662-54580-5_13).
  Read: section 2; searched in full. <https://verit.loria.fr/papers/tacas2017.pdf>.
- A. Reynolds, H. Barbosa, P. Fontaine. Revisiting enumerative instantiation.
  *TACAS 2018*, LNCS 10806, pp. 112–131.
  doi:[10.1007/978-3-319-89963-3_7](https://doi.org/10.1007/978-3-319-89963-3_7).
  Read: section 2; searched in full.
- L. de Moura, N. Bjørner. Efficient E-matching for SMT solvers. *CADE-21*,
  LNCS 4603, pp. 183–198, 2007.
  doi:[10.1007/978-3-540-73595-3_13](https://doi.org/10.1007/978-3-540-73595-3_13).
  Read: section 3 (pattern compilation); searched in full.
  <https://leodemoura.github.io/files/ematching.pdf>.
- A. Reynolds. *Finite Model Finding in Satisfiability Modulo Theories*. PhD
  thesis, University of Iowa, 2013.
  <http://homepage.divms.uiowa.edu/~ajreynol/thesis.pdf>. Searched for
  bound-variable representation; nothing found.
- H. Barbosa. *New Techniques for Instantiation and Proof Production in SMT
  Solving*. PhD thesis, Université de Lorraine, 2017.
  <https://hanielbarbosa.com/papers/phd.pdf>. Read: section 5.1, chapter 7
  (sections 7.1.1, 7.1.2, and the correctness argument).
- H. Barbosa, J. C. Blanchette, P. Fontaine. Scalable Fine-Grained Proofs for
  Formula Processing. *CADE 2017*, LNCS 10395, pp. 398–412.
  doi:[10.1007/978-3-319-63046-5_25](https://doi.org/10.1007/978-3-319-63046-5_25).
  **Not read**; cited because Barbosa's thesis says its chapters 6–7 became this
  paper.
