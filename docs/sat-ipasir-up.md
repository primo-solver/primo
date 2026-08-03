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

## Protocol audit (2026-08-03)

Reviewed `OnlineLraPropagator` against CaDiCaL's IPASIR-UP contract, reading
`contrib/src/cadical/src/{internal,external_propagate,contract}.{cpp,hpp}`
rather than reasoning from the interface. **No live violation was found.** Two
couplings are load-bearing but non-local, and are now asserted in the code:

**`cb_check_found_model` returns `true` unconditionally, and that is only sound
in eager mode.** CaDiCaL's search loop runs `external_propagate()` before it
tests `satisfied()`, and `external_propagate()` calls `ask_external_clause()`
-- hence our theory check -- unconditionally whenever there is no conflict. So
a model is only offered for approval after a check produced no conflict clause.
But `external_propagate()` short-circuits on `!external_prop_is_lazy`: register
the propagator lazy and no check runs, so approving unconditionally would
report **sat on a theory-inconsistent assignment**. `is_lazy` is a public
member defaulting to false and we never set it; `cb_check_found_model` now
asserts it.

**A missing entry in `reasons_` would stream an empty reason clause.** CaDiCaL
documents that an external clause empty after simplification sets `unsat` --
so the symptom would be answering *unsatisfiable*. In practice its own
`REQUIRE` ("external reason clause must contain the propagated literal") aborts
first, and contracts are compiled into our CaDiCaL (`NCONTRACTS` is unset --
verified by finding the message in the built `libcadical.a`), so the failure is
loud. `cb_add_reason_clause_lit` now asserts too, so the abort names our broken
invariant rather than CaDiCaL's symptom.

Checked and found correct without change:

- `cb_decide` returns only unassigned, observed literals. CaDiCaL calls
  `notify_assignments()` immediately before `cb_decide`, so the propagator's
  `assigned_` mirror cannot be stale at that point, and every theory-atom
  variable is registered through `add_observed_var`. CaDiCaL `REQUIRE`s both.
- `cb_propagate` skips variables assigned since a candidate was queued, and
  records `reasons_[lit]` before returning `lit`.
- The reason stream emits the propagated literal first, satisfying CaDiCaL's
  "must contain the propagated literal" contract.
- `notify_backtrack` erases `reasons_` only for literals it actually
  unassigns. Clearing unconditionally was a real bug once, fixed in `d00134b`.

One latent item, left alone deliberately: `cb_has_external_clause` copies
`conflict_` without clearing it, so a second call before any
`notify_backtrack` would re-offer the same clause. It is unreachable in
practice -- a Farkas conflict clause is falsified by the current assignment, so
CaDiCaL must either backtrack (which resets `conflict_`) or derive unsat at
level 0 -- and `conflict()` is read after `solve()` returns, so clearing it
early would break that reader.

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
