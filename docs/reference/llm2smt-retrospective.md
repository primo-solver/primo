# LLM2SMT retrospective

## Status

LLM2SMT, branch `lra`, is the predecessor of Primo.

Repository:

```text
https://github.com/MikolasJanota/llm2smt/tree/lra
```

## How it may be used

Allowed:

- compare high-level architecture
- inspect old benchmark workflow
- identify missing regression tests
- understand previous failure modes
- compare command-line behavior
- document lessons learned

Not allowed:

- copying code
- copying tests verbatim
- copying grammar files
- copying documentation text
- porting modules mechanically

## Questions to answer later

- Which components were useful?
- Which parts became hard to maintain?
- Which invariants were missing?
- Which tests were absent?
- Which prompts led to unsystematic development?
- Which scripts should be redesigned?
- Which algorithms need stronger paper-level documentation?

## Current comparison role

Use LLM2SMT as a reference solver and as a retrospective source, not as an
implementation source. A useful comparison has three parts:

- run the same benchmark manifest with Primo and LLM2SMT at the same timeout;
- record solved count, wrong answers, PAR2, and family-level outcomes;
- inspect the input shape and Primo's stats before deciding which Primo
  mechanism to strengthen.

The QF_UF NEQ/PEQ/SEQ gap illustrates the intended use. On the seven held-out
gap instances in `benchmarks/qfuf-smac-gap-validate.txt`, LLM2SMT 0.2.4 solves
all seven within 120 seconds. Primo after commit `d94ca2d` solves
`NEQ015_size6` in 17.59 seconds under the tuned online profile, but still times
out on `NEQ016_size8`, the four PEQ instances, and `SEQ026_size7`. The lesson is
that changed-term online propagation was a real bottleneck on one NEQ case, but
the remaining reference gap is not just candidate-scan overhead. Future work on
those families should start from the formula structure and preprocessing
statistics, not by adding more propagation scheduling knobs.

The July 2026 hard-set campaign refined that lesson. On the same seven held-out
NEQ/PEQ/SEQ files, commit `bb69076` solved 1/7 at 120 seconds with PAR2
1543.363725 and zero wrong answers. Commit `ecfe1d8` solved 5/7 with PAR2
794.441822 and zero wrong answers. The winning change did not add a new SAT
profile or a new EUF heuristic: it removed eager scaffold work by using the
incremental use-list congruence closure in preprocessing and by avoiding
congruence explanation construction when preprocessing facts have unconditional
reasons. The result artifacts are:

- `results/slurm-qfuf-hard-direct-conflict-bb69076/`;
- `results/slurm-qfuf-hard-preinc-ecfe1d8/`.

This should be treated as a general development rule. When performance is far
behind a reference solver, first look for eager work in default paths:
preprocessing closures, all-pairs scans, repeated explanation construction,
callback full scans, and work performed after a conflict is already known.
Only after those costs are ruled out should the next step be parameter tuning or
additional propagation heuristics.

## Optimization styles that transferred

### Structural preprocessing before search

The strongest recurring pattern is to reduce the Boolean/EUF search problem
before SAT starts. Useful Primo targets in this style are:

- unit Boolean and unit equality simplification;
- equality bridging under disjunctions;
- finite-domain at-most-one clauses for operation-table encodings;
- symmetry breaking for finite-domain choice sets;
- solved-constant substitution guarded by size and cycle checks;
- forced-atom simplification from top-level predicate, equality, and
  disequality facts;
- bounded ground-congruence clauses over terms already present in the input.

This style is preferred for PEQ/SEQ/NEQ because it produces ordinary assertions
and clauses whose effect is visible in stats. It also keeps theory reasoning
simple: every generated fact must be justified as a satisfiability-preserving
or equisatisfiable preprocessing transformation and tested independently.

### Affected-set propagation

The useful online-propagation lesson is not "scan harder"; it is "only inspect
candidates whose truth value could have changed." Primo now implements a small
version of this idea for EUF: flat congruence-closure merges record terms whose
representative changed, and the SAT callback maps those terms back to theory
atoms before falling back to the full scan.

Future affected-set targets should keep the same shape:

- maintain an explicit dirty set or work queue;
- deduplicate candidates before calling expensive explanation code;
- keep reason construction as the soundness gate;
- expose counters that show how many candidates were skipped, tested, and
  delivered;
