# Command-line options and output

Run `./build/primo --help` for the complete option inventory of your binary.
The {download}`captured help <../examples/primo-help.txt>` belongs to the
source revision shown in [](../current-status.md). This page explains the
common controls; specialized switches remain documented in the linked
component chapters while their reference material is being rewritten.

## Everyday use

| Option | Default | Use and boundary |
| --- | --- | --- |
| `--help`, `-h` | Off | Print grouped CLI help and exit. |
| `--version` | Off | Print revision/build information and exit. |
| `--quiet`, `-q` | Off | Suppress the startup banner; SMT-LIB responses remain enabled. |
| `--stats` | Off | Write query statistics to stderr. See [](../debugging.md). |
| `--check-model` | Off | Check original assertions against a requested model. Set `:produce-models true` separately; see [](../guide/models.md). |
| `--incremental-mode` | `rebuild` | Choose `rebuild` or the restricted `assumptions` session engine. See [](../guide/sessions.md). |
| `--preprocessing-mode` | `auto` | Choose `auto`, `off`, `basic`, or `strong`. `off` does not bypass required lowering. Assumptions mode admits only `auto` or `off`. |
| `--preprocess-only` | Off | Run parsing/preprocessing and report `unknown` without solving. Not accepted in assumptions mode. |
| `--max-theory-conflicts=N` | Unlimited | Positive budget, up to 1,000,000,000; return `unknown` after reaching the learned-theory-conflict limit. |
| `--lia-branch-limit=N` | `100000` | Bound integer branch-and-bound nodes; reaching the bound can return `unknown`. See [](../qf-lia-branch-and-bound.md). |
| `--cancel-grace-seconds=N` | `1` | Cooperative cancellation grace period after SIGINT/SIGTERM, from 1 to 3600 seconds. This is not a solve-time limit. |

For example:

```bash
./build/primo --quiet --stats docs/examples/first-solve.smt2
./build/primo --quiet --check-model docs/examples/model.smt2
./build/primo --quiet --incremental-mode=assumptions docs/examples/session.smt2
```

Options accept only their declared values/ranges. A name beginning with
`experimental` or a diagnostic switch does not identify a qualified default.
For component-specific controls, use [](../preprocessing.md),
[](../euf.md), [](../arithmetic.md), [](../quantifiers.md), and
[](../tuning-parameters.md). The help snapshot includes every registered CLI
option; the prose above is intentionally a practical subset.

## Output and exit status

SMT-LIB answers, models and errors go to stdout. Diagnostics, startup information
and optional statistics go to stderr. Use `--quiet` when scripting.

The command loop returns zero when it finishes without a command/unsupported
failure; this includes normal `sat`, `unsat` and `unknown` responses. It returns
2 for a command error or an unsupported command/solving result. File-opening and
explicit option-validation failures return 1; CLI parsing can return its own
nonzero status. Cancellation and external termination need separate treatment.
An exit code alone never distinguishes SAT from UNSAT.

Each input file creates a fresh session. A command error stops the current
stream; an unsupported response marks a nonzero status but can allow later
commands to be processed. For reliable automation, capture the full response
sequence and stderr, and identify which `check-sat` each answer belongs to.

These conventions come from {source}`src/main.cpp` and
{source}`src/cli/response_printer.cpp`.

## Where defaults live

CLI registration and validation currently live in {source}`src/main.cpp`, enum
spellings in {source}`src/cli/option_names.hpp`, and solver defaults in
{source}`src/smt/check_sat.hpp`. Strategy selection can determine whether a
particular option takes effect. The placeholder `src/cli/options.cpp` and
`src/cli/options.hpp` are not the authoritative option implementation.

The documentation example checker compares the captured help with the built
binary. Update the help snapshot only after reviewing changed defaults and the
affected manual pages; it is evidence for this revision, not a substitute for
explaining new behavior.
