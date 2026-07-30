#!/usr/bin/env python3
"""Prepare and validate the SLURM workspace used for benchmark campaigns."""

from __future__ import annotations

import argparse
import shlex
import subprocess
from pathlib import Path


def run(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(shlex.quote(part) for part in command), flush=True)
    return subprocess.run(command, check=check, text=True, capture_output=True)


def ssh(config: Path, remote: str, command: str) -> subprocess.CompletedProcess[str]:
    shell_syntax_check(command)
    proc = run(["ssh", "-F", str(config), remote, command])
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="")
    return proc


def shell_syntax_check(command: str) -> None:
    proc = subprocess.run(
        ["bash", "-n"],
        input=command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if proc.returncode != 0:
        raise SystemExit(f"generated remote shell failed bash -n:\n{proc.stderr}")


def rsync_repo(config: Path, local: Path, remote: str, remote_dir: str) -> None:
    excludes = [
        ".venv-build",
        ".venv-docs",
        ".venv-smac",
        ".venv-yinyang",
        "build",
        "build-*",
        "docs/_build",
        "fuzz-results",
        "logs",
        "results",
        "slurm_logs",
        "vendor",
        "yinyang_bugs",
        "yinyang_scratch",
        # Vendored native deps (GMP/CaDiCaL/ANTLR4/fuzzsmt/z3/...) built by
        # `make contrib-*` -- excluded for the same reason build/build-* are:
        # they contain host-specific absolute paths (e.g. fuzzsmt's wrapper
        # script embeds contrib/local/share/fuzzsmt/fuzzsmt.jar's *local*
        # resolved path). Syncing a local contrib/local over a remote one
        # silently clobbers it with paths that don't exist on the remote --
        # found 2026-07-24 when a QF_LRA campaign's fuzzsmt shards failed
        # with "Unable to access jarfile /workspace/..." after a second
        # prepare_benchmark_workspace.py sync. Each side provisions its own
        # copy via `make contrib-*`, never syncs it from the other.
        "contrib/local",
        "contrib/src",
    ]
    command = ["rsync", "-az", "--delete"]
    for pattern in excludes:
        command.extend(["--exclude", pattern])
    command.extend(
        [
            "-e",
            f"ssh -F {config}",
            f"{local.resolve()}/",
            f"{remote}:{remote_dir}/",
        ]
    )
    run(command)


def build_command(workdir: str, cmake: str, target: str, jobs: int) -> str:
    return f"""
set -eu
cd {shlex.quote(workdir)}
if [ ! -x {shlex.quote(cmake)} ]; then
  python3 -m venv .venv-build
  . .venv-build/bin/activate
  python -m pip install --upgrade pip cmake
fi
rm -f contrib/local/.cadical.stamp contrib/local/lib/libcadical.a
rm -rf contrib/src/cadical/build contrib/src/cadical/makefile \
  contrib/src/cadical/src/makefile build-slurm-rel
make contrib-cadical
{shlex.quote(cmake)} -B build-slurm-rel -DCMAKE_BUILD_TYPE=Release
{shlex.quote(cmake)} --build build-slurm-rel --target {shlex.quote(target)} -j{jobs}
build-slurm-rel/{shlex.quote(target)} --version
""".strip()


def check_command(workdir: str, expected_git: str, target: str) -> str:
    expected_line = f"git: {expected_git}"
    return f"""
set -eu
cd {shlex.quote(workdir)}
test -x build-slurm-rel/{shlex.quote(target)}
version="$(build-slurm-rel/{shlex.quote(target)} --version)"
printf '%s\\n' "$version"
printf '%s\\n' "$version" | grep -F {shlex.quote(expected_line)}
printf '%s\\n' "$version" | grep -F 'cadical:'
ldd build-slurm-rel/{shlex.quote(target)} >/dev/null
""".strip()


def active_primo_jobs_command() -> str:
    return r"""
set -eu
if command -v squeue >/dev/null 2>&1; then
  squeue -h -u "$USER" -o '%i %j %T' |
    awk '$3 ~ /^(RUNNING|PENDING|CONFIGURING|COMPLETING)$/ && $2 ~ /^primo/ {print}'
fi
""".strip()


def ensure_no_active_primo_jobs(config: Path, remote: str) -> None:
    proc = ssh(config, remote, active_primo_jobs_command())
    if proc.stdout.strip():
        raise SystemExit(
            "refusing destructive SLURM workspace preparation while active "
            "Primo jobs exist; use --allow-active-jobs only when the remote "
            "workdir is not used by those jobs"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--ssh-config", type=Path, default=Path("/tmp/llm2smt_slurm_ssh_config")
    )
    parser.add_argument("--remote", default="llm2smt-slurm")
    parser.add_argument("--remote-dir", default="primo-cadical-profile")
    parser.add_argument("--local-dir", type=Path, default=Path("."))
    parser.add_argument("--cmake", default=".venv-build/bin/cmake")
    parser.add_argument("--target", default="primo")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--no-sync", action="store_true")
    parser.add_argument(
        "--allow-active-jobs",
        action="store_true",
        help="Allow sync/rebuild even if active SLURM jobs with Primo job names exist",
    )
    args = parser.parse_args()

    if args.jobs < 1:
        raise SystemExit("--jobs must be positive")
    if not args.ssh_config.is_file():
        raise SystemExit(f"missing SSH config: {args.ssh_config}")

    git = run(
        ["git", "-C", str(args.local_dir), "rev-parse", "--short", "HEAD"]
    ).stdout.strip()
    status = run(["git", "-C", str(args.local_dir), "status", "--short"]).stdout.strip()
    if status:
        print(status)
        raise SystemExit("refusing to prepare SLURM workspace from a dirty tree")

    if not args.check_only and not args.allow_active_jobs:
        ensure_no_active_primo_jobs(args.ssh_config, args.remote)

    if not args.check_only and not args.no_sync:
        ssh(args.ssh_config, args.remote, f"mkdir -p {shlex.quote(args.remote_dir)}")
        rsync_repo(args.ssh_config, args.local_dir, args.remote, args.remote_dir)

    if not args.check_only:
        ssh(
            args.ssh_config,
            args.remote,
            build_command(args.remote_dir, args.cmake, args.target, args.jobs),
        )
    ssh(args.ssh_config, args.remote, check_command(args.remote_dir, git, args.target))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