- preserve full-scan or final-check fallback until the optimized path is proved
  complete.

### Stats-first profiling

Optimization changes should add or reuse counters before they are interpreted as
success. Counters that have already been useful include:

- SAT variables, clauses, binary clauses, unit clauses, and clause literals;
- preprocessing initial/final reachable term sizes and changed pass counts;
- finite-domain domains, AMO clauses, symmetry constraints, and rejected sets;
- unit equality substitutions, accepted/rejected guarded substitutions, and
  forced-atom rewrites;
- EUF propagation rounds, candidates, delivered literals, budget cutoffs, and
  hit-rate cutoffs;
- incremental congruence-closure merges, lookup probes/hits, explanation calls,
  explanation-cache hits, and conflict counts.

When a target times out and emits no final stats, the harness should be extended
or the instance should be run under a shorter instrumented local profile to
recover partial information. A timeout without stats is a weak diagnostic.

Use fixed-work diagnostic cutoffs when wall-clock time hides the issue. The
`--max-theory-conflicts` option made the `SEQ026_size7` investigation
repeatable: the same 1000-conflict run exposed that preprocessing scaffold work
and flat closure use-list saturation were consuming time before solved-count
changes were visible. Fixed-conflict profiles are especially useful when
CaDiCaL search noise or full 120-second timeouts make two builds hard to
compare.

Do not stop after observing a local profile win. The direct disequality-conflict
early exit reduced lookup probes and pending pops on a fixed-conflict local
probe, but the first SLURM hard-set run still solved only 1/7. The subsequent
preprocessing-closure change was validated because it improved both the local
fixed-work probe and the SLURM solved count/PAR2.

### Profiles as experiments, not defaults

Parameter tuning and CaDiCaL profiles are useful for finding mechanisms, but
not sufficient for default behavior. A tuned profile that solves one validation
case can still regress Goel hardware or leave PEQ/SEQ untouched. Promote a
profile to default only after:

- zero wrong answers on the relevant exact set;
- a family-level solved-count and PAR2 improvement;
- no unacceptable regression on Goel, NEQ, PEQ, QG, and SEQ;
- documentation of which mechanism explains the improvement.

Until then, keep the profile as an experiment and implement the underlying
mechanism directly.

Cluster setup is part of the experiment. If a remote build links against a
workstation-built CaDiCaL archive, benchmark results may be blocked by libc
symbol mismatches such as `__isoc23_fscanf`. Rebuild CaDiCaL on the cluster
before trusting a fresh SLURM campaign, and record which binary produced each
artifact.

### Reference-driven target selection

Use LLM2SMT, Z3, and cvc5 artifacts to choose targets, but choose mechanisms
from Primo's architecture. The preferred loop is:

1. Rank fast-reference/slow-Primo files from current CSVs.
2. Pick a cluster, not a remembered anecdote.
3. Inspect input shape: top-level assertions, disjunctions, units,
   finite-domain-looking choices, equality/disequality density, `ite`, and
   repeated subterms.
4. Compare Primo stats to the reference outcome.
5. Implement the smallest mechanism that explains a cluster.
6. Re-run the same manifest on SLURM and compare PAR2 and wrong answers.

## Styles to avoid

- Do not copy code, tests, grammar fragments, comments, or documentation from
  LLM2SMT.
- Do not add another broad option profile when a missing mechanism is visible.
- Do not treat one solved target as reference parity.
- Do not accept unsound or unreasoned propagation for speed. Reasons and
  independent tests are part of the optimization.
- Do not tune CaDiCaL before checking whether preprocessing inflated the SAT
  problem or failed to expose unit facts.
- Do not keep adding heuristics without a counter proving that the heuristic is
  exercised.

## Future optimization targets

These are legitimate future directions, provided each is implemented from a
paper or a Primo design note with tests and counters:

- stronger finite-domain preprocessing for PEQ/SEQ operation-table formulas;
- more complete finite-domain symmetry breaking, including rejection counters
  that explain missed symmetries;
- better Boolean/unit propagation through top-level disjunction and bridge
  patterns without rewriting inside uninterpreted applications;
- bounded congruence preprocessing over ground applications already present in
  the input;
- cheaper theory-propagation reason construction and explanation caching;
- family-specific ablation manifests that isolate Goel, NEQ, PEQ, QG, and SEQ
  instead of mixing all QF_UF behavior into one aggregate number.
