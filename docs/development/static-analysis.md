# Static analysis and the LLVM toolchain

Static analysis can run locally. Solver builds for qualification, solver test
batches, fuzzing, benchmarks and corpus validation still belong on CPU SLURM.
The commands below configure CMake and parse source; they do not build or run
Primo. Configuration can compile small compiler/dependency capability probes.

## Run a complete source scan

Install matching Clang, clang-tidy and clang-format tools, CMake, ripgrep, and
the normal development dependencies under `contrib/local`. The defaults use
LLVM 18. The distribution's `run-clang-tidy-18` also needs PyYAML in the Python
interpreter that runs it; the default is `/usr/bin/python3`. The three Primo
Python helpers themselves use only the standard library.

```bash
make lint
make tidy STATIC_JOBS=2
make include-cleaner STATIC_JOBS=2
make check-headers STATIC_JOBS=2
```

Each static target creates a timestamped directory under `results/`. Set
`STATIC_REPORT_DIR=results/my-static-attempt` to choose a durable name, using
a fresh name for each attempt. Override `STATIC_CXX`, `CLANG_TIDY`,
`RUN_CLANG_TIDY`, and `STATIC_BUILD_DIR` when the installed paths differ.
`STATIC_JOBS` bounds concurrent analyzer/compiler processes; it does not impose
a memory limit. For example, in Bash, a scan can inherit an 8 GiB per-process
address-space limit and a 30-minute overall deadline:

```bash
(ulimit -v 8388608; timeout -k 10s 30m make tidy STATIC_JOBS=2)
```

The {staticsource}`Makefile` static targets depend on `analysis-db`, which configures
a separate Debug compilation database and generates the Git revision metadata
header needed by `main.cpp`. There is no dependency on `build` or dependency
installation. A missing compiler, analyzer or development dependency fails the
command rather than skipping the check.

{staticsource}`prepare_static_analysis.py <scripts/prepare_static_analysis.py>`
requires actual CMake commands for every tracked production and unit-test
`.cpp` file. It supplies explicit derived commands for documentation examples
using the SMT library's flags, and for the standalone rational benchmark using
`rational.cpp`'s flags. These derived contexts are recorded individually; they
are parsing contexts, not evidence that those programs were built or linked.
All command variants are preserved. The manifest records hashes of the input
database, owned C++ sources/headers and generated revision header. The original
database digest is provenance; the summarizer does not rehash that database.

## Read the result

The {staticsource}`clang-tidy profile <.clang-tidy>` enables compiler diagnostics,
Clang Static Analyzer, bugprone, performance, portability and include-cleaner
checks. The header filter requests owned source, tests and examples; analyzer
path reports can still point into dependencies, which retain their own paths
in the inventory.
`make include-cleaner` provides a separate advisory include scan. Neither target
applies suggested edits.

The LLVM runner writes `tidy.log` and `tidy.yaml`; the status and diagnostic
inventory is `tidy-summary.json`. The include-only equivalents are
`includes.log`, `includes.yaml` and `includes-summary.json`. A nonzero aggregate
exit must be treated as incomplete even if a selected file later passes a
retry. {staticsource}`summarize_static_analysis.py <scripts/summarize_static_analysis.py>`
requires a successful aggregate, an observed command for every expected source,
unchanged recorded C++ sources, owned headers and generated revision header,
and no compiler errors. It deduplicates repeated
header diagnostics by location, message and check. Its statuses are:

| Status | Meaning | Target result |
| --- | --- | --- |
| `clean` | Complete scan with no reported diagnostics | Success |
| `complete-with-findings` | Complete scan with diagnostics requiring review | Success, findings explicitly reported |
| `incomplete` | Failed tool, missing source, changed recorded source/header/revision or compiler error | Failure |

The current broad profile is advisory while establishing a baseline. A
successful target is therefore not a warning-free gate. Formatting and Ruff
remain blocking in `make lint`. Review diagnostics in their owning function:
check guards and caller invariants, allocation and invalidation boundaries,
exception guarantees, and test setup. In particular, a proposed reference in
place of a copy needs a lifetime argument across intervening mutations. Keep
paired header self-includes unless their removal is justified independently.

{staticsource}`check_static_headers.py <scripts/check_static_headers.py>` generates a
one-include translation unit for each tracked header under `src/`, using every
SMT-library compile context with `-fsyntax-only`. It records per-header commands,
stdout/stderr and exits, with a 60-second timeout per check. This detects
accidental dependence on include order; it does not prove that all template
instantiations or feature configurations compile.

## Coverage and remaining checks

| Tool or check | Current use | Remaining boundary |
| --- | --- | --- |
| clang-format | Mandatory owned C++ formatting in `make lint` | Formatting alone checks no behavior |
| Clang compiler diagnostics | Local parsing through clang-tidy and header checks | Full Clang code-generation/link/test qualification remains separate |
| clang-tidy / Static Analyzer | Broad local whole-source profile | Findings require triage; cross-translation-unit analysis is not configured |
| misc-include-cleaner | Included in broad scan, separate advisory target | Suggested removals require source review |
| clangd | Can use the configured build compilation database in editors | No clangd executable was available in the recorded local audit; no editor diagnostics claimed |
| ASan / LSan | Existing CPU SLURM merge qualification | Separate runtime evidence, not part of local static analysis |
| UBSan / MSan / TSan | No result from this local audit | Separate instrumented builds and runtime/dependency coverage are needed |
| Source-based coverage | No result from this local audit | Requires instrumented runtime workloads and an explicit coverage scope |

The local audit on 27 October 2026 analyzed all 132 tracked `.cpp` files and
parsed all 100 owned headers individually with LLVM 18.1.3. The broad scan's
745 distinct diagnostics are an inventory for triage, not 745 confirmed bugs.
No solver C++ was changed by this tooling work. See the
{staticsource}`dated audit record <docs/coverage/local-static-20261027.json>` and
{staticsource}`run ledger <docs/run-log.json>` for source identity, resources,
commands, setup failures and limitations. This does not establish full LLVM
runtime or cross-translation-unit qualification.

The workflow follows the official
[clang-tidy documentation](https://clang.llvm.org/extra/clang-tidy/).
[Cross-translation-unit analysis](https://clang.llvm.org/docs/analyzer/user-docs/CrossTranslationUnit.html),
[clangd compile commands](https://clangd.llvm.org/design/compile-commands),
[UBSan](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
and [source-based coverage](https://clang.llvm.org/docs/SourceBasedCodeCoverage.html)
are separate LLVM facilities; listing them does not claim they have been run.
