# Exploiting directly asserted quantifiers

**Status: Open lead. Owner: unassigned.**

Investigate a simpler path for quantified obligations whose truth value is
fixed by an active assertion. The motivating observation is that many inputs
may assert quantifiers directly. Measure that proportion, including the work
created by those roots, before choosing an optimization.

## What is already known at the root

An assertion `forall x. B(x)` requires the universal to be true. An assertion
`not (forall x. B(x))` requires it to be false and hence needs a counterexample
witness. The same distinction applies to positive and negated existentials:

| Active assertion | Fixed obligation |
| --- | --- |
| `forall x. B(x)` | Generate universal instances of `B`. |
| `not (exists x. B(x))` | Generate universal instances of `not B`. |
| `exists x. B(x)` | Introduce witnesses satisfying `B`. |
| `not (forall x. B(x))` | Introduce witnesses satisfying `not B`. |

The {source}`current root clausifier <src/smt/clausifier.cpp#L248-L320>` already
splits asserted conjunctions and sends each directly asserted quantifier or
its negation to SAT as a unit. Thus SAT does not need to decide its value.
The usual guarded instance `not Q or B[t]` reduces to `B[t]` when the root
unit is `Q`; SAT can already make that reduction propositionally.

The quantifier component still discovers obligations by reading the
{source}`complete assignment <src/theory/quantifiers/quantifier_component.cpp#L91-L108>`.
The possible savings are in registration, repeated classification, guard
handling, and rounds spent waiting to create witnesses, beyond the unit
propagation that already happens.

## Candidate changes

First record a fixed root obligation separately from conditional quantified
atoms, while retaining the general SAT mapping where other uses require it.
For such an obligation, the effective universal/existential role can be decided
once for its active lifetime. This can be done directly from its signed form;
it does not depend on enabling [NNF](quantifier-nnf.md).

An initial experiment could prepare witnesses for fixed existential obligations
before the first SAT solve. They would then enter term pools and the ground
problem immediately. A second experiment could simplify generated instance
guards or avoid unnecessary SAT atom bookkeeping for isolated fixed roots.
Removing an atom also used elsewhere requires preserving every such use and
its explanations; syntactic top-level occurrence alone does not authorize it.

Keep universal generation bounded and scheduled so that ground search gets
time to find conflicts. The {source}`dispatcher guard
<src/smt/component_dispatcher.cpp#L132-L150>` deliberately waits for a complete
candidate: generating work whenever root quantifiers became assigned previously
interrupted search too early. The
{source}`complete-assignment regression <tests/unit/quantifier_solver_tests.cpp#L394-L424>`
must remain effective. Preparing a finite initial set of witnesses is distinct
from repeatedly restarting SAT to enumerate more universal instances.

## Scope and correctness obligations

Fixed means required by an active assertion, not merely present in its syntax.
An asserted conjunction requires its conjuncts; `Q or r`, `r implies Q`,
and a quantified formula inside another quantifier's body need their actual
Boolean or enclosing-instance conditions. Detect roots by a sound required-value
analysis, not by counting occurrences or checking syntactic polarity alone.
The complementary [relevance lead](quantifier-relevance.md) asks which
conditional obligations can be omitted altogether for a particular candidate.

The root fact is permanent only for its active lifetime. The current quantified
route rebuilds per query; `pop` removes assertions before the next solve.
Future retained quantified sessions would need activation guards or equivalent
scoped ownership. A root asserted under selector `s` justifies an instance
under `s`, not an unconditional permanent clause. Witness constraints and
cached instances must not outlive that condition. Preserve binder-dependent
witnesses for nested obligations, and do not strengthen the current SAT
completion condition merely because a quantifier was found at the root.

This is a proposed local specialization of the existing
[four polarity cases](../implementation/quantifiers.md#universal-instances-and-existential-witnesses)
and assertion/guard contracts, not a new instantiation algorithm.

## First evaluation and acceptance

Count fixed and conditional quantified atoms, witnesses, generated lemmas,
classification visits, and SAT rounds, including the first round that can
use a required witness. Compare the unchanged route with one specialization
at a time; evaluate NNF separately before combining the two changes.

Regressions must cover all four signed root forms, conjunctions, conditional
occurrences of the same term, contradictions between root requirements,
nested alternation, Boolean witnesses, backtracking, repeated queries and
push/pop, and ground-search progress. Extend the existing
{source}`polarity and nesting tests <tests/unit/quantifier_solver_tests.cpp#L226-L244>`
and preserve the pool-export/witness correspondence used by external checking.
Run fresh [correctness gates](../development/correctness.md) and paired
evaluations, reporting runtime/PAR2 and peak memory as well as saved
rounds and bookkeeping. No implementation, corpus proportion, or performance
gain has yet been established.

Contributors may use any suitable machine. In the maintainer workspace, these
batches run on SLURM; see the {ref}`execution policy <run-the-applicable-local-checks>`.
