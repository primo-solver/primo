# Observing the structure of LRA bounds

This diagnostic answers two questions: what are the dependencies of bounds
actually propagated to SAT, and which numerical bounds can the reached tableau
state derive without an atom naming their exact value? It changes no theory
clause, SAT setting, or installed bound. It is deliberately expensive.

## Observation contract and design checklist

The arithmetic basis is Dutertre and de Moura's technical report
{cite:p}`dutertre2006simplexreport`, §3.2.2's row interval argument and §3.3's
lexicographically ordered delta rationals. This is a read-only observer of the
existing interval helpers, not a new implementation of an optimized propagation
algorithm. The checklist below is an original diagnostic design.

- The SMT layer owns the [event sink `LraBoundProbe`](https://github.com/primo-solver/primo/blob/47e5dee/src/smt/lra_bound_probe.hpp), which borrows an output stream from
  `CheckSatOptions`. The CLI owns the file for the entire session. No term or
  SAT storage is mutated by the observer; no pointer into growing term storage
  survives a term-creating operation.
- Each online LRA checker writes a JSONL `begin`/`end` block. Comparison atom
  polarities map SAT literals to tableau variables and exact bounds. Problem
  variables also carry term IDs and constant names; slack variables use IDs.
- Assignment events snapshot both current installed bounds. A weaker incoming
  bound can leave the stored reason unchanged. A conflicting incoming bound is
  reported as a rejected assignment and never counted as installed.
- Actual delivered theory propagations record their signed literal, immediate
  reason literals, and whether they came from same-variable, forward-row, or
  reverse-row propagation. Reasons use the existing negated-premise convention.
  Conflict clauses and backtrack removal deltas are recorded separately.
- After nonconflicting bound assertions and completed feasible checks, scan
  current rows in both directions, ignoring production size/fanout caps. Record
  every finite interval derivation and its bound/row snapshot. These are
  **hypothetical deductions from a reached state**; they are not recursively
  installed and do not imply that production propagation examined the row.
- The existing exact arithmetic computes the candidate. An offline reader must
  recompute its value and reason selection from the snapshot, including negative
  coefficients and infinitesimal parts, before counting it as checked evidence.
- The event limit bounds trace size. An `end` with `limit_reached=true` means a
  prefix, not complete search coverage. A killed process can lack `end`; a path
  solved in preprocessing or using another engine can produce no block.

A row with $k$ terms costs $O(k)$ exact operations for its forward bound and
$O(k^2)$ for all reverse bounds, plus serialization. Whole-row scans repeat after
assignments. There is no deduplication in the solver; performance of a traced run
must not be presented as ordinary solver performance. Exact-arithmetic operation
counters also include the observer's extra calculations.

## Reading the dependency graph

The offline reader freezes a delivered theory reason against the earlier active
assignments. It expands only recorded LRA theory reasons. Leaves without such a
reason remain **opaque SAT literals**: they can be decisions, input consequences,
or Boolean propagations whose reasons this observer cannot see. They must not be
called original assertions. Backtracking removes active literal instances;
already recorded derivations retain their historical dependencies.

For each reason or conflict, compare its immediate distinct premises, expanded
leaf occurrences, expanded distinct leaves, and maximum recorded theory depth.
Repeated leaves can include duplicate immediate premises. The separate
`theory_expansions_with_shared_leaves` count detects overlap between the leaf
sets of **distinct** immediate premises, isolating sharing exposed by recorded
theory reasons. Expansion can enlarge a reason.
These counts do not establish that replacing the clause would improve search.

For each numerical candidate, distinguish an exactly matching registered atom,
a weaker atom the candidate entails, and the absence of either. Compare against
the actually installed bound separately. A candidate whose exact value lacks an
atom is not evidence that a hidden bound was installed in the tableau.

## Running the probe

`--lra-bound-probe FILE` enables the observer and
`--lra-bound-probe-limit N` caps events per checker (default 20000). The stream
must outlive any programmatic checks using its borrowed pointer. The observer
currently follows `OnlineLraPropagator`, including its mixed and rebuilt
assumptions-session uses; other engines are not instrumented. A missing block is
reported as missing coverage. Forward and reverse production propagation remain
controlled by their existing flags, independently of diagnostic row scans.

Start with the shared-parent example $x=2y$, $z=y$, $0\le y\le1$, then a
multi-premise sum, a two-stage chain, strict bounds, negative coefficients and
backtracking. Keep preprocessing disabled for constructed examples when its
substitutions would erase the structure being studied. Real-corpus runs should
also record ordinary preprocessing behavior rather than silently suppress it.


The [offline analyzer](https://github.com/primo-solver/primo/blob/47e5dee/scripts/analyze_lra_bound_probe.py)
checks each interval snapshot with Python `Fraction`, then reconstructs historical
theory ancestry. Run it as `python3 scripts/analyze_lra_bound_probe.py TRACE.jsonl
--out SUMMARY.json`. Counts of unnamed numerical bounds and stronger-than-installed
bounds use distinct `(target, direction, value, immediate reasons)` tuples within
each checker, so repeated snapshots do not inflate these counts. A separate set
counts candidates that are stronger than installed at **any** observed snapshot,
including after backtracking; a dominated first occurrence does not hide a later
opportunity. Delivered
propagations and conflicts retain their event counts.

Tests cover exact strict reverse reasoning with a negative coefficient, shared
leaves, removal and reassignment of literal instances, unchanged verdict/search
counters with tracing enabled, trace truncation, and output names
from both short and long symbol strings. Variable names are copied into owned
strings before synchronous serialization, so a temporary cannot leave a dangling
view in the observer. These tests do not establish
coverage of Boolean SAT reasons or independently prove UNSAT.

Snapshot checking validates the interval computation using the recorded row. It
does not independently certify that row against the original assertions. Tracing
can change timeouts and cancellation outcomes, even though it changes no logical
deduction. The driver checks declared known statuses and jointly decisive
reference answers, then independently replays successful SAT rows with Dolmen.


Exact atom matching compares the full $(q,d)$ pair in $q+d\delta$. Two strict
bounds at the same rational threshold can have different delta coefficients.
An unnamed exact numerical value therefore need not express a new SMT-LIB
constraint. The separate entailment count uses the existing tableau's
lexicographic comparison, including that coefficient.

The SLURM driver has separate `check`, `fuzz`, `asan`, and `probe` phases.
For a cluster with a separately provisioned sanitizer runtime, set
`PRIMO_ASAN_RUNTIME_DIR` to a directory containing `libasan.so`; the ASan phase
adds its link and runtime search paths. Keep compiler/runtime provenance with
the job artifacts. This changes verification setup, not the solver profile.
