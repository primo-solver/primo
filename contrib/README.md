# Local dependencies

The project keeps local third-party tools under `contrib/local`.

These files are intentionally not committed:

- `contrib/local/`
- `contrib/src/`
- `contrib/downloads/`

## Fresh checkout

`make build`, `make test`, and `make check` bootstrap the required local
dependencies automatically:

```bash
make contrib-required
```

This installs:

- CLI11 single header
- CaDiCaL/IPASIR headers and static library

## Install all optional tools

```bash
make contrib
```

## Install individual tools

```bash
make contrib-z3
make contrib-cvc5
make contrib-yices
make contrib-opensmt
make contrib-cadical
make contrib-cli11
make contrib-fuzzsmt
make contrib-ddsmt
```

## Tool roles

- CLI11 is downloaded as the single header used by the solver executable.
- Z3 is installed through `pip install z3-solver` into the active Python environment and a `contrib/local/bin/z3` wrapper is created when possible.
- cvc5 is downloaded from GitHub releases into `contrib/local/bin/cvc5`.
- Yices 2 and OpenSMT2 are downloaded from GitHub releases into
  `contrib/local/bin/yices-smt2` and `contrib/local/bin/opensmt`. Both are
  **reference solvers only** -- run as differential oracles, never linked
  against and never read. That boundary is stated in each installer's header
  because both implement the same QF_LRA algorithm Primo does.
- CaDiCaL is cloned and built from its GitHub release tag, then installed into `contrib/local`.
- FuzzSMT is cloned from the public release repository and exposed as
  `contrib/local/bin/fuzzsmt`.
- ddSMT is installed through `pip install ddsmt` and exposed as
  `contrib/local/bin/ddsmt`.

`make compare` and `make fuzz` prepend `contrib/local/bin` to `PATH`.
`make build` passes `contrib/local` through `CMAKE_PREFIX_PATH` so CMake can find local CLI11, GMP, and CaDiCaL/IPASIR headers and libraries. The active SMT-LIB parser is handwritten; ANTLR and Java are not build dependencies. Existing cached ANTLR installations can be left in place but are not used.
