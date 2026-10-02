# Boolean Arguments in EUF

## Generated-term preparation contract

The original design obligation here is independent of the producer of a term:
before a ground solve, asserted formulas and all of their auxiliary definitions
must have been lowered, clausified and prepared for their owning theories.
Allocating a sorted symbol does not establish its semantic constraints.

`CombinationRecipe::prepare_boolean` is an idempotent registry-completion hook.
The component driver calls it before initial ownership planning and at every
atom-installation boundary, after disconnecting the old dispatcher and before
borrowing the atom registry. Preparation may append atoms of its own; those
atoms must also be installed and observed before the next solve. Existing
identities and domain definitions survive repeated preparation. Ground walks
stop at quantifiers, including their annotations; closed instances are prepared
when they enter the ground formula. No pass walks every allocated symbol.

This is a repair of the existing Boolean-domain invariant, not a new
instantiation or theory algorithm. Its proof obligations are that each Boolean
argument receives its SAT truth value in EUF, that no open binder body enters
ground EUF, and that registry growth cannot invalidate a borrowed term or atom.

The producer audit is:

| Producer | Definitions and preparation owner |
| --- | --- |
| Initial assertions and frontend Boolean names | Frontend side assertions and mandatory lowering precede the selected engine's domain preparation. |
| Quantifier instances and Skolem lemmas | `prepare_lemmas` names Boolean arguments and lowers each batch, including its definitions; the driver's installation boundary completes the registry. |
| Ground ITE, `div` and `mod` | `lower_extended_terms` returns rewritten roots together with defining constraints. ITE auxiliaries keep their result sort. Quotients are Int, with bounds on the residual; integer theory enforces integrality. |
| Component interface encodings | Materialization emits ground equivalences; installation prepares all resulting atoms before reconnection. The existing fixed interface scopes are not extended by this repair. |
| Assumptions-mode UF appends | The persistent engine prepares Boolean arguments once per query after all appends, before constructing its working propagator. |
| Assumptions-mode LRA appends | The session's mandatory lowering supplies scoped roots and side constraints; append prepares arithmetic atoms before checking. No UF Boolean domain is involved. |

Quotient sharing is local to one lowering call. A later call emits its own
definitions, so a cached result cannot outlive popped defining assertions.
Lowering processes children before parents and builds definitions from already
lowered terms. Fresh terms produced in a later batch require another lowering
call. Unsupported quantified arithmetic remains unsupported.

`BooleanClausifier::boolean_argument_scan_count_` advances through the
append-only atom registry. Repeated preparation visits only new roots, including
roots appended by registration itself; the visited-term set is local to each
call. Existing roots need no rescan after scope pop. The retained-session
invariants and tests are recorded in [](assumption-incremental-uf.md).

## Semantic invariant

SMT-LIB permits uninterpreted functions over arbitrary sorts, including
Boolean arguments. If `p` and `q` are Boolean terms, EUF congruence requires

```text
p = q  =>  f(p) = f(q)
```

This remains true when the Boolean equality is written explicitly, lowered to
CNF, or implied transitively by several Boolean equivalences.

For example, the following is unsatisfiable:

```text
(declare-fun p () Bool)
(declare-fun q () Bool)
(declare-fun f (Bool) Bool)
(assert (= p q))
(assert (not (= (f p) (f q))))
```

The same obligation can be expressed propositionally:

```text
(assert (or (not p) q))
(assert (or (not q) p))
(assert (not (= (f p) (f q))))
```

The CNF clauses are handled by SAT, but SAT alone does not know the UF
congruence rule. The EUF layer must retain the Boolean arguments and connect
their assigned truth values to the application terms.

## Current Primo architecture

### Boolean equality lowering

SMT-LIB Core defines equality on `Bool` as ordinary equality of the two truth
values. Primo therefore lowers every source-level Boolean equality to
bidirectional implication, and every source-level Boolean disequality to XOR,
before Boolean abstraction. This rule is uniform: a UF application inside an
operand does not turn the equality into an independently assignable EUF atom.

The implementation checklist is:

- `Frontend::build_pairwise_equality` and `build_pairwise_disequality` own the
  source-level lowering; the resulting `And`, `Or`, `Implies`, and `Not` terms
  are clausified by `BooleanClausifier`.
- Boolean constants and Boolean-valued UF applications retain one SAT literal
  each. Their assigned truth values are represented in congruence closure by
  equality to the canonical `true` or `false` term.
- Compound Boolean UF arguments remain frontend-created auxiliary constants
  with a bidirectional implication to the source formula. EUF sees the
  auxiliary while SAT owns the defining equivalence.
- The lowering is linear in the number of operands. It introduces a constant
  amount of Boolean structure for each adjacent pair in n-ary `=`.

