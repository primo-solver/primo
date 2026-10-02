# GCD reasoning with bounded variables

**Status: Open lead. Owner: unassigned.**

Extend the {ref}`row divisibility check <lia-gcd>` to
use bounds on variables that are not fixed. This carries forward
{source}`lead 12e <docs/open-leads.md#L1903-L1946>`; it is not a newly discovered
gap or an implemented procedure.

## Why the existing test misses it

With integer $x,y$, the constraints $2\le4x+4y\le3$ are impossible:
$4x+4y$ is a multiple of $4$, and the interval $[2,3]$ contains none.
The real relaxation is feasible. The current
{source}`GCD check <src/theory/arithmetic/lia_gcd_consistency.cpp>` substitutes
only fixed values; it does not use an interval to exclude all multiples.
The existing {source}`whole-solver regression
<tests/unit/qf_lia_solver_tests.cpp#L252-L274>` named
`gcd_uncatchable_divergent_system_hits_the_node_limit` records the resulting
node-limit case. A row may be scaled during preparation, so the proposed
test must work on the stored rational row, not assume the input's coefficients
survive unchanged.

## Paper basis and proposed rule

Bjørner and Nachmanson, [*Arithmetic Solving in Z3*](https://doi.org/10.1007/978-3-031-65627-9_2),
CAV 2024, §4.3, describes an extension using bounded variables. The existing
{source}`paper note <docs/papers/arithmetic-solving-in-z3.md>` already reviews
this section. Its printed empty-interval inequality has the wrong direction:
there is no integer in $[L/g,U/g]$ when
$\lceil L/g\rceil>\lfloor U/g\rfloor$, not when the reverse strict inequality
holds. Derive the condition before implementing it.

The following is a proposed general residue formulation, rather than a
transcription of the paper's restricted coefficient grouping. After exact
scaling and fixed-value substitution, write a row as

$$a\cdot x+b\cdot y+c=0,$$

where all coefficients are integers, $y$ is integer-valued, and bounds on $x$
give a sound enclosure $L\le a\cdot x\le U$. Let $g>0$ be the GCD of the
nonzero coefficients in $b$. Any solution requires
$a\cdot x\equiv-c\pmod g$. Thus the row is impossible if

$$\left\lceil\frac{L+c}{g}\right\rceil>
  \left\lfloor\frac{U+c}{g}\right\rfloor.$$

This keeps the offset explicitly. The paper instead writes a test with
$\gcd(b,c)$; since that divisor divides $c$, an unshifted multiple test is a
weaker necessary condition. Do not mix its divisor with the stronger
residue formulation. Passing either test is inconclusive: an interval can
contain a suitable residue that the actual bounded sum cannot attain.

## Design and qualification still needed

Keep this in `theory/arithmetic/`, operating on the integer completion's
private `LraTableau`. Design a cheap grouping of bounded terms and a work cap;
trying every partition would introduce exponential work. Compute endpoints
according to coefficient signs, handle strict bounds with exact
`DeltaRational` arithmetic or a proved conservative relaxation, and handle
an empty or zero-coefficient remainder separately. Never infer integrality
from a variable's basic/nonbasic position.

The explanation must retain the reasons for every bound used in the interval
and every fixed substitution. A conflict may depend on a prior cut, whose
antecedents must still be available through `LiaExplanation`. No heuristic
choice of grouping may weaken these proof obligations.

Before implementation, count eligible rows and missed residue conflicts on
representative QF_LIA and QF_UFLIA inputs. The older register's sample found
many timeouts that never reached integer completion; this rule cannot help
those queries. Add regressions for offset residues, negative coefficients,
fractional normalization, endpoints exactly on a multiple, auxiliary variables,
and explanation validity. Compare total solving time and branch nodes, then
run fresh fuzzing and independent SAT-model validation on SLURM. The existing
crafted case establishes a coverage gap, not a performance benefit.
