# SAT backend: IPASIR-UP and CaDiCaL

## Role

Primo does not implement a SAT solver.

The SAT layer owns Primo's propositional variables, literals, clauses, assumptions, and backend adapter. Actual SAT solving is delegated through IPASIR-UP, initially using CaDiCaL as the implementation.

## Implemented scope

- create and destroy a backend instance
- add permanent clauses
- add assumptions for a solve call
- call solve
- read Boolean assignments after SAT
- identify failed assumptions where supported
- connect one CaDiCaL external propagator
- observe SAT variables for that propagator
- receive assignment, decision-level, backtrack, and complete-model callbacks
- return theory propagations with streamed reason clauses
- return external conflict or lemma clauses during search
- provide theory-guided decision literals where the connected propagator supports
  them
- return clear unsupported-feature errors when CaDiCaL/IPASIR-UP is unavailable

## Deferred scope

- proof-producing integration
- a backend-independent wrapper for every CaDiCaL-specific callback capability
- safe removal of observed variables from a live mixed-theory search
- proof infrastructure for external clauses and external propagation reasons

## Boundaries

- The backend sees only SAT literals and clauses.
- SMT terms and theory literals do not cross into CaDiCaL-specific code.
- CaDiCaL-specific APIs stay isolated in the backend module.
- Missing IPASIR-UP/CaDiCaL dependencies must be reported explicitly.

## User-Propagator Contract

Primo uses CaDiCaL's C++ external-propagator API, which implements the IPASIR-UP
role described by Fazekas, Niemetz, Preiner, Kirchweger, Szeider, and Biere. The
contract matters for Nelson-Oppen work because it controls when a mixed-theory
candidate equality may become a SAT literal.

Only one external propagator can be connected to a CaDiCaL solver at a time, so
mixed QF_UFLRA uses `MixedTheoryPropagationDispatcher` as the single callback
object and lets that dispatcher delegate ordinary arithmetic callbacks to
`OnlineLraPropagator`. Pure QF_LRA and pure QF_UF keep their direct callback
objects and do not pay for the mixed dispatcher.

Variables relevant to the external propagator must be marked observed after the
propagator is connected. CaDiCaL's current header states that external
propagation, clause addition during search, and assignment notifications are
over observed variables. Observed variables are frozen for inprocessing. The
same header allows a fresh variable to be observed during solving, but
`cb_propagate()` must return an observed literal and current external clauses
are expected to be over observed variables.

The practical Primo rules are therefore:

- A callback queue may store theory-owned candidates, but it may return only a
  SAT literal that already exists and has been observed.
- Eagerly observing the full same-sort shared-pair cross product is a quadratic
  cost and should stay out of default paths.
- A model-based Nelson-Oppen path should create and observe an interface
  equality atom only when a model/care/theory candidate asks for that pair, then
  propagate it or add a guarded external clause.
- If a future CaDiCaL version relaxes the observed-variable restriction for
  external clauses, Primo can add a backend capability flag; until then,
  mixed-theory code must assume observed-only external propagation and clauses.

## CaDiCaL option overrides

The CLI accepts repeated `--cadical-option name=value` arguments. Each pair is
parsed as an integer CaDiCaL option and applied through `CaDiCaL::Solver::set()`
while the backend is still in its configuring state. The options are stored in
`smt::CheckSatOptions` and consumed only by `sat::CadicalBackend`; no SMT or
theory layer depends on CaDiCaL-specific option names.

This hook is intended for benchmark campaigns and dependency checks, not as a
paper-backed solver algorithm. It exists because incremental SAT workloads can
be sensitive to CaDiCaL version defaults. In particular, repeated `solve()`
calls with small deltas should be checked with an incremental profile such as
`ilb=2` plus selected inprocessing options disabled before attributing a
slowdown to Primo's EUF code. Invalid option names or values are rejected by the
backend and reported as unsupported input for that run.

## Search statistics

`sat::CadicalBackend::statistics()` combines Primo-owned clause counters with
CaDiCaL-owned search counters read through the public
`CaDiCaL::Solver::get_statistic_value()` API. The SMT layer publishes the
additional counters as `:sat.conflicts`, `:sat.decisions`, and
`:sat.propagations`.

These counters are diagnostic only. Root-level simplification, unit propagation,
or an external EUF conflict may solve or stop a small instance while conflicts
and decisions remain zero, so benchmark analysis should compare them with the
EUF counters rather than treating nonzero SAT conflicts as a precondition for
real solver work.

## CaDiCaL version checks

Primo links one CaDiCaL library into each solver binary. The CLI therefore
offers runtime option overrides through `--cadical-option`, but it does not
switch CaDiCaL versions inside a single process. Version comparisons use one
Primo build directory per CaDiCaL tag.

`contrib/install_cadical.py` accepts `--tag` and `--prefix`, so a specific
CaDiCaL release can be installed into a separate prefix:

```bash
python3 contrib/install_cadical.py \
  --tag rel-1.9.5 \
  --prefix contrib/cadical-versions/rel-1.9.5 \
  --clean-build
cmake -S . -B build-cadical-1.9.5 \
  -DPRIMO_CADICAL_ROOT=$PWD/contrib/cadical-versions/rel-1.9.5 \
  -DCMAKE_PREFIX_PATH="$PWD/contrib/cadical-versions/rel-1.9.5;$PWD/contrib/local"
cmake --build build-cadical-1.9.5 --target primo
```

For repeated checks, use `scripts/check_cadical_versions.py`; it installs each
tag, builds a matching Primo binary, runs the CaDiCaL profile check, and writes
a merged CSV. This is the preferred way to compare tags such as `rel-1.9.5`,
`rel-2.2.1`, and `rel-3.0.0` without relying on whatever CaDiCaL happens to be
installed in `contrib/local`.

## References

See `docs/papers/ipasir-up.md`.

## Local installation

CaDiCaL can be installed into `contrib/local` with:

```bash
make contrib-cadical
```

Set `CADICAL_TAG=rel-1.9.5` to install a non-default tag into `contrib/local`.
For campaign comparisons, prefer versioned prefixes instead of replacing
`contrib/local`.

On SLURM, never trust a copied or stale `contrib/local/lib/libcadical.a` after a
toolchain change or after seeing linker errors such as `__isoc23_fscanf`.
Rebuild the dependency with the cluster compiler before measuring:

```bash
rm -f contrib/local/.cadical.stamp contrib/local/lib/libcadical.a
rm -rf contrib/src/cadical/build contrib/src/cadical/makefile \
  contrib/src/cadical/src/makefile build-slurm-rel
make contrib-cadical
cmake -B build-slurm-rel -DCMAKE_BUILD_TYPE=Release
cmake --build build-slurm-rel --target primo
build-slurm-rel/primo --version
```

`make build` passes `contrib/local` through `CMAKE_PREFIX_PATH` and reports whether CaDiCaL/IPASIR headers and libraries were found.
