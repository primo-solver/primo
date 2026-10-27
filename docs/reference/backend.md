# SAT backend operations

Primo links one CaDiCaL library into each binary. The
{source}`installer <contrib/install_cadical.py>` defaults to tag `rel-3.0.0`;
record the actual dependency signature printed by `primo --version` with every
comparison. Backend details stay behind
{source}`CadicalBackend <src/sat/cadical_backend.hpp>`.

## Option overrides

Repeated `--cadical-option name=value` arguments apply integer settings during
backend configuration. Malformed or non-integer values and unknown names are
rejected. The {source}`adapter <src/sat/cadical_backend.cpp#L119-L122>` passes
integer values to CaDiCaL, whose API clamps values outside an option's range
to the nearest permitted endpoint. An accepted override therefore need not
retain the requested integer unchanged. These options are
not SMT-LIB `set-option` settings, and retained assumptions mode rejects custom
overrides. Check option reachability with the attached external propagator;
some backend processing is disabled or constrained by that interface.

## Factoring and variable ownership

The dependency's factoring pass performs bounded variable addition in CNF.
For example,

```text
(a or x) (a or y) (b or x) (b or y)
```

can be represented with a fresh Boolean `e` by

```text
(a or e) (b or e) ((not e) or x) ((not e) or y)
```

Existentially choosing `e` preserves satisfiability: when either `a` or `b` is
false, `e` must be true and hence both `x` and `y` must hold; when both are true,
`e = false` satisfies the added clauses. This describes the transformation,
not a Primo implementation of the backend's candidate-search algorithm.

The extension variable has no Primo `TermId`. The adapter allocates later
Primo variables through the backend's allocation API, preventing collisions
with backend-created variables. See [SAT integration](../implementation/sat.md).
`--cadical-option factor=0` is an experimental override, not a new default.
The {source}`original factoring profile <docs/sat-ipasir-up.md#L113-L172>`
retains its single-input result and explicitly insufficient promotion evidence.

## Version comparisons

Use a separate install prefix and build directory for each tag. For example,
on the build/test host:

```bash
python3 contrib/install_cadical.py --tag rel-3.0.0 \
  --prefix contrib/cadical-versions/rel-3.0.0 --clean-build
cmake -S . -B build-cadical-3.0.0 \
  -DPRIMO_CADICAL_ROOT="$PWD/contrib/cadical-versions/rel-3.0.0" \
  -DCMAKE_PREFIX_PATH="$PWD/contrib/cadical-versions/rel-3.0.0;$PWD/contrib/local"
cmake --build build-cadical-3.0.0 --target primo
```

{source}`check_cadical_versions.py <scripts/check_cadical_versions.py>` automates
installation, builds, profile checks and merged CSV output. It runs solver
batches and belongs in the experiment environment. Compare the same Primo
revision, inputs, limits and options, then perform separate correctness/model
checks. A different dependency is part of the configuration being evaluated.

## Search counters

The adapter combines Primo-submitted clause counts with CaDiCaL's public search
statistics. `:sat.conflicts`, `:sat.decisions`, and `:sat.propagations` do not
count the same operations as EUF or arithmetic counters. A formula can be
settled by root simplification or an external conflict with zero SAT decisions.
Retained-session active clause counts exclude some satisfied clauses and are
not allocated-byte measurements. See [statistics](statistics.md).
