# Command-line options and output

Run `./build/primo --help` for the complete option inventory of your binary.
The {download}`captured help <../examples/primo-help.txt>` belongs to the
documented implementation; source metadata is centralized in the review ledger. This page explains the
common controls; the complete captured inventory below supplies accepted values and defaults.
[Effective policies](policies.md) explains automatic selection and option dependencies.

## Everyday use

| Option | Default | Use and boundary |
| --- | --- | --- |
| `--help`, `-h` | Off | Print grouped CLI help and exit. |
| `--version` | Off | Print revision/build information and exit. |
| `--quiet`, `-q` | Off | Suppress the startup banner; SMT-LIB responses remain enabled. |
| `--stats` | Off | Write query statistics to stderr. See [](../reference/statistics.md). |
| `--check-model` | Off | Check original assertions against a requested model. Set `:produce-models true` separately; see [](../guide/models.md). |
| `--incremental-mode` | `rebuild` | Choose `rebuild` or the restricted `assumptions` session engine. See [](../guide/sessions.md). |
| `--preprocessing-mode` | `auto` | Choose `auto`, `off`, `basic`, or `strong`. `off` does not bypass required lowering. Assumptions mode admits only `auto` or `off`. |
| `--max-theory-conflicts=N` | Unlimited | Positive budget, up to 1,000,000,000. Counts aggregate SMT-reported conflicts across supported routes; see [conflict-limit accounting](statistics.md#uniform-conflict-budget-implementation-contract). Exhaustion returns `unknown` with `ResourceLimit`. |
| `--lia-branch-limit=N` | `100000` | Bound integer branch-and-bound nodes; reaching the bound can return `unknown`. See [](../implementation/lia.md). |
| `--cancel-grace-seconds=N` | `1` | Cooperative cancellation grace period after SIGINT/SIGTERM, from 1 to 3600 seconds. This is not a solve-time limit. |

For example:

```bash
./build/primo --quiet --stats docs/examples/first-solve.smt2
./build/primo --quiet --check-model docs/examples/model.smt2
./build/primo --quiet --incremental-mode=assumptions docs/examples/session.smt2
```

Options accept only their declared values/ranges. A name beginning with
`experimental` or a diagnostic switch does not identify a qualified default.
For component-specific controls, use [](../implementation/preprocessing.md),
[](../implementation/euf.md), [](../implementation/lra.md), [](../implementation/quantifiers.md), and
[](../reference/policies.md). The help snapshot includes every registered CLI
option; the prose above is intentionally a practical subset.

The two automatic-budget options, `--euf-prop-budget` and `--euf-prop-delivery`,
accept exactly `auto`, `unlimited`, or unsigned decimal digits fitting in
`std::size_t`. Zero and leading zeros are accepted; signs, whitespace, trailing
characters and overflow are CLI errors detected before solving by
{repairsource}`parse_auto_budget <src/main.cpp>`. The options'
different zero meanings are described in [effective policies](policies.md).

## Preprocessing diagnostic

`--preprocess-only` stops before search. Use it with `--stats` to inspect
preprocessing cost and formula-size counters independently of solving. It
returns `unsat` if ground preprocessing proves a contradiction and `unknown`
otherwise, including when simplification leaves no assertions. It does not
print the transformed formula. With quantified `UF` input it stops after the
frontend: ground preprocessing does not support binders, and neither
instantiation nor SAT/theory search runs. Unsupported inputs remain unsupported.
The option defaults off and is not accepted in assumptions mode.

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
particular option takes effect. The placeholder {source}`src/cli/options.cpp` and
{source}`src/cli/options.hpp` are not the authoritative option implementation.

The documentation example checker compares the captured help with the built
binary. Update the help snapshot only after reviewing changed defaults and the
affected manual pages; it is evidence for this revision, not a substitute for
explaining new behavior.

## Complete CLI inventory

The snapshot is checked by the existing documentation example checker. The
word “scaffold” in the banner is the current CLI description, not a capability
classification. Experimental split counts are bounded by the available atoms,
as explained in [tactics](tactics.md).

```{literalinclude} ../examples/primo-help.txt
:language: text
```

## LRA bound diagnostics

`--lra-bound-probe FILE` writes online LRA bound and reason events as JSONL.
`--lra-bound-probe-limit N` caps events per checker (default 20000). This expensive
observer is off by default and changes no deductions. See the
[observation contract](../papers/lra-bound-structure-probe.md) for coverage and
trace interpretation.

`--lra-bound-probe-snapshot-limit M` separates numerical snapshots from the
causal-event budget. With it, `--lra-bound-probe-limit N` limits causal events;
the initial catalog is recorded independently. Zero snapshots selects a causal
trace without numerical row scanning. Leaving the new option unset preserves
the original shared-budget behavior.
