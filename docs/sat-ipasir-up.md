# SAT backend: IPASIR-UP and CaDiCaL

## Role

Primo does not implement a SAT solver.

The SAT layer owns Primo's propositional variables, literals, clauses, assumptions, and backend adapter. Actual SAT solving is delegated through IPASIR-UP, initially using CaDiCaL as the implementation.

## Initial scope

- create and destroy a backend instance
- add permanent clauses
- add assumptions for a solve call
- call solve
- read Boolean assignments after SAT
- identify failed assumptions where supported
- return clear unsupported-feature errors for callbacks not wired yet

## Deferred scope

- online theory propagation through user-propagator callbacks
- theory-guided decisions
- adding clauses during search
- explaining user propagations
- proof-producing integration

## Boundaries

- The backend sees only SAT literals and clauses.
- SMT terms and theory literals do not cross into CaDiCaL-specific code.
- CaDiCaL-specific APIs stay isolated in the backend module.
- Missing IPASIR-UP/CaDiCaL dependencies must be reported explicitly.

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
