# Preprocessing

Preprocessing turns a ground assertion set into terms and constraints that the
solver engines can consume, simplifies that set, and sometimes adds useful
consequences. These operations have different contracts: a rewrite can preserve
equivalence, elimination can require model reconstruction, and fresh definitions
can extend the signature. The [tactic layer](tactics.md) carries those contracts
through the query.

This chapter describes the default rebuild engine's pipeline. Quantified UF
takes a [separate route](quantifiers.md); assumptions-based incremental engines
use the [restricted tactic contract](tactics.md#scope-and-incremental-use).
Clausification follows preprocessing and is described in [SAT integration](sat.md).

(preprocessing-representation)=
## Representation

| Type and code | Responsibility |
| --- | --- |
| {source}`CheckSatOptions <src/smt/check_sat.hpp>` | Requested profiles, pass switches, and size/iteration limits. |
| {source}`PreprocessingProfile <src/smt/preprocessing_strategy.hpp>` | Selected profile, loop policy, finite-domain analysis and retained generation results. |
| {source}`PreparedPreprocessingStrategy <src/smt/preprocessing_strategy.hpp>` | Full strategy description, selected profile, and result after execution. Preparing it performs transformations; it is not a read-only query. |
| {source}`PreprocessingRoundOptions <src/smt/preprocessing_tactics.hpp>` | Immutable settings for one repeated round. The round reads execution services from its tactic context. |
| {source}`FiniteDomainPreprocessInput <src/smt/preprocessing_tactics.hpp#L65-L75>` | Assertion snapshot for symmetry, shared raw at-most-one clauses, and any previously computed symmetry result. |
| {source}`ModelReconstruction <src/smt/model_reconstruction.hpp>` | Goal-local typed conversion chain and forced equality/disequality pairs needed by later stages. |

The term manager owns every original and generated term. Results contain IDs
and vectors of IDs, not private term trees. Rewriting preserves DAG sharing
through memoized visits and hash-consed construction. Stable IDs survive arena
growth; borrowed references to term nodes may not. A pass must snapshot the
children and payload it needs before constructing another term.

The raw finite-domain and symmetry snapshots are intentional: simplification
can erase a choice disjunction that justified a useful clause. Later emission
must retain that original justification. They belong to the current query and
its term manager, not to a process-wide cache.

(preprocessing-order)=
## Execution order

{source}`prepare_and_execute_preprocessing_strategy
<src/smt/preprocessing_strategy.cpp#L554-L641>` executes the initial sequence,
analyzes its result, then constructs and executes the remaining strategy.
The following order is encoded by
{source}`make_preprocessing_strategy <src/smt/preprocessing_strategy.cpp#L419-L552>`.
A terminal result can stop the enclosing sequence early.

| Stage | Work and condition |
| --- | --- |
| 1. Simplify extended terms | If enabled, simplify constant-valued ITE comparisons, then apply bounded structural `ite`/`div`/`mod`/`abs` rewrites. The constant-ITE pass also has its own switch. |
| 2. Eliminate extended operators | Always translate remaining extended terms into Boolean structure, linear terms, and fresh definitions. |
| 3. Normalize the initial goal | Optional arithmetic atom canonicalization, then optional initial bounded Boolean flattening. |
| 4. Choose a profile | Analyze the normalized goal; retain any raw finite-domain clauses and symmetry snapshot needed later. |
| 5. Profile-specific preparation | Integer profiles run bounded syntactic solve-eqs. The large-input bypass can run one solve-eqs pass. Integer profiles with an enabled round normalize Boolean negations before that round. |
| 6. Repeat a simplification round | Only when the selected profile enables it and the pass budget is positive; see the round below. |
| 7. Emit finite-domain constraints | Selected symmetry restrictions, raw/post-simplification at-most-one clauses, and equality definitions. |
| 8. Normalize generated arithmetic | Optional canonicalization catch-up, then required arithmetic-equality elimination. |
| 9. Eliminate one-sided Real variables | Optional monotone elimination. |
| 10. Purify mixed terms | Introduce the required theory-interface proxies; optionally canonicalize their comparisons again. |
| 11. Normalize the final Boolean structure | Optional implication rewriting and bounded associative flattening. |

`--preprocessing-mode=off` disables the repeated round. It does **not** skip
the initial sequence, arithmetic-equality elimination, purification, or final
Boolean normalization. Those stages still honor their individual switches;
the required translations have no such bypass. Integer syntactic solve-eqs
also runs independently of this mode. For surviving goals, `--preprocess-only` stops before engine execution and
returns `unknown` for the inspection request; preprocessing can still report
UNSAT if every alternative has already reduced to false.

(preprocessing-profiles)=
## Profiles and repetition

The options default to `auto`, with the **legacy** automatic selector.
{source}`Profile analysis <src/smt/preprocessing_strategy.cpp#L223-L417>`
separates integer logic policy from the ordinary UF/Real policy.

| Requested mode or logic | Repeated-round policy |
| --- | --- |
| Ordinary `off` | No repeated round. |
| Ordinary `basic` | Round with the selected optional transformations. |
| Ordinary `strong` | Round with at least three base passes, equality bridging and finite-domain strengthening enabled by the profile. Individual applicable switches still matter. |
| Ordinary `auto` | Choose a large-input bypass, a guarded finite-domain/compact basic profile, a single-assertion profile, or strong. |
| `QF_LIA` with `auto` | Adapt to `off`, retaining integer syntactic solve-eqs and the stages outside the loop. |
| `QF_UFLIA` with `auto` | Adapt to the integer-safe `basic` profile. |
| Either integer logic with explicit `basic`/`strong` | Enable the integer-safe round; strong additionally enables finite-domain strengthening. |

The legacy selector uses the number of assertion roots and reachable-term
counts. Consequently, packaging a conjunction as one assertion can change
its selected profile. The opt-in structural selector inspects the implicit
positive conjunction: nested positive `and` nodes contribute conjuncts, while
an `and` under `or` or negation stays inside its containing conjunct.
It chooses, in order, large-input bypass, guarded finite-domain basic,
compact clause-like basic, or strong. Its thresholds are explicit options.
This is an original empirical selection policy, not a different simplification
algorithm or a paper-backed optimal choice. The
{source}`selector design and earlier evaluation <docs/structural-auto-preprocessing.md>`
record why it remains opt-in.

Both integer profiles first run `qf-lia-solve-eqs`, with at most eight syntactic
substitution passes and no Gaussian elimination. Their optional repeated round
uses Boolean/value propagation and syntactic solved-constant substitution,
but disables Gaussian elimination, equality bridging, ground-congruence
generation, and ground-unit substitution. It starts with Boolean NNF rewriting.
The separately optional monotone pass recognizes only Real variables, so it
does not eliminate integer variables. Strong integer profiles can recognize
finite domains of distinct integer numerals; those numerals never become
permutable symmetry values.

The base repeat budget is `preprocessing_passes` (default 4). Once that budget
is reached, another round requires a strict reduction in reachable terms of
at least `preprocessing_adaptive_shrink_percent` (default 10), with at most
`preprocessing_adaptive_extra_passes` extra rounds (default 4). Strong and
ordinary single-assertion profiles have a minimum base budget of three.
Unchanged assertion vectors stop the loop earlier. Reachable terms measure
the current formula, not the total arena: old nodes remain allocated.

(preprocessing-round)=
## One simplification round

The round in
{source}`preprocessing_tactics.cpp <src/smt/preprocessing_tactics.cpp#L643-L666>`
has this order:

1. Equality bridging, if selected.
2. Ground-congruence clauses, if selected. The reusable group also offers
   finite-domain evaluation, but production profile construction disables it.
3. Boolean unit propagation, then forced-atom propagation.
4. Solved-constant substitution, optionally with Gaussian candidates, then
   optional direct or guarded ground-unit substitution.
5. Unit-equality consequences for observed equality atoms.

Later phases can expose work for the next round. Reordering them changes
both that opportunity and which justification/model records must remain live.

### Propagating facts

{source}`propagate_boolean_units <src/smt/preprocessing.cpp#L4763-L4799>`
collects Boolean units from the assertion conjunction and simplifies formulas
under those assignments. For $p\land(\neg p\lor q)$, it retains the fact
$p$ and reduces the second conjunct to $q$. It does not guess a truth value
for a literal merely because it occurs with one polarity.

{source}`propagate_forced_atoms <src/smt/preprocessing.cpp#L4801-L4894>`
extends this to asserted predicates and equality/disequality facts. Equalities
seed a congruence closure; disequalities are indexed modulo that closure.
Thus $a=b$ can simplify a test of $f(a)=f(b)$, and $a=b$ together with
$f(a)\ne f(b)$ exposes a contradiction. Known facts remain represented in
assertions or reconstruction data; they cannot be discarded as if their
atoms were independent propositional variables.

For example, the conjunction $a=b\land b=c\land a\ne c$ has three atoms
each used in just one polarity, yet is unsatisfiable. Applying the propositional
pure-literal rule to those theory atoms would destroy their shared meaning.

The final unit-equality phase adds equalities already queried by the input
when they follow from asserted equality facts. Its job is to expose useful
consequences before SAT encoding, not to replace the later EUF engine.

### Eliminating definitions

{source}`substitute_solved_constants <src/smt/preprocessing.cpp#L4676-L4761>`
looks for asserted definitions $x=t$. It rejects self-reference, dependencies
on other pending definitions, and substitutions exceeding its definition-size
or estimated-growth caps. Multiple rounds can therefore remove a chain that
one round conservatively leaves in place. This is a bounded definitional
eliminator, not unrestricted equation solving.

Optional Gaussian candidate generation isolates a Real variable in an
asserted linear equality, such as $2x+y=6$, giving $x=3-y/2$. Candidates then
pass the same substitution guards. It is not a full matrix elimination pass.
The division is valid over the reals; applying it as an unconditional integer
substitution would lose divisibility constraints. Integer profiles disable it.

Ground-unit substitution instead picks representatives of equal ground terms
and rewrites their uses. Its guarded variant accepts only sufficient reachable
size reduction or a large rewrite count with bounded assertion growth; the
guard is a cost heuristic. Both variants are off by default. Directed
substitutions are recorded when a model is requested, including the extra
ground-term restoration information needed for UF interpretations.

### Deriving equality and congruence clauses

{source}`EqualityBridge <src/smt/preprocessing.cpp#L3602-L3610>` examines
conjunctive equality information within the branches of a disjunction.
For

$$((a=b)\land(b=c))\lor((a=d)\land(d=c)),$$

both branches imply $a=c$, so the pass can add that consequence. It uses
bounded branch/pair exploration and looks through the supported `and`/`or`
structure. It does not infer a unit from a disjunction occurring in an arbitrary
negative context. This is a narrow equality-diamond rule, not a complete
minimum-transitivity-constraint algorithm.

The optional {source}`CongruenceDefinitionGenerator
<src/smt/preprocessing.cpp#L5132-L5195>` adds bounded clauses expressing
congruence between applications of the same function already present in the
goal. For unary $f$, the clause is $a\ne b\lor f(a)=f(b)$. It creates neither
new applications nor a separate propositional encoding of the entire theory.

(preprocessing-extended)=
## Extended terms and arithmetic normalization

### Simplify before introducing definitions

{source}`simplify_extended_terms <src/smt/preprocessing.cpp#L4425-L4466>`
performs a bounded number of bottom-up rewriting rounds. Structural ITE
identities, constant folding, and arithmetic-extension identities can remove
operators before fresh variables become necessary. Equality distribution over
an ITE has separate growth controls and is off by default: replacing a shared
conditional at every use can enlarge the formula substantially.

A constant-valued ITE is an arithmetic term whose leaves are numerals, possibly
scaled or offset. For instance, `ite(p, 2, 5) < 4` decides the two branches
differently; `ite(p, 2, 5) < 6` is true in both branches.
{source}`simplify_constant_ites <src/smt/preprocessing.cpp#L4328-L4423>`
computes possible leaf values and pushes a comparison with a constant into
the conditional only where needed. Uniform outcomes collapse immediately.
Leaf sets and comparison results are cached across the pass, so shared
subterms are not repeatedly expanded for every atom.

This implements the constant-versus-CITE part of Bromberger's §8.2.3
`csimp` procedure {cite:p}`bromberger2019`. The two-nonconstant-CITE equality
case is not implemented. Primo hoists conditionals across sums and constant
scales using exact identities; it does not divide coefficients out of integer
comparisons. This pass precedes extended-term simplification because it can
produce Boolean ITEs that those structural rules simplify further. Fewer
auxiliaries can come at the cost of more Boolean structure; old measurements
remain in the preservation map rather than serving as new qualification.

### Translate remaining operators

{source}`lower_extended_terms <src/smt/preprocessing.cpp#L4468-L4614>`
uses one memoized traversal and the context's fresh-constant capability.

| Input | Replacement and side constraints |
| --- | --- |
| Boolean `ite(c, a, b)` | $(c\land a)\lor(\neg c\land b)$, with no fresh term variable. |
| Non-Boolean `ite(c, a, b)` | Fresh $v$ with $c\Rightarrow v=a$ and $\neg c\Rightarrow v=b$. |
| Integer `abs(t)` | The same conditional encoding for `ite(0 <= t, t, -t)`. |
| Integer `div(t,k)` / `mod(t,k)`, positive constant $k$ | Shared integer quotient $q$ with $0\le t-kq\le k-1$; division uses $q$ and remainder uses $t-kq$. |

Definitions are emitted before the assertion that needs them. Within the call,
equal rewritten numerator/divisor pairs share a quotient. Negative divisors
are normalized earlier; zero or nonconstant divisors are unsupported. The
[integer chapter](lia.md#input-lowering-and-integer-bounds) gives the uniqueness
argument, signed example, and subsequent bound handling.

### Normalize atoms at the right boundary

The optional early arithmetic canonicalizer converts proportional atoms to
a common core-term form with coprime integer coefficients. It can therefore
merge their SAT abstractions. This differs from the always-used
[leading-coefficient normalization](lra.md#from-atoms-to-bounds)
inside LRA preparation, whose purpose is sharing a polynomial row. The
{source}`canonicalizer <src/smt/preprocessing.cpp#L499-L599>` is off by default;
if enabled it runs initially, at catch-up, and for comparisons after purification.

Required {source}`arithmetic-equality elimination
<src/smt/preprocessing.cpp#L2537-L2544>` translates $a=b$ to
$(a\le b)\land(b\le a)$ for **both Real and Int**, despite the helper's
`lower_real_equalities` name. Negation remains Boolean structure for SAT.
This stage follows equality-producing passes, including finite-domain
generation; otherwise a late generated disequality could bypass the encoding.
The opt-in guarded shortcut distributes an `or` with exactly one arithmetic
equality child over these two comparisons. General distribution is excluded
because it can grow exponentially.

(preprocessing-finite)=
## Finite domains and symmetry

A recognized choice $t=c_0\lor\cdots\lor t=c_{n-1}$ permits extra Boolean
reasoning when the values are known distinct. The
{source}`finite-domain generators <src/smt/preprocessing.cpp#L4896-L5102>`
use explicit asserted disequalities; distinct integer numerals supply another
supported proof of distinctness. Arbitrary values with different `TermId`s
are not automatically distinct.

At-most-one (AMO) clauses state
$\neg(t=c_i)\lor\neg(t=c_j)$ for distinct values. Raw clauses discovered
before simplification are emitted later and deduplicated against a second
scan after simplification. The generator also handles observed equality atoms
sharing a term and known disequal endpoints without requiring a complete
choice disjunction. For recognized same-domain terms known disequal, it can
add $\neg(s=c_i)\lor\neg(t=c_i)$. Forced equalities/disequalities carried
by the goal keep that test meaningful after rewriting.

If the input already mentions $s=t$ for two recognized same-domain terms,
equality-definition clauses connect that atom to their values. For each $c_i$,
they express $(s=c_i\land t=c_i)\Rightarrow s=t$ and both directions of
value transfer under $s=t$. The pass does not create equality definitions
for every possible term pair. Automatic equality-definition promotion has
separate domain/clause/term caps and is disabled by default.

{source}`UfSymmetryBreaker <src/smt/preprocessing.cpp#L5197-L5241>` has a
different contract. It checks syntactic invariance under permutations of a
recognized set of uninterpreted constants, then restricts equivalent models
to a canonical ordering. Permutations are checked through generators, with
commutative comparison for equality, `and`, and `or`. Only candidate terms
containing none of the permuted values enter the fixed-term sequence.

With value precedence, a later value may be used only after its predecessor
has appeared in that sequence. The alternative mode emits weaker exclusions.
Moving-only groups are rejected, and arithmetic numerals are never permuted.
Unlike AMO consequences, symmetry constraints need not hold in every original
model; the justification is that some equivalent renamed model satisfies them.
The {source}`symmetry design note <docs/papers/smt-symmetry-breaking.md>`
maps this subset to Déharbe et al.'s constant-permutation restriction.
Theorem 5 supplies the model-renaming justification and Lemma 6 the
permutation-generator test {cite:p}`deharbe2011symmetry`; the fixed-term value
precedence rule is the local specialization described in the design note.
General graph-automorphism detection is outside Primo's implementation.

For a domain of size $n$, pairwise AMO generation can produce
$n(n-1)/2$ clauses for one term. Equality-definition and congruence generation
also expand the formula. Their guards and counters are substantive parts of
the implementation, not merely presentation options. `FiniteDomainEvaluator`
offers a separate bounded enumeration simplifier; the production profile
builder currently selects neither its round nor its final-stage invocation.

(preprocessing-final)=
## Final passes and reconstruction

### One-sided Real variables

The optional {source}`monotone eliminator
<src/smt/preprocessing.cpp#L5250-L5509>` tracks the direction in which each
linear inequality constrains a Real constant, accounting for Boolean polarity.
If every occurrence permits moving $x$ in the same direction, a sufficiently
extreme value can satisfy those occurrences together. For
$x>y\land x>z$, removing the two constraints is satisfiability-preserving:
after choosing $y,z$, reconstruct $x=\max(y,z)+1$.

Equalities, opposing directions, uninterpreted applications involving the
variable, and unsupported leaves disqualify it. Negative occurrences reverse
the direction; an atom seen in both polarities cannot simply be discharged.
The pass records the eliminated variables and their constraints for model
construction. When several eliminated variables share a constraint, whichever
is valued last repairs that constraint after the other values are known.
Primo uses a deterministic order; it does not reject such dependencies as
cycles. The {source}`original rule and attribution note
<docs/papers/monotone-variable-elimination.md>` explain the one-sided
Fourier–Motzkin observation and why it is not an objective-driven LP
dominated-column rule.

### Mixed purification and Boolean structure

{source}`make_purify_mixed <src/smt/solver_engines.cpp#L12578-L12625>`
replaces mixed applications/expressions with interface proxies and records
application-to-proxy model conversions. [Theory combination](combination.md)
explains the interface and its defining constraints. This step runs after
ordinary arithmetic-equality elimination and handles its own definitions.
If canonicalization is enabled, the post-purification pass is restricted to
comparisons so it preserves the intended interface equalities.

Finally, {source}`make_normalize_boolean
<src/smt/preprocessing_tactics.cpp#L485-L531>` can rewrite implications and
flatten nested same-kind `and`/`or` terms. Implication rewriting defaults to
off; its clause-only mode recognizes a bounded clause shape, while `all`
allows the broader rewrite. Initial flattening defaults to off; final
flattening defaults to an arity threshold of 2048. A second application point
is needed because intervening rewrites can introduce new nesting.
The term constructors retain sharing and local simplifications; they do not
recursively flatten every shared Boolean node. This distinction prevents
construction from repeatedly copying large operand lists.

(preprocessing-example)=
### Follow an integer goal

Take the `QF_LIA` assertions $x=y+2$ and $y\ge1$, with model production and
the default automatic mode. There are no extended terms to eliminate. The
profile adapts `auto` to `off` for the repeated round, but integer syntactic
solve-eqs still replaces $x$ by $y+2$ and appends a `LiaSubstitutionConversion`.
The remaining defining equality simplifies to true. Arithmetic-equality
elimination and purification have no substantive work left here; final
Boolean normalization leaves the bound on $y$ for the integer engine.

A reduced model with $y=1$ is reconstructed with $x=3$. The goal carries that
conversion through every intervening `with_assertions` update. This explains
both why preprocessing-off is not an identity operation and why returning
only transformed assertions is an incomplete tactic interface.

More generally, Boolean facts, directed substitutions, integer substitutions,
monotone eliminations, and purification proxies have distinct conversion
records. The [model chapter](models.md#following-a-model-back-through-preprocessing)
describes their reverse replay and validation against original assertions.

(preprocessing-evidence)=
## Correctness checks and evidence

The {source}`preprocessing unit tests <tests/unit/preprocessing_tests.cpp>`
exercise transformations and their guards: deep/shared DAGs, substitution
cycles and growth limits, equality consequences, finite-domain distinctness,
ITE arena growth, shared quotients, and monotone polarity. The
[composition checks](tactics.md#checks-and-implementation-status) test complete
legal profiles and model conversion, since individually correct passes can
still compose incorrectly. Later-generated arithmetic equalities and raw
finite-domain clauses are particularly important boundary cases.

Useful counters distinguish selection from actual work: profile and limit
counters describe policy; `:preprocessing.passes` and adaptive-pass counts
describe repetition; transformation counters describe rewrites, generated
clauses, rejected candidates, and cutoffs. Tactic invocation alone does not
show that an algorithm transformed an input. Historical performance and fuzz
records are located in the [preservation map](../coverage/tactics-preprocessing.md).
This chapter adds no new solver qualification or default promotion.

(preprocessing-sources)=
## Sources and implementation status

- The strategy and model-conversion framing follows de Moura and Passmore,
  §3 {cite:p}`deMoura2013`; Primo's concrete subset is in [tactics](tactics.md).
- Bromberger's §§8.2.2–8.2.4 {cite:p}`bromberger2019` provide the comparison
  for sharing, arithmetic flattening, ITE simplification, definitions, and
  CNF preparation. Primo hash-conses on construction, keeps its own polynomial
  representation, and implements the CITE subset described above. The thesis's
  broader ITE reconstruction, pseudo-Boolean rewriting, and optimized CNF
  selection are not all implemented. Its §7.2.4 arithmetic search preparation
  is a separate subject covered by [LIA](lia.md).
- Arithmetic atom normalization is related to Monniaux's §2.1
  {cite:p}`Monniaux2009`; [LRA](lra.md) distinguishes the two normal forms
  actually used by Primo. This is not an implementation of the paper's
  floating-point oracle.
- The symmetry and monotone design notes cited above give their restrictions
  and attribution. Equality bridging and the bounded finite-domain clauses
  are the elementary rules stated here, not complete implementations of
  transitivity-constraint generation or finite-model finding.

Current limitations and optional paths are described with their mechanisms.
Further investigation belongs in the {source}`open-leads register
<docs/open-leads.md>`, including ITE preprocessing and strategy synthesis;
those proposals are not additional stages of the current pipeline.
