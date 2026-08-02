#!/usr/bin/env python3
"""Install the YinYang fuzzer fork into a virtualenv, with Primo's patches.

YinYang was previously set up by hand, which meant the venv on the cluster
and the one on a workstation could differ in ways nobody could see -- and one
of them (`-km` versus `-k` for --keep-mutants) is a spelling that changed
between upstream and the fork. This script makes the setup reproducible.

It installs from the project's fork, https://github.com/MikolasJanota/yinyang,
not from PyPI or from upstream: the fork already carries local changes (e.g.
"Handle QF_UF custom sorts in typefuzz"), and Primo's own patches under
contrib/patches are applied on top.

Patches applied:

  yinyang-mutant-validity.patch
      Two flags, both about mutants being valid SMT-LIB in the seed's logic.

      -kl/--keep-logic retains the seed's (set-logic ...). YinYang strips it
      by default so solvers do not emit warnings, but dropping it has two
      costs. Solvers that need the logic before a sort exists reject every
      mutant outright -- yices-smt2 fails with `undefined sort: Real` at line
      1, so as a reference solver it contributed nothing at all. And solvers
      that infer a default will cheerfully answer a mutant that has left the
      seed's logic instead of rejecting it, which makes them a poor oracle
      for a solver implementing only that logic.

      -hd/--hoist-declarations emits declarations before the commands that
      use them. typefuzz builds new terms from sort-compatible variables
      without checking the variable is declared before the assert it lands
      in, so a mutant can use a symbol well above its own declare-fun.
      Moving a declaration earlier is always safe -- top-level declarations
      are global and cannot shadow -- so this normalises rather than changes
      meaning. opfuzz does not need it: it substitutes operators in place
      and leaves command order alone.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


REPO = "https://github.com/MikolasJanota/yinyang.git"
PATCHES = ["yinyang-mutant-validity.patch"]


def run(command: list[str], cwd: Path | None = None) -> None:
    print("+", " ".join(str(c) for c in command))
    subprocess.run(command, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--venv", type=Path, default=Path(".venv-yinyang"))
    parser.add_argument("--src", type=Path, default=Path("contrib/src/yinyang"))
    parser.add_argument("--patches", type=Path, default=Path("contrib/patches"))
    parser.add_argument("--repo", default=REPO)
    parser.add_argument("--ref", default="", help="commit/branch to check out; default is the clone's HEAD")
    args = parser.parse_args()

    if args.src.exists():
        # Reset rather than pull: the tree carries applied patches, and a pull
        # onto a patched tree is how you get a conflict nobody asked for.
        run(["git", "-C", str(args.src), "fetch", "--quiet", "origin"])
        run(["git", "-C", str(args.src), "checkout", "--quiet", "--", "."])
        if args.ref:
            run(["git", "-C", str(args.src), "checkout", "--quiet", args.ref])
        else:
            run(["git", "-C", str(args.src), "reset", "--hard", "--quiet", "origin/HEAD"])
    else:
        args.src.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--quiet", args.repo, str(args.src)])
        if args.ref:
            run(["git", "-C", str(args.src), "checkout", "--quiet", args.ref])

    for name in PATCHES:
        patch = (args.patches / name).resolve()
        if not patch.is_file():
            raise SystemExit(f"missing patch: {patch}")
        # --check first so a partly-applied tree fails loudly instead of
        # leaving a half-patched fuzzer that still runs.
        run(["git", "-C", str(args.src), "apply", "--check", str(patch)])
        run(["git", "-C", str(args.src), "apply", str(patch)])

    if not args.venv.exists():
        run([sys.executable, "-m", "venv", str(args.venv)])
    pip = args.venv / "bin" / "pip"
    run([str(pip), "install", "--quiet", "--upgrade", "pip"])
    run([str(pip), "install", "--quiet", "-e", str(args.src)])

    typefuzz = args.venv / "bin" / "typefuzz"
    opfuzz = args.venv / "bin" / "opfuzz"
    for tool in (typefuzz, opfuzz):
        if not tool.exists():
            raise SystemExit(f"install finished but {tool} is missing")

    # The patch has to be live, not merely applied to a file: a venv installed
    # non-editable, or pointed at a different checkout, would silently lose it.
    #
    # Introspect the installed module rather than parsing CLI output. Two
    # cheaper probes were tried and both pass on a completely unpatched tree:
    # `--help` is a hand-written string, so a working flag can be missing from
    # it; and an unrecognised flag makes YinYang print usage and exit without
    # ever saying "unrecognized arguments" (worse, `-kl` parses as `-k -l` and
    # fails with a message about --logfolder). Checking the signature tests
    # the behaviour the patch exists for.
    probe = subprocess.run(
        [str(args.venv / "bin" / "python"), "-c",
         "import inspect, sys;"
         "from yinyang.src.parsing.Parse import prepare_seed;"
         "from yinyang.src.parsing.Parse import hoist_declarations;"
         "sys.exit(0 if 'keep_logic' in inspect.signature(prepare_seed).parameters else 1)"],
        capture_output=True, text=True,
    )
    if probe.returncode != 0:
        raise SystemExit(
            "installed yinyang lacks keep_logic/hoist_declarations; the patch did not take effect\n"
            + (probe.stderr or "")
        )
    print(f"yinyang installed in {args.venv} from {args.src} with --keep-logic and --hoist-declarations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