The proof obligation is that the lowered formula is true exactly when the two
Boolean operands have the same truth value. Congruence remains sound because
Boolean terms assigned the same SAT value are merged with the same canonical
EUF value. Tests cover equality and disequality between an arithmetic predicate
and a compound formula containing a Boolean UF application, as well as the
existing direct, CNF-transitive, nested-argument, and complement cases.

This is semantic lowering based on the SMT-LIB 2 Core definition of Boolean
equality. It is not an implementation of a new congruence-closure algorithm.

The current mixed solver keeps Boolean theory atoms available to EUF. This is
deliberate: SAT assignments and CNF clauses do not currently form a complete
SAT-to-EUF equality bridge. In particular, the clauses

```text
(not a) or b, (not b) or a,
(not b) or c, (not c) or b,
(not c) or d, (not d) or c
```

force `a = b = c = d` in every satisfying SAT assignment, but they are still
propositional clauses. They are not delivered to the EUF closure as a proof
that the terms `a` and `d` are equal. Removing `b` and `c` from EUF because a
syntactic dependency analysis says they are not direct UF arguments can
therefore lose the explanation needed to derive `f(a) = f(d)`.

Consequently, the current implementation does **not** prune nullary Boolean
terms from mixed EUF. A future architecture could add an explicit, explained
SAT-to-EUF equality bridge or compile Boolean UF applications into guarded
congruence clauses, but that is outside the current implementation.

## Why congruence closure alone is incomplete here

Congruence closure decides the ground equality fragment over an *unconstrained*
domain: it is free to satisfy a formula in a model with as many elements as it
likes. That is sound for an uninterpreted sort, whose cardinality the problem
never fixes. `Bool` is different -- SMT-LIB fixes it at exactly two elements.

In Nelson--Oppen terms, combining EUF with `Bool` steps outside the
stably-infinite assumption the standard completeness proof relies on. A
stably-infinite theory lets the procedure guess only an *arrangement* of the
shared terms and never a domain size; a sort of fixed finite cardinality
invalidates that, and the two standard repairs are **domain enumeration**
(assert that every relevant term equals one of the elements) or a
polite/shiny-style combination that exchanges cardinality information
explicitly. With two elements, enumeration is one constraint per term and is
what Primo does.

The failure mode without it is concrete rather than theoretical. On

```lisp
(declare-fun tb (Bool) U)
(assert (= (tb true) (tb false)))
(assert (not (= (tb p) (tb q))))
```

`tb` collapses both elements of `Bool`, so `tb(p) = tb(q)` for every `p` and
`q` and the formula is unsatisfiable. Left unconstrained, congruence closure
puts `p` in a class containing neither `true` nor `false` -- a third truth
value -- and reports `sat`. Primo did, until 2026-08-26, and the model it
printed showed the third element directly: a `Bool -> Bool` function emitted
with **three** `ite` branches.

## The cardinality axiom: argument registration

`BooleanClausifier::register_boolean_argument_terms()` (`src/smt/clausifier.cpp`)
registers, as theory atoms, every `Bool`-sorted term that occurs as an
**argument** of an uninterpreted application. Registration is the whole
mechanism; nothing else is added.

It works because the per-atom handling that already existed does the rest. For
any assigned theory atom whose term is a `Bool`-sorted `Constant`,
`add_explained_theory_equality` (explained engine) and `flatten_assigned_atom`
(flat/incremental engine) assert

```text
t = true    with reason {lit(t)}      when SAT assigns lit(t) true
t = false   with reason {not lit(t)}  when SAT assigns lit(t) false
```

and `true != false` is asserted unconditionally when the closure is built
(`PreparedFlatTheory`, the online closure, and re-checked in
`find_theory_conflict`).

**Cost.** `theory_atom_for_term` routes to `encode_theory_atom_lit`, which
reuses the argument's existing literal. An argument not yet encoded receives
its first SAT variable. Registration introduces **no separate equality variable
and no linking clause**. The counter `:no.boolean-domain.registered-terms`
reports how many terms were registered; it is 0 for any problem whose functions
take no `Bool` argument, which is almost all of the SMT-LIB QF_UF division --
five files in a 289-file sample register anything at all.

Deliberately not registered, with reasons in the correctness argument below:
`Bool`-valued applications, operands of source-level `Bool` equality, and the
constants `true`/`false` themselves.

Registration is now the sole encoding. The former `bridge-atoms` path and
`--euf-boolean-domain` selector have been removed, together with their bridge
counters. Historical A/B runs remain reproducible from the revisions recorded
in the run log; see {ref}`the removal record <lead-boolean-bridge-exclusion>`.

## Correctness argument

### Soundness

**Claim.** The encoding cannot turn a satisfiable problem unsatisfiable.

