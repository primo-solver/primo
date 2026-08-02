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


def archive_repo(config: Path, local: Path, remote: str, remote_dir: str) -> None:
    """Install a clean tracked-tree snapshot without copying local .git state."""

    remote_script = f"""
set -eu
workdir={shlex.quote(remote_dir)}
snapshot="$workdir/.primo-source-snapshot"
rm -rf "$snapshot"
mkdir -p "$snapshot"
tar -xf - -C "$snapshot"
mkdir -p "$workdir"
rsync -a --delete \
  --exclude .primo-source-snapshot \
  --exclude .git \
  --exclude .venv-build \
  --exclude .venv-docs \
  --exclude .venv-smac \
  --exclude .venv-yinyang \
  --exclude build \
  --exclude 'build-*' \
  --exclude contrib/local \
  --exclude contrib/slurm-local \
  --exclude contrib/src \
  --exclude docs/_build \
  --exclude fuzz-results \
  --exclude logs \
  --exclude results \
  --exclude slurm_logs \
  --exclude tmp \
  --exclude vendor \
  --exclude yinyang_bugs \
  --exclude yinyang_scratch \
  "$snapshot"/ "$workdir"/
rm -rf "$snapshot"
""".strip()
    shell_syntax_check(remote_script)

    archive_command = ["git", "-C", str(local), "archive", "--format=tar", "HEAD"]
    ssh_command = ["ssh", "-F", str(config), remote, remote_script]
    print(
        "+",
        " ".join(shlex.quote(part) for part in archive_command),
        "|",
        " ".join(shlex.quote(part) for part in ssh_command),
        flush=True,
    )
    archive_proc = subprocess.Popen(archive_command, stdout=subprocess.PIPE)
    try:
        ssh_proc = subprocess.run(
            ssh_command,
            stdin=archive_proc.stdout,
            text=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    finally:
        if archive_proc.stdout is not None:
            archive_proc.stdout.close()
    archive_return = archive_proc.wait()
    if archive_return != 0:
        raise subprocess.CalledProcessError(archive_return, archive_command)
    if ssh_proc.stdout:
        print(ssh_proc.stdout.decode(errors="replace"), end="")
    if ssh_proc.stderr:
        print(ssh_proc.stderr.decode(errors="replace"), end="")
    if ssh_proc.returncode != 0:
        raise subprocess.CalledProcessError(ssh_proc.returncode, ssh_command)


def build_command(workdir: str, cmake: str, target: str, jobs: int, git: str) -> str:
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
{shlex.quote(cmake)} -B build-slurm-rel -DCMAKE_BUILD_TYPE=Release \
  -DPRIMO_GIT_REVISION_OVERRIDE={shlex.quote(git)}
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


def active_primo_jobs_command(workdir: str) -> str:
    """Active jobs whose *actual* working directory is the tree about to be synced.

    Matching on the job name (anything starting with "primo") was both too
    broad and too weak: it blocked a QF_LRA sync because an unrelated
    Nelson-Oppen job was queued, while never checking whether either job used
    this directory at all. scontrol reports each job's WorkDir, which is the
    condition that actually matters -- a sync is destructive exactly when a
    running job is reading the tree being overwritten.
    """
    quoted = shlex.quote(workdir)
    return r"""
set -eu
command -v squeue >/dev/null 2>&1 || exit 0
command -v scontrol >/dev/null 2>&1 || exit 0
target=$(cd TARGET_DIR 2>/dev/null && pwd -P) || exit 0
for id in $(squeue -h -u "$USER" -t RUNNING,PENDING,CONFIGURING,COMPLETING -o '%i'); do
  wd=$(scontrol show job "$id" 2>/dev/null | tr ' ' '\n' | sed -n 's/^WorkDir=//p' | head -1)
  [ -n "$wd" ] || continue
  wd=$(cd "$wd" 2>/dev/null && pwd -P) || continue
  case "$wd" in
    "$target"|"$target"/*) echo "$id $wd" ;;
  esac
done
""".replace("TARGET_DIR", quoted).strip()


def ensure_no_active_primo_jobs(config: Path, remote: str, workdir: str) -> None:
    proc = ssh(config, remote, active_primo_jobs_command(workdir))
    if proc.stdout.strip():
        raise SystemExit(
            f"refusing destructive SLURM workspace preparation: the jobs above "
            f"are running out of {workdir} and this sync would overwrite the "
            f"tree underneath them. Run a different work stream from a "
            f"different --remote-dir, or wait. --allow-active-jobs overrides."
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
        "--sync-mode",
        choices=("archive", "rsync"),
        default="archive",
        help="archive sends only tracked HEAD files; rsync preserves the legacy full-tree sync",
    )
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
        ensure_no_active_primo_jobs(args.ssh_config, args.remote, args.remote_dir)

    if not args.check_only and not args.no_sync:
        ssh(args.ssh_config, args.remote, f"mkdir -p {shlex.quote(args.remote_dir)}")
        if args.sync_mode == "archive":
            archive_repo(args.ssh_config, args.local_dir, args.remote, args.remote_dir)
        else:
            rsync_repo(args.ssh_config, args.local_dir, args.remote, args.remote_dir)

    if not args.check_only:
        ssh(
            args.ssh_config,
            args.remote,
            build_command(args.remote_dir, args.cmake, args.target, args.jobs, git),
        )
    ssh(args.ssh_config, args.remote, check_command(args.remote_dir, git, args.target))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
