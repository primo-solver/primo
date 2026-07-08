# Optional local dependencies

The project keeps optional third-party tools under `contrib/local`.

These files are intentionally not committed:

- `contrib/local/`
- `contrib/src/`
- `contrib/downloads/`

## Install all optional tools

```bash
make contrib
```

## Install individual tools

```bash
make contrib-z3
make contrib-cvc5
make contrib-cadical
```

## Tool roles

- Z3 is installed through `pip install z3-solver` into the active Python environment and a `contrib/local/bin/z3` wrapper is created when possible.
- cvc5 is downloaded from GitHub releases into `contrib/local/bin/cvc5`.
- CaDiCaL is cloned and built from its GitHub release tag, then installed into `contrib/local`.

`make compare` and `make fuzz` prepend `contrib/local/bin` to `PATH`.
`make build` passes `contrib/local` through `CMAKE_PREFIX_PATH` so CMake can find local CaDiCaL/IPASIR headers and libraries.