Registration adds no clause. A previously unencoded argument may receive an
unconstrained SAT variable, which does not restrict the existing propositional
assignments. The theory then receives the argument's assigned truth value. Adding
theory assertions can produce a conflict where none was found before, but it
can only be a real conflict provided every added assertion is **entailed**. So
soundness reduces to entailment of the two assertion kinds:

1. `t = true` under `lit(t)`, and `t = false` under `not lit(t)`. In SMT-LIB a
   `Bool`-sorted term used as a formula denotes its truth value, and `lit(t)`
   *is* the propositional abstraction of `t`. In any model of an assignment
   containing `lit(t)`, `t` denotes true, so `t = true` holds there. The reason
   recorded is `{lit(t)}` -- exactly the assumption used, so explanations
   remain valid and conflict clauses stay over genuine assumptions.
2. `true != false`. SMT-LIB's `Bool` has two *distinct* elements.

Neither depends on which terms were registered, so registering more terms than
necessary would be sound (merely wasteful); registering too few is the
completeness question below.

### Completeness: why arguments are enough

**Claim.** With arguments registered, no `Bool` term the closure holds can end
up in a class containing neither constant, on a complete assignment.

The closure holds a `Bool`-sorted term only through one of these routes, and
each is covered:

| route | constrained by |
| --- | --- |
| a `Bool`-valued application `f(...)` | already a theory atom; the same per-atom handling asserts `f(...) = true/false` |
| a `Bool`-sorted **argument** of an application | registration -- this is the case that was missing |
| the constants `true` / `false` | they *are* the canonical elements, and are asserted distinct |
| a compound `Bool` term in argument position | the frontend binds it to a fresh `Bool` constant with a defining equivalence (`bind_bool_argument_if_needed`), and that constant is an argument, so the row above applies |
| an operand of source-level `Bool` `=` / `distinct` | lowered to iff / XOR before Boolean abstraction, so it never becomes an EUF equality atom; it reaches the closure only if it also occurs in one of the rows above |
| a `Bool` term occurring only in the propositional skeleton | never entered into the closure, so it is in no class and cannot carry a third value |

On a complete assignment every `Bool` term the closure holds is therefore
merged with `true` or with `false`, and those two are distinct. The closure's
interpretation of `Bool` is then a genuine two-element interpretation, and
ordinary congruence over it is complete for the ground fragment.

Two consequences worth stating, because both were verified rather than assumed:
pigeonhole reasoning over `Bool` terms (three pairwise-distinct ones cannot
exist) is already decided propositionally by the iff/XOR lowering, and the
converse of congruence -- `f(p) = f(q)` with `f` injective implies `p = q` --
is exactly what argument registration restores.

