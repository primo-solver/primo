#!/usr/bin/env python3
"""Stage manifest-listed benchmark files into a remote SLURM workspace."""

from __future__ import annotations

import argparse
import shlex
import subprocess
from pathlib import Path, PurePosixPath


def run(command: list[str], *, stdin=None) -> subprocess.CompletedProcess[bytes]:
    print("+", " ".join(shlex.quote(part) for part in command), flush=True)
    return subprocess.run(command, stdin=stdin, check=True, capture_output=True)


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


def manifest_paths(manifest: Path) -> list[str]:
    paths: list[str] = []
    for line_number, raw_line in enumerate(manifest.read_text().splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        path = PurePosixPath(line)
        if path.is_absolute() or ".." in path.parts:
            raise SystemExit(f"{manifest}:{line_number}: unsafe manifest path: {line}")
        paths.append(line)
    if not paths:
        raise SystemExit(f"{manifest}: no benchmark paths found")
    return paths


def all_smt2_paths(root: Path) -> list[str]:
    paths = sorted(
        path.relative_to(root).as_posix()
        for path in root.rglob("*.smt2")
        if path.is_file()
    )
    if not paths:
        raise SystemExit(f"{root}: no SMT-LIB files found")
    return paths


def compressed_tar_command(root: Path, paths: list[str]) -> list[str]:
    return ["tar", "-C", root.as_posix(), "-czf", "-", "--", *paths]


def remote_extract_script(remote_root: str) -> str:
    return f"""
set -eu
remote_root={shlex.quote(remote_root)}
staging="$remote_root.staging.$$"
cleanup() {{ rm -rf "$staging"; }}
trap cleanup EXIT HUP INT TERM
rm -rf "$staging"
mkdir -p "$staging"
tar -xzf - -C "$staging"
mkdir -p "$(dirname "$remote_root")"
rm -rf "$remote_root"
mv "$staging" "$remote_root"
trap - EXIT HUP INT TERM
""".strip()


def remote_copy_script(remote_source_root: str, remote_root: str) -> str:
    return f"""
set -euo pipefail
source_root={shlex.quote(remote_source_root)}
remote_root={shlex.quote(remote_root)}
staging="$remote_root.staging.$$"
path_list="$remote_root.paths.$$"
cleanup() {{ rm -rf "$staging"; rm -f "$path_list"; }}
trap cleanup EXIT HUP INT TERM
test -d "$source_root"
mkdir -p "$(dirname "$remote_root")"
rm -rf "$staging"
mkdir -p "$staging"
cat > "$path_list"
tar -C "$source_root" --null -T "$path_list" -cf - | tar -xf - -C "$staging"
rm -rf "$remote_root"
mv "$staging" "$remote_root"
rm -f "$path_list"
trap - EXIT HUP INT TERM
""".strip()


def stage_from_remote(
    config: Path,
    remote: str,
    remote_source_root: str,
    remote_root: str,
    paths: list[str],
) -> None:
    remote_script = remote_copy_script(remote_source_root, remote_root)
    shell_syntax_check(remote_script)
    path_list = b"".join(path.encode() + b"\0" for path in paths)
    command = ["ssh", "-F", str(config), remote, remote_script]
    print(
        "+",
        " ".join(shlex.quote(part) for part in command),
        f"<{len(paths)} remote paths>",
        flush=True,
    )
    proc = subprocess.run(command, input=path_list, capture_output=True, check=False)
    if proc.stdout:
        print(proc.stdout.decode(errors="replace"), end="")
    if proc.stderr:
        print(proc.stderr.decode(errors="replace"), end="")
    proc.check_returncode()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ssh-config", type=Path, required=True)
    parser.add_argument("--remote", required=True)
    parser.add_argument("--remote-dir", required=True)
    parser.add_argument(
        "--remote-source-dir",
        help=(
            "Existing workspace on --remote from which to copy selected files instead of "
            "streaming file contents from the local machine"
        ),
    )
    parser.add_argument("--root", type=Path, required=True)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument("--manifest", type=Path)
    selection.add_argument(
        "--all-smt2",
        action="store_true",
        help="stage every .smt2 file under --root in sorted relative-path order",
    )
    args = parser.parse_args()

    if not args.ssh_config.is_file():
        raise SystemExit(f"missing SSH config: {args.ssh_config}")
    if not args.root.is_dir():
        raise SystemExit(f"missing local root: {args.root}")
    if args.root.is_absolute() or ".." in args.root.parts:
        raise SystemExit("--root must be a relative path inside the repository")
    if args.manifest is not None:
        if not args.manifest.is_file():
            raise SystemExit(f"missing manifest: {args.manifest}")
        paths = manifest_paths(args.manifest)
    else:
        paths = all_smt2_paths(args.root)
    missing = [path for path in paths if not (args.root / path).is_file()]
    if missing:
        shown = "\n".join(f"  {path}" for path in missing[:20])
        suffix = "" if len(missing) <= 20 else f"\n  ... {len(missing) - 20} more"
        raise SystemExit(f"{args.root}: missing {len(missing)} manifest files:\n{shown}{suffix}")

    remote_root = f"{args.remote_dir.rstrip('/')}/{args.root.as_posix()}"
    if args.remote_source_dir is not None:
        remote_source_root = (
            f"{args.remote_source_dir.rstrip('/')}/{args.root.as_posix()}"
        )
        if remote_source_root == remote_root:
            raise SystemExit("--remote-source-dir and --remote-dir select the same corpus")
        stage_from_remote(
            args.ssh_config, args.remote, remote_source_root, remote_root, paths
        )
        print(
            f"staged {len(paths)} files within {args.remote}:"
            f"<remote-workspace>/{args.root}"
        )
        return 0

    remote_script = remote_extract_script(remote_root)
    shell_syntax_check(remote_script)

    tar_command = compressed_tar_command(args.root, paths)
    ssh_command = ["ssh", "-F", str(args.ssh_config), args.remote, remote_script]
    print(
        "+",
        " ".join(shlex.quote(part) for part in tar_command[:6]),
        f"<{len(paths)} paths>",
        "|",
        " ".join(shlex.quote(part) for part in ssh_command),
        flush=True,
    )
    tar_proc = subprocess.Popen(tar_command, stdout=subprocess.PIPE)
    try:
        ssh_proc = subprocess.run(
            ssh_command,
            stdin=tar_proc.stdout,
            capture_output=True,
            check=False,
        )
    finally:
        if tar_proc.stdout is not None:
            tar_proc.stdout.close()
    tar_return = tar_proc.wait()
    if tar_return != 0:
        raise subprocess.CalledProcessError(tar_return, tar_command)
    if ssh_proc.stdout:
        print(ssh_proc.stdout.decode(errors="replace"), end="")
    if ssh_proc.stderr:
        print(ssh_proc.stderr.decode(errors="replace"), end="")
    if ssh_proc.returncode != 0:
        raise subprocess.CalledProcessError(ssh_proc.returncode, ssh_command)

    print(f"staged {len(paths)} files under {args.remote}:<remote-workspace>/{args.root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
