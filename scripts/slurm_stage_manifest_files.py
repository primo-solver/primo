#!/usr/bin/env python3
"""Stage manifest-listed benchmark files into a remote SLURM workspace."""

from __future__ import annotations

import argparse
import shlex
import subprocess
from pathlib import Path, PurePosixPath


def run(command: list[str], *, stdin=None) -> subprocess.CompletedProcess[bytes]:
    print("+", " ".join(shlex.quote(part) for part in command), flush=True)
    return subprocess.run(command, stdin=stdin, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ssh-config", type=Path, required=True)
    parser.add_argument("--remote", required=True)
    parser.add_argument("--remote-dir", required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    if not args.ssh_config.is_file():
        raise SystemExit(f"missing SSH config: {args.ssh_config}")
    if not args.root.is_dir():
        raise SystemExit(f"missing local root: {args.root}")
    if args.root.is_absolute() or ".." in args.root.parts:
        raise SystemExit("--root must be a relative path inside the repository")
    if not args.manifest.is_file():
        raise SystemExit(f"missing manifest: {args.manifest}")

    paths = manifest_paths(args.manifest)
    missing = [path for path in paths if not (args.root / path).is_file()]
    if missing:
        shown = "\n".join(f"  {path}" for path in missing[:20])
        suffix = "" if len(missing) <= 20 else f"\n  ... {len(missing) - 20} more"
        raise SystemExit(f"{args.root}: missing {len(missing)} manifest files:\n{shown}{suffix}")

    remote_root = f"{args.remote_dir.rstrip('/')}/{args.root.as_posix()}"
    remote_script = f"""
set -eu
rm -rf {shlex.quote(remote_root)}
mkdir -p {shlex.quote(remote_root)}
tar -xf - -C {shlex.quote(remote_root)}
""".strip()
    shell_syntax_check(remote_script)

    tar_command = ["tar", "-C", args.root.as_posix(), "-cf", "-", *paths]
    ssh_command = ["ssh", "-F", str(args.ssh_config), args.remote, remote_script]
    print(
        "+",
        " ".join(shlex.quote(part) for part in tar_command),
        "|",
        " ".join(shlex.quote(part) for part in ssh_command),
        flush=True,
    )
    tar_proc = subprocess.Popen(tar_command, stdout=subprocess.PIPE)
    try:
        ssh_proc = subprocess.run(
            ssh_command,
            stdin=tar_proc.stdout,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
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
