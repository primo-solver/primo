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
    proc = run(["ssh", "-F", str(config), remote, command], check=False)
    if proc.stdout:
        print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="")
    proc.check_returncode()
    return proc


def shell_syntax_check(command: str) -> None:
    proc = subprocess.run(
        ["bash", "-n"],
        input=command,
        text=True,
        capture_output=True,
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
        # Vendored native deps (GMP/CaDiCaL/fuzzsmt/z3/...) built by
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
            capture_output=True,
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


def build_command(
    workdir: str,
    cmake: str,
    target: str,
    jobs: int,
    git: str,
    contrib_targets: list[str],
    reuse_dependencies: bool,
    extra_build_targets: list[str] | None = None,
) -> str:
    build_targets = " ".join(
        shlex.quote(name) for name in [target, *(extra_build_targets or [])]
    )
    make_targets = " ".join(
        shlex.quote(contrib_target) for contrib_target in contrib_targets
    )
    if reuse_dependencies:
        extra_targets = [
            target for target in contrib_targets if target != "contrib-required"
        ]
        extra_setup = ""
        if extra_targets:
            quoted_extra_targets = " ".join(
                shlex.quote(target) for target in extra_targets
            )
            extra_setup = f"make MAKEFLAGS= {quoted_extra_targets}"
        dependency_setup = f"""
missing_dependency=0
for required in \
  contrib/local/include/CLI/CLI.hpp \
  contrib/local/lib/libcadical.a \
  contrib/local/lib/libgmp.a \
  contrib/local/lib/libgmpxx.a; do
  if [ ! -e "$required" ]; then
    echo "missing reusable dependency: $required"
    missing_dependency=1
  fi
done
if [ ! -e contrib/local/include/ipasir_up.h ] && \
   [ ! -e contrib/local/include/ipasir.h ]; then
  echo "missing reusable dependency: IPASIR header (ipasir_up.h or ipasir.h)"
  missing_dependency=1
fi
if [ "$missing_dependency" -ne 0 ]; then
  echo 'reusable dependency installation is incomplete; provisioning it now'
  make MAKEFLAGS= contrib-required
fi
{extra_setup}
rm -rf build-slurm-rel
""".strip()
    else:
        dependency_setup = f"""
rm -f contrib/local/.cadical.stamp contrib/local/lib/libcadical.a
rm -rf contrib/src/cadical/build contrib/src/cadical/makefile \\
  contrib/src/cadical/src/makefile build-slurm-rel
make {make_targets}
""".strip()
    return f"""
set -eu
cd {shlex.quote(workdir)}
cmake_path={shlex.quote(cmake)}
if [ ! -x {shlex.quote(cmake)} ]; then
  python3 -m venv .venv-build
  . .venv-build/bin/activate
  python -m pip install --upgrade pip cmake
fi
case "$cmake_path" in
  /*) cmake_dir=$(dirname "$cmake_path"); export PATH="$cmake_dir:$PATH" ;;
  */*) cmake_dir=$(dirname "$cmake_path"); export PATH="$PWD/$cmake_dir:$PATH" ;;
esac
{dependency_setup}
"$cmake_path" -B build-slurm-rel -DCMAKE_BUILD_TYPE=Release \
  -DPRIMO_GIT_REVISION_OVERRIDE={shlex.quote(git)}
"$cmake_path" --build build-slurm-rel --target {build_targets} -j{jobs}
build-slurm-rel/{shlex.quote(target)} --version
""".strip()


def check_command(
    workdir: str, expected_git: str, target: str, required_tools: list[str]
) -> str:
    expected_line = f"git: {expected_git}"
    tool_checks = "\n".join(
        f"test -x {shlex.quote(tool)}\n{shlex.quote(tool)} --version >/dev/null"
        for tool in required_tools
    )
    return f"""
set -eu
cd {shlex.quote(workdir)}
test -x build-slurm-rel/{shlex.quote(target)}
version="$(build-slurm-rel/{shlex.quote(target)} --version)"
printf '%s\\n' "$version"
printf '%s\\n' "$version" | grep -F {shlex.quote(expected_line)}
printf '%s\\n' "$version" | grep -F 'cadical:'
ldd build-slurm-rel/{shlex.quote(target)} >/dev/null
{tool_checks}
""".strip()


def with_cxx_headers(command: str, manifest: str) -> str:
    """Use a pre-provisioned matching compiler header snapshot on compute nodes."""
    setup = f"""
set -eu
cxx_header_flags=
while IFS= read -r header_root; do
  case "$header_root" in
    ''|*[!a-zA-Z0-9_./-]*) echo 'invalid C++ header path in manifest' >&2; exit 2 ;;
  esac
  test -d "$header_root"
  cxx_header_flags="$cxx_header_flags -isystem $header_root"
done < {shlex.quote(manifest)}
test -n "$cxx_header_flags"
export CXXFLAGS="${{CXXFLAGS:-}}$cxx_header_flags"
printf '%s\\n' {shlex.quote("C++ header manifest: " + manifest)}
sha256sum {shlex.quote(manifest)}
""".strip()
    return setup + "\n" + command


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
    parser.add_argument(
        "--extra-target",
        action="append",
        default=[],
        help="Also build a helper target; version preflight still checks --target",
    )
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument(
        "--build-via-slurm",
        action="store_true",
        help="Run the clean build in a synchronous SLURM allocation, not on the SSH login host",
    )
    parser.add_argument(
        "--build-slurm-nodelist",
        help="Constrain the build allocation to these nodes; requires --build-via-slurm",
    )
    parser.add_argument(
        "--cxx-header-manifest",
        help="Remote absolute manifest of pre-provisioned include directories matching the compiler",
    )
    parser.add_argument(
        "--contrib-target",
        action="append",
        help=(
            "Make target to run before configuring the remote build. Repeat to add "
            "reference-tool targets such as contrib-z3. Defaults to contrib-required."
        ),
    )
    parser.add_argument(
        "--login-contrib-target",
        action="append",
        default=[],
        help="Run an explicit make target on the login host before the build allocation; "
        "use for binary downloads when compute nodes lack network access.",
    )
    parser.add_argument(
        "--with-z3",
        action="store_true",
        help="Install and require contrib/local/bin/z3 in the remote workspace.",
    )
    parser.add_argument(
        "--require-tool",
        action="append",
        default=[],
        help=(
            "Remote executable path that --check-only must find and run with --version, "
            "for example contrib/local/bin/z3. Repeat for multiple tools."
        ),
    )
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--no-sync", action="store_true")
    parser.add_argument(
        "--sync-mode",
        choices=("archive", "rsync"),
        default="archive",
        help="archive sends only tracked HEAD files; rsync preserves the legacy full-tree sync",
    )
    parser.add_argument(
        "--reuse-dependencies",
        action="store_true",
        help=(
            "Reuse the remote contrib/local installation while replacing the tracked source "
            "snapshot and rebuilding Primo from a clean build-slurm-rel directory"
        ),
    )
    parser.add_argument(
        "--allow-active-jobs",
        action="store_true",
        help="Allow sync/rebuild even if active SLURM jobs with Primo job names exist",
    )
    args = parser.parse_args()

    if args.jobs < 1:
        raise SystemExit("--jobs must be positive")
    if args.build_slurm_nodelist and not args.build_via_slurm:
        parser.error("--build-slurm-nodelist requires --build-via-slurm")
    if not args.ssh_config.is_file():
        raise SystemExit(f"missing SSH config: {args.ssh_config}")

    contrib_targets = args.contrib_target or ["contrib-required"]
    required_tools = list(args.require_tool)
    if args.with_z3:
        if "contrib-z3" not in contrib_targets:
            contrib_targets.append("contrib-z3")
        if "contrib/local/bin/z3" not in required_tools:
            required_tools.append("contrib/local/bin/z3")

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
        if args.login_contrib_target:
            targets = " ".join(shlex.quote(target) for target in args.login_contrib_target)
            ssh(args.ssh_config, args.remote,
                f"set -eu\ncd {shlex.quote(args.remote_dir)}\nmake MAKEFLAGS= {targets}")
        command = build_command(
            args.remote_dir,
            args.cmake,
            args.target,
            args.jobs,
            git,
            contrib_targets,
            args.reuse_dependencies,
            args.extra_target,
        )
        if args.cxx_header_manifest:
            command = with_cxx_headers(command, args.cxx_header_manifest)
        if args.build_via_slurm:
            node_option = (
                f"--nodelist={shlex.quote(args.build_slurm_nodelist)} "
                if args.build_slurm_nodelist
                else ""
            )
            command = (
                f"srun --ntasks=1 --job-name=primo-build --cpus-per-task={args.jobs} --mem=8G "
                f"--time=00:30:00 {node_option}bash -c {shlex.quote(command)}"
            )
        ssh(args.ssh_config, args.remote, command)
    ssh(
        args.ssh_config,
        args.remote,
        check_command(args.remote_dir, git, args.target, required_tools),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
