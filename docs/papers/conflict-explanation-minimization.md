# Conflict explanations, minimization, and shared antecedents

**Status: Literature intake and a small backend ablation are complete; read-only theory
bound instrumentation is implemented, while explanation transformations remain proposed.** This note separates improvements
to an arithmetic explanation from SAT conflict analysis and learned-clause
minimization. The experiment below changes no solver implementation or defaults.

## Which problem are we trying to solve?

Four operations can improve a conflict, with different objectives:

| Operation | What changes | Relevant literature |
| --- | --- | --- |
| Learned-clause minimization | Delete literals from an already derived clause using their trail reasons. | Sörensson–Biere 2009; Fleury–Biere 2021 §2. |
| All-UIP shrinking | Resolve further and introduce replacement literals, potentially replacing several consequences with a common antecedent. | Feng–Bacchus 2020; Fleury–Biere 2021 §§3–4. |
| Arithmetic bound weakening | Use weaker sufficient bounds in the conflict, exploiting its remaining numerical margin. Clause length can stay unchanged while the theory lemma becomes more general. | King 2014 §2.2.9. |
| Arithmetic explanation regression | Expand internal derived constraints to assertions with SAT names while preserving their justifications. | King 2014 §2.2.4. |

These also differ from [SOI row-subset reduction](../implementation/lra.md#optional-sum-of-infeasibilities),
which is already implemented as an optional part of SOI. A subset-minimal conflict
has no removable premise; a minimum-cardinality conflict has the fewest premises
among all candidates. Neither property by itself chooses the best literals for
future search. Shortening a clause by substituting new literals also need not
produce a logically stronger clause in the Boolean abstraction.

## Primary readings and missing coverage

### Sörensson and Biere: classical learned-clause minimization

Niklas Sörensson and Armin Biere, *Minimizing Learned Clauses*, SAT 2009,
pp. 237–243 {cite:p}`sorensson2009minimizing`.
[Author PDF](https://cca.informatik.uni-freiburg.de/papers/SoerenssonBiere-SAT09.pdf).
Read the Introduction, Minimization section and experimental design.

The starting point is a first-UIP clause, where UIP means unique implication
point in the trail's implication graph. Local minimization removes a literal
when its reason's other literals are already covered. Recursive minimization
follows further reasons and removes the candidate when every path terminates
at covered literals. Resolution justifies each removal. The output is a
subclause of the input; it does not introduce a fresh shared antecedent.

Required state includes the trail, reason clauses, candidate marks and cached
redundancy results. Decision-level checks can reject unproductive traversals.
The paper documents MiniSat's technique dating from 2005. Primo's bibliography
previously omitted this reference. **Status: SAT-backend responsibility; no
Primo implementation proposed.** Its result does not establish that the linked
backend performs every possible reduction of an external theory explanation.

### Feng and Bacchus: replacement by UIPs at earlier levels

Nick Feng and Fahiem Bacchus, *Clause Size Reduction with all-UIP Learning*,
SAT 2020, pp. 28–45 {cite:p}`feng2020alluip`.
[Deposited article](https://pmc.ncbi.nlm.nih.gov/articles/PMC7326470/).
Read §§1–3, including the clause-learning framework and all-UIP variants.

The paper continues resolution beyond the current decision level to seek UIPs
on earlier levels. Its variants constrain the increase of LBD, the number of
distinct decision levels in a learned clause, and accept useful size reductions.
Unlike classical minimization, the resulting clause need not be a subclause of
the first-UIP clause. This is directly relevant to replacing several propagated
consequences with a shared earlier cause.

The state needs reasons, per-literal levels, trail order, per-level working sets
and a retained original clause if an attempted reduction is rejected. General
resolution remains sound even when the chosen replacement is not useful.
**Status: missing bibliography and comparison; backend audit first.** Do not
duplicate the SAT implication graph in LRA. The paper's empirical benefits are
not Primo results or a guarantee for the example below.

### Fleury and Biere: distinguish minimization from shrinking

Mathias Fleury and Armin Biere, *Efficient All-UIP Learned Clause Minimization*,
SAT 2021, pp. 171–187; extended version, FMV-TR-21-3
{cite:p}`fleury2021alluip`.
[Extended author PDF](https://fmv.jku.at/papers/FleuryBiere-FMV-TR-21-3.pdf).
Read §§1–4, Algorithms 1–4, Theorems 13, 14 and 16, and Example 15 of the
extended version.

Section 2 formalizes redundancy relative to trail reasons. Sections 3–4 distinguish
literal deletion from shrinking, which may introduce replacements, and integrate
shrinking with minimization. Processing decision-level slices from low to high
preserves reusable redundancy information; newly introduced literals require care
with cached negative results. Section 4 qualifies the traversal cost: direct
trail iteration is linear when decision-level slices are disjoint, as without
chronological backtracking. Overlapping slices under chronological backtracking
can make naive trail scans quadratic. Its binary-heap alternative has a bound
of $O(g\,n\log n)$, where $g$ counts slices and $n$ is the largest slice's
implication-graph size. Linear minimization caching does not by itself make
arbitrary shrinking or repeated explanation expansion linear.

**Status: missing reference; backend-owned comparison.** The paper evaluates
CaDiCaL, Kissat and Satch. That history makes a version/configuration audit
necessary before assuming the mechanism is absent from Primo's backend. It does
not certify the effective configuration or external-reason handling of our build.

### King: ordinary simplex conflict strengthening and provenance

Tim King, *Effective Algorithms for the Satisfiability of Quantifier-Free Formulas
Over Linear Real and Integer Arithmetic*, PhD thesis, NYU, 2014
{cite:p}`king2014`.
[Thesis PDF](https://cs.nyu.edu/media/publications/king_tim.pdf).
Read §§2.2.4 and 2.2.9, especially printed pp. 61–62 and 74–76; compare
§3.3.5 and FMCAD 2013 §V-B {cite:p}`King-fmcad2013` for SOI.

Section 2.2.4 attaches explanations and assertion indices to constraints.
Derived constraints must depend on already explained constraints. Its
`Regress2Assertions` rule recursively expands internal constraints lacking an
assertion index; a propagated constraint that re-enters as an assertion can be
used through that index. It is not unconditional expansion to decision literals.

Section 2.2.9 first establishes minimality of a blocked-row conflict in its
representation. It then observes that choosing the strongest available bounds
can yield a less useful lemma. A greedy procedure moves to weaker asserted
bounds while a positive conflict surplus remains. Ordered per-variable
constraints, explanations and exact arithmetic support that choice. This is
distinct from §3.3.5's SOI row-selection procedure.

**Status: thesis already cited, these mechanisms omitted from our mapping and
not implemented as general LRA explanation transformations.** Candidate traversal
and provenance costs need measurement; no constant-time whole-minimizer claim is
made here.

### Van Gelder: ordered minimization proofs and cached failures

Allen Van Gelder, *Improved Conflict-Clause Minimization Leads to Improved
Propositional Proof Traces*, SAT 2009, pp. 141–146
{cite:p}`vangelder2009minimization`.
[Primary PDF location](https://users.soe.ucsc.edu/~avg/Papers/ccmin-avg-sat09.pdf).
Read §§2–5 from the user-supplied local reading copy on 2026-10-26 after remote
download failures. Section 3 describes MiniSat's recursive removal test; §4
retains results from both failed and successful depth-first searches. At
post-order time it classifies vertices as kept, removable or forbidden in
intermediate resolvents. A stack of removable vertices gives an order for
resolving each needed variable at most once. Required state is the reason
graph, per-vertex classification and finishing-order stack.

**Status: missing reference; backend/proof-logging concern.** Section 5 reports
the same minimized clauses and literal order within each clause in the compared MiniSat versions;
the improvement is computation and an ordered proof trace, not finding a shorter
arithmetic explanation. Shared subgraphs and failed-subsearch reuse are useful
checks for a future reason traversal. There is no need to implement a second SAT
minimizer in LRA. The finishing-order invariant must hold before claiming this
proof-trace algorithm; simple repeated expansion is not its implementation.

## Two worked distinctions

### Shared antecedents can require replacement, not deletion

Treat $x=2y$ and $z=y$ as fixed equations for this example. Let `p` name
$y\le1$, `a` name $x\le2$, `b` name $z\le1$, and `r` name $x+z>3$.
Suppose the propagation reasons and theory conflict are

$$
\neg p\lor a,\qquad \neg p\lor b,\qquad
\neg a\lor\neg b\lor\neg r.
$$

Resolution can derive $\neg p\lor\neg r$. It introduces `p` into the conflict
before the two occurrences are combined. A procedure restricted to deleting
literals from $\neg a\lor\neg b\lor\neg r$ cannot produce that clause when
`p` is absent. Ordinary conflict analysis may already reach it; all-UIP shrinking
is relevant when the first-UIP stopping point has left several consequences.
The result depends on trail levels and available reasons.

If the equations are asserted literals, their justifications must also be
included. Tableau substitution might expose $x+z=3y$ and use the bound on `y`
directly, but Primo does not search for a basis solely to shorten explanations.
Neither route guarantees this resolvent on every search schedule.

### A weaker bound can make a more general lemma of the same length

For the row $s=x+y$, suppose asserted bounds include $x\ge2$, $x\ge1$,
$y\ge2$, and $s\le2$. The strongest lower bounds imply $s\ge4$.
Choosing $x\ge1$ instead still implies $s\ge3>2$.
Both conflicts use three bound premises, but the second excludes more cases
when paired with the implication $x\ge2\Rightarrow x\ge1$.
This is the kind of remaining numerical margin exploited by King's method;
literal-count reduction alone would miss its benefit.

## Bounds without a SAT literal

SAT visibility belongs to a bound's naming atom, not to the tableau variable
it constrains. An auxiliary variable can have an asserted bound with a SAT
name. Conversely, an internally derived numerical bound can have no naming
atom at all. A conflict using the latter needs a theory explanation expanding
its derivation to SAT-visible premises; SAT cannot resolve an internal proof
edge that was never supplied as a clause. This is the role of King's
§2.2.4 regression rule, distinct from SAT clause minimization.

Primo's live ordinary LRA path does not maintain a general graph of internally
installed derived bounds. The row-propagation helpers return a numerical value
and the selected bounds' SAT reasons together. For example, an internal result
$s\le7$ may justify propagating a registered atom $s\le8$; that atom's reason
uses the original premises without requiring a SAT atom for $s\le7$. The
tableau receives the bound 8 only when the propagated atom is assigned.

### Tightening a bound and preserving its reason

Bound tightening is implemented. When an assigned atom supplies a stronger
bound compatible with the opposite active bound, `assert_upper` or `assert_lower`
stores the new value with that atom's
negated SAT literal. The replaced bound and its reason are trailed for rollback.
The reason is replaced together with the value; the old weaker bound is not
an additional premise of the new bound. An incoming bound incompatible with
the opposite bound returns their conflict reasons without installing it.

For example, suppose $x\le10$ is active, $x=2y$ is a fixed equation, and `p`
asserts $y\le1$. With row propagation enabled and `t: x<=2` registered, the
theory can propagate `t` with clause $\neg p\lor t$. The SMT callback stores
this reason before delivering `t`. Once SAT assigns `t`, the tableau tightens
the upper bound on `x` to 2 and stores $\neg t$ as its bound reason.
If `r` asserts $x\ge3$, the conflicting bounds give $\neg t\lor\neg r$.
Resolving with the preserved propagation reason gives $\neg p\lor\neg r$.
If the equation is asserted, its premise must also be included in the reason.

For a decision or input assertion `t`, there is no theory derivation of `t`
to expand. For a propagated `t`, later reason requests must use the saved
antecedents; using the tightened bound's own $\neg t$ as the derivation of `t`
would create a cycle. Current reason storage and the
[reason lifetime contract](../implementation/dpllt.md#reason-lifetime-is-separate-from-clause-retention)
keep these two kinds of reason distinct.

### Private bounds and future provenance

Private equality probes are a separate case: their scratch tableaux temporarily
assert a strict comparison using the internal sentinel `Lit{0}`. The
{source}`orientation checks and caller <src/smt/solver_engines.cpp#L4719-L4807>`
test both strict directions before explaining equality from the remaining
SAT-visible premises. The temporary assumption is discharged; zero must not
be delivered as an ordinary premise literal. This scratch procedure does not
provide a general persistent proof graph for live bound propagation.

An eventual implementation that stores derived bounds internally would need
theory-owned proof nodes or handles, context-safe dependencies and expansion
to visible premises. Common-subproof sharing could then be measured before
the SAT boundary. Installed SAT minimization alone cannot perform that missing
arithmetic proof traversal.

## Installed backend metadata check

On 2026-10-26, read-only CLI queries reported:

```text
contrib/local/bin/cadical --version   -> 3.0.0
contrib/local/bin/cadical --help      -> minimize=true, shrink=3,
                                       shrinkreap=true, chrono=1
```

The help describes `shrink=3` as full conflict-clause shrinking. These are
the installed standalone executable's advertised defaults. They do not prove
which library an arbitrary Primo binary links, which options a particular
recipe changes, or how often minimization handles its external reasons.
The proposed backend audit therefore starts with evidence of likely overlap;
it must establish the actual measured binary and configuration before attributing
new reductions to LRA. No SMT input, performance measurement or SLURM job was
run for these metadata queries.

## Sources and current implementation status

| Mechanism and owner | Implemented state and missing operation |
| --- | --- |
| `LraTableau` bound storage | {source}`VarState and bound trail <src/theory/arithmetic/lra_tableau.hpp>` keep one active lower and upper bound, each with a reason. {source}`Bound assertion <src/theory/arithmetic/lra_tableau.cpp#L47-L95>` ignores weaker incoming bounds and trails replaced bounds for rollback. There is no ordered index of all currently asserted alternative bounds for conflict weakening. |
| Ordinary LRA conflict construction | {source}`build_conflict <src/theory/arithmetic/lra_tableau.cpp#L433-L446>` collects the current row's blocking-bound reasons. No shared-antecedent regression or greedy alternative-bound search follows it. |
| SOI conflict reduction | {source}`SOI minimization <src/theory/arithmetic/lra_tableau.cpp#L744-L886>` reduces a set of violated rows using a restricted-row conflict test. It does not implement King's §2.2.9 bound weakening. |
| Dedicated LRA propagation | {source}`Reason delivery <src/smt/solver_engines.cpp#L7147-L7179>` supplies stored external propagation reasons to SAT. [Row propagation](../implementation/lra.md#propagate-and-explain-consequences) proves existing atoms; a numeric derived bound is not installed unless an atom asserting it is assigned. |
| SAT integration | {source}`ExternalPropagator <src/sat/ipasir_up.hpp>` and {source}`CaDiCaL adapter <src/sat/cadical_backend.cpp>` delegate reason callbacks and conflict analysis. They do not expose the complete Boolean reason graph to LRA. The pilot below records its linked version and requested option overrides; coverage of individual explanation reductions remains unmeasured. |

This audit reads Primo's code and the stated primary sections; it does not
copy another solver's source or establish a speedup. The existing
[explanation lifetime contract](../implementation/dpllt.md#reason-lifetime-is-separate-from-clause-retention)
still applies to every proposed transformation.

## Proposed bounded probe

The first experiment should measure opportunity without replacing conflict
clauses. These are prospective owners and checks, not implemented behavior:

| Probe | Data and owning layer | Invariant, counter and rejection condition |
| --- | --- | --- |
| Backend overlap | `sat/` records linked backend version, effective minimization/shrinking options and available statistics. | Keep terms out of SAT. Separate original theory clauses, reason clauses and final learned clauses; do not attribute backend reductions to a theory prototype. |
| Common external reasons | SMT callback state inspects its own stored reasons and surviving assignments, using scratch literal sets and a traversal budget. | Only expand valid earlier antecedents. Count raw/distinct literals, shared leaves, expansions and projected resolvent size. Missing Boolean reasons mean incomplete coverage, not permission to invent antecedents. |
| Weaker sufficient bounds | SMT retains asserted alternative-bound candidates; `theory/arithmetic/` checks candidate row inequalities with `DeltaRational`. | Recompute a contradictory bound exactly for every accepted candidate; retain its actual asserting literal. Count candidates, successful weakenings, surplus and work. Reject improvements based solely on shorter clauses or unsynchronized rollback state. |

Use the shared-antecedent and bound-surplus examples above, then strict-bound,
negative-coefficient, equal-bound/different-reason and backtracking cases. An
eventual implementation must preserve asserted-equality premises, reason lifetime
and noncircular dependencies. Flattening can enlarge explanations, so retain the
original candidate whenever a budget or availability check fails. A naive
recomputation costs $O(k)$ exact operations per candidate on a row with $k$
entries; caching or incremental surplus updates need their own invariants.

After a correctness gate, evaluate on a fixed QF_LRA development manifest and
paired SLURM runs with the same backend settings. Compare time, solved counts,
PAR2, theory-clause size, learned-clause statistics where available, and explanation
work. A reduction already performed by SAT, rare opportunities, or extra work
without a search benefit are grounds to reject a prototype. Record commands,
revision, resources, job IDs and results in the run log. The backend-only pilot
below supplies an initial comparison; the theory probes in this table remain
unimplemented.


## Backend pilot: 34 QF_LRA inputs

The 2026-10-26 diagnostic compared existing CaDiCaL settings before adding any
arithmetic minimizer. The [fixed manifest](https://github.com/primo-solver/primo/blob/9f6b920/benchmarks/qf-lra-explanation-probe.txt)
selects the first two listed inputs per family from the existing stratified
sample: 34 inputs across 17 families. Selection preceded timing. Each native
profile ran twice at a 10-second limit; Z3 ran once. All jobs ran on SLURM compute,
on a Xeon E5-2690 v4, with one physical CPU and 4 GiB per task, at most four
concurrent timing tasks. A separate workspace and compute node kept this probe
apart from the user's existing timing campaign.

The measured binary was Release revision `9f6b920`, reporting
`cadical-3.0.0-7b99c07`. Every native command included `--quiet --stats`.
The {source}`backend constructor <src/sat/cadical_backend.cpp#L109-L134>`
passes the requested overrides to the linked library and rejects invalid names;
this single-query path retains library defaults for other options. The experiment
did not dump every effective library option. The installed CLI help above is
supporting metadata, rather than a complete configuration snapshot.

| Profile | Extra options | Solved per repeat | Timeouts per repeat | Mean PAR2 seconds, repeats 0 / 1 | Direct Dolmen SAT passes |
| --- | --- | --- | --- | --- | --- |
| Default | none | 26 / 26 | 8 / 8 | 5.171 / 5.166 | 13 / 13 |
| Shrinking disabled | `--cadical-option shrink=0` | 28 / 28 | 6 / 6 | 4.272 / 4.252 | 15 / 15 |
| Shrinking and minimization disabled | `--cadical-option shrink=0 --cadical-option minimize=0` | 28 / 28 | 6 / 6 | 4.278 / 4.274 | 15 / 15 |
| Z3 reference | none | 26 | 8 | 5.204 | not selected for model replay |

Mean PAR2 charges each unsolved input 20 seconds and uses elapsed time for a
successful `sat` or `unsat` answer. Errors and timeouts override any answer
printed while terminating. All seven timing runs had zero wrong answers,
non-timeout errors or unsupported outcomes. Answers agreed with declared known
statuses and with Z3 wherever both solved the input; UNSAT answers were compared,
not independently proof-checked. After all timings finished, separate jobs
replayed every successful native SAT row with the same binary and options,
`--check-model`, and independent Dolmen 0.10: **86/86 passed**, including both
repetitions. Model-production and validation time is excluded from the table.

Both disabled-shrinking profiles gained the same two Labyrinth inputs,
`0011-labyrinth-12-0.smt2` and `0051-labyrinth-12-0.smt2`, in each repeat, with no
lost solves. On the 26 commonly solved inputs, disabling shrinking gave median
candidate/default runtime ratios of 1.001 and 0.992. Two cases improved by more
than a factor of two in both repeats: `sc/sc-17.induction3.cvc.smt2` and
`tta_startup/simple_startup_12nodes.missing.induct.smt2`; none regressed beyond
twice the runtime. Disabling minimization as well produced identical collected
SAT conflict, decision, propagation and theory-conflict counts on all 28 solved
inputs. It supplied no additional measured search benefit in this pilot.

This is a useful sensitivity result for backend settings, but it does not count
how many theory explanations SAT already reduces. Neither theory-reason sizes nor
final learned-clause size distributions were captured. Shared-antecedent traversal
and weaker sufficient bounds remain the next proposed theory probes. Keep the
current defaults: this small sample and its model checks do not replace a full
evaluation or a fresh fuzz gate for promotion.

The [committed evidence](../coverage/lra-explanation-backend-pilot.json)
contains per-input results, family summaries, runtime distributions, paired
comparisons, binary hashes and model coverage. The
[run log](../run-log.json) entry
`2026-10-26-lra-explanation-backend-pilot` records job IDs, reproduction commands
and the raw artifact location.

The subsequent [theory-bound structure probe](lra-bound-structure-probe.md#observed-structure)
records actual LRA reasons and numerical interval candidates. It demonstrates
shared ancestry on the constructed example, including a case where flattening
enlarges the reason. It implements observation only; bound weakening and
explanation replacement remain prospective.
