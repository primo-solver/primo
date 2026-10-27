# Build and run Primo

Start here to build Primo and solve a small SMT-LIB problem. Commands below run
from the repository root. The [capability map](../capabilities.md) describes
what this version accepts and where solving can return `unknown`.

## Prerequisites

Use a Unix-like environment with a C++20 compiler, CMake 3.20 or newer, GNU
Make, Git, Python 3, and a shell. Dependency bootstrap needs network access.
The documentation build also needs Python's `venv` and `pip` modules. Linux is
the environment used for this manual's verification; this is not a claim of
validation on every platform.

## Build

```bash
git clone https://github.com/primo-solver/primo.git
cd primo
make build
./build/primo --version
```

`make build` installs CLI11, CaDiCaL and GMP under `contrib/local`, configures
CMake in `build/`, and builds `build/primo`. It selects a Release build unless
you have configured another build type. No parser generator is required.
Reference solvers and fuzzers are optional tools, not prerequisites for this
first solve. See {source}`Makefile`, {source}`CMakeLists.txt`, and the
{source}`GMP installer <contrib/install_gmp.py>` for the actual build paths.

Record `--version` when sharing results: it identifies the revision and build
configuration. Re-run `make build` after changing the source.

If GMP bootstrap reports `Network is unreachable`, the failure is downloading
the archive, before compilation. The installer tries GNU's download server,
then the kernel.org GNU mirror if the first download fails or has a bad
checksum. Downloads of the pinned GMP 6.3.0 release are checked against its
SHA-256 digest before extraction; other versions do not yet have pinned
digests. Each connection/read has a 30-second socket timeout, which is not a
total download deadline. If both sources fail, the error lists both failures.
To select an accessible release archive explicitly, run
`python3 contrib/install_gmp.py --url https://your-mirror.example/gmp-6.3.0.tar.xz`.
An explicit URL disables fallback and still checks the pinned release digest.
An existing extracted source directory is reused without another download;
download verification does not revalidate that directory.

CMake also supports a backend-disabled scaffold (set
`IPASIR_UP_INCLUDE_DIR=OFF` and `CADICAL_LIBRARY=OFF`). It compiles the
non-backend infrastructure but supplies no SAT solver. Both
{migrationsource}`integer solving <src/smt/solver_engines.cpp#L10069-L10076>` and
{migrationsource}`retained solving <src/smt/solver_engines.cpp#L12330-L12351>`
require an IPASIR backend; this build is not a solving fallback.

## First solve

This problem is contradictory: equal arguments must give equal results for
`f`, while the last assertion requires different results.

```{literalinclude} ../examples/first-solve.smt2
:language: lisp
```

Run the included file:

```bash
./build/primo --quiet docs/examples/first-solve.smt2
```

Expected standard output:

```{literalinclude} ../examples/first-solve.txt
:language: text
```

`--quiet` suppresses the startup banner. Answers go to standard output;
diagnostics and requested statistics go to standard error. A file can contain
several `check-sat` commands. With no filename, Primo reads commands from stdin
and flushes each response as it becomes available:

```bash
./build/primo --quiet < docs/examples/first-solve.smt2
```

The [session guide](sessions.md) shows how to add and retract assertions during
one process. The [model guide](models.md) shows how to inspect a satisfying
assignment. Use the [command-line reference](../reference/options.md) for
options, output and exit-code conventions.

## Check the build and examples

```bash
make test
python3 scripts/check_doc_examples.py --solver ./build/primo
```

The example check verifies the small programs used in this manual. It is not
a replacement for solver fuzzing or independent model validation. Contributor
requirements are in [](../development/correctness.md).

## Read the book locally

```bash
make docs-serve
```

Open `http://localhost:8000`. This first runs the strict documentation build,
then serves `docs/_build/site`. Stop the server with Ctrl-C. For just the HTML,
run `make docs` and open `docs/_build/site/index.html`.

The generated book does not require a running solver or database service.
The same assembled site is the publication artifact; local preview commands do not deploy it.