**This is an argument, not a machine-checked proof.** Its empirical backing is
under [Verification cases](#verification-cases).

### The invariant the pure-theory filter must respect

The pure-theory filter drops the assignment of an atom that occurs at a single
polarity in the formula, on the reasoning that the other assignment cannot
matter. That reasoning has a precondition -- spelled out, with the soundness
argument it comes from and the rule for future mechanisms, in [](euf.md)'s
[](#pure-theory-filter-precondition):

> An atom may be filtered only if its meaning to the theory is **exhausted by
> its propositional occurrences**.

Registered arguments violate it. `collect_theory_atom_polarities` records
propositional occurrences only and never descends into function arguments, so a
constant occurring once in the skeleton -- `(or p q)` is enough -- is
classified pure, while congruence over `f(p)` needs its value either way. The
first version of this fix (`fa44295`) missed that and was **unsound**, and on
the mixed path it was a **regression**: the older bridge encoding was immune
only because its atoms are created after polarity collection and so are never
classifiable as pure.

`TheoryAtom::theory_relevant_at_both_polarities` records the precondition
explicitly, and `should_filter_assignment` refuses to filter such an atom. Any
future mechanism that introduces theory atoms not derived from the
propositional structure has to set it.

### What is not claimed

- Nothing here extends to a sort of fixed cardinality other than `Bool`.
  Uninterpreted sorts are unconstrained, so the question does not arise for
  them today; a future finite-sort feature would need its own enumeration.
- This Boolean-domain argument does not establish the arithmetic semantics of
  `div`, `mod`, `divisible` or `to_int`. Those belong to the arithmetic layer
  and the separate combination discussion in [](open-leads.md)'s lead 14.
- Historical A/B agreement with the removed bridge encoding is empirical
  evidence, not an equivalence proof. The purity-filter interaction above
  explains why registration needs its explicit both-polarities relevance flag.

## Scope and limitations

The current implementation covers ground Boolean UF applications and their
arguments by retaining the relevant Boolean theory atoms in the mixed path. It
does not claim a general Boolean rewriting, SAT-to-EUF bridge, or
Ackermannization algorithm. Prepared-EUF reuse across SAT checks is a separate
performance optimization.

MathSAT has historically required special rewriting for UF applications with
Boolean arguments. That is an implementation compatibility issue, not an
SMT-LIB semantic restriction; the benchmark corpus may therefore underrepresent
this EUF shape.

## Verification cases

**Seeds.** `benchmarks/seeds/qf_uf/` carries the distilled shapes in the pure
logic -- the two-assertion domain witness, the converse of congruence,
pigeonhole, the pure-polarity case that defeated the first fix, and two shapes
that already worked (forward congruence, the Bool-valued result link) kept
deliberately so a change is measured against what must not regress.
`benchmarks/seeds/qf_uflra/` carries the mixed-path versions, including a
`_mixed_` companion for the pure-polarity case.

**Harness.** `scripts/qf_uf_boolean_fuzz.py` generates pure QF_UF cases -- a
python test asserts no arithmetic can leak in, since arithmetic is precisely
what kept the older QF_UFLRA generator off this solver path -- and compares
against z3. Eleven shapes: `argument`, `result`, `compound`, `nested`,
`mixed-sort`, `two-valued`, `converse`, `pure-polarity`,
`model-reconstruction`, `random`, `all`.
`scripts/slurm_qf_uf_boolean_fuzz_gate.sbatch` runs one shard per shape, keeps
every generated input, selects successful SAT answers for model replay, and
requires Dolmen to accept each returned model. Replay also requires the answer
to remain SAT. UNSAT inputs receive no model request; a run without successful
SAT cases skips replay while preserving any answer-check failure. A failure names the capacity rather
than only a case number. Model validation is intentionally part of this gate:
answer differential fuzzing cannot detect an invalid model attached to a
correct `sat` result. Prepared workspaces use the pinned
`contrib/local/bin/dolmen` by default; `PRIMO_FUZZ_DOLMEN` remains available
for an explicitly verified alternative binary.

The `model-reconstruction` shape always includes a solved constant of
uninterpreted sort whose definition contains a Bool argument, plus an equality
collapsing `tb(true)` and `tb(false)`. Both assertions are load-bearing. This
pins the Boolean-completion failure introduced with the original QF_UF model
printer and prevents random assertion sampling from silently deleting the
preprocessing/table interaction.

The `random` shape is grammar-based: random signatures over `Bool` and two
uninterpreted sorts, terms built bottom-up by level, compound terms
(`and`/`or`/`not`/`=>`/`xor`/`ite`, `ite` over an uninterpreted sort, and
`(dis)equality` used as a `Bool` term) reaching argument position, and a random
propositional skeleton. It does **not** subsume the targeted shapes, which was
measured: against the binary carrying the pure-theory-filter bug,
`pure-polarity` disagrees on ~35% of cases while `random` found none in 1,000,
because that bug needs four conditions simultaneously. Both kinds are kept.

**Evidence for the 2026-08-26 fix**, recorded in `docs/run-log.json` under
`2026-08-26-qf-uf-boolean-cardinality-fix`:

| check | result |
| --- | --- |
| harness, all shapes | 91 disagreements in 600 -> 0; clean on further seeds |
| SLURM gate | 1,000 cases per shape, every shard clean |
| QF_UF division, 7,503 files at 60s | **0 answer changes** against the pre-fix binary, 0 wrong |
| QF_UFLRA, 1,284 files at 120s | 1,272 answered, identical sat/unsat split, 0 disagreements against the pre-fix run |
| registration vs `bridge-atoms` | 0 answer disagreements on either fragment; registration 3% faster on the 881 QF_UF files both solve in >1s |
| cvc5 1.3.4, Yices 2.7.0 | agree with z3 on every generated case |
| OpenSMT 2.9.2 | clean on pure QF_UF; wrong on 58 of 600 *mixed* cases, reproducing the OpenSMT bug report in `bugs/opensmt-qf-uflra-boolean-disagreement/README.md` |

Two of the five solvers compared here have had a wrong answer in this corner.

**Historical note on coverage.** The QF_UFLRA smoke generator has had a
`bool-arguments` shape, and `benchmarks/seeds/qf_uflra/` eight seeds, since the
2026-08-07 fix ([](bool-argument-congruence-bug.md)). Every one of them is
QF_UFLRA carrying a Real constraint, and every one asserts the argument
equality and checks congruence forward. That is why a converse-direction bug in
the pure path survived them, and why the shape list above varies *logic* and
*direction* and *propositional context* rather than term shape alone.

The design follows the many-sorted ground congruence-closure treatment of
EUF in Nieuwenhuis and Oliveras, *Fast Congruence Closure and Extensions*,
and the SAT/theory separation used by de Moura and Bjørner, *Model-Based
Theory Combination*. SMT-LIB Core defines equality for every sort, including
`Bool`.
