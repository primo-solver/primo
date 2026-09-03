#!/usr/bin/env python3
"""Build pinned Dolmen in a repository-local opam switch.

Dolmen's official 0.10 Linux executable requires glibc 2.35, while Primo's
SLURM environment currently provides 2.34. Building from the official opam
package produces a host-compatible validator without requiring root access or
a system OCaml installation.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import stat
import subprocess
import urllib.request
from pathlib import Path

OPAM_VERSION = "2.4.1"
OPAM_URL = (
    "https://github.com/ocaml/opam/releases/download/"
    f"{OPAM_VERSION}/opam-{OPAM_VERSION}-x86_64-linux"
)
OPAM_SHA256 = "30da8eb20c8f4cfd3818db867d85f26df1ca5ca27cb25910c0235c5a02e12d3b"
OCAML_VERSION = "4.14.1"
DOLMEN_VERSION = "0.10"


def prepend_environment_value(
    env: dict[str, str], name: str, value: str, separator: str
) -> None:
    current = env.get(name)
    env[name] = value if not current else f"{value}{separator}{current}"


def add_local_dependency_paths(env: dict[str, str], prefix: Path) -> None:
    """Expose dependencies installed under Primo's repository-local prefix."""
    include = str(prefix.resolve() / "include")
    library = str(prefix.resolve() / "lib")
    prepend_environment_value(env, "CPPFLAGS", f"-I{include}", " ")
    prepend_environment_value(env, "CFLAGS", f"-I{include}", " ")
    prepend_environment_value(env, "LDFLAGS", f"-L{library}", " ")
    prepend_environment_value(env, "LIBRARY_PATH", library, os.pathsep)
    prepend_environment_value(env, "LD_LIBRARY_PATH", library, os.pathsep)
    prepend_environment_value(
        env, "PKG_CONFIG_PATH", str(Path(library) / "pkgconfig"), os.pathsep
    )


def run(command: list[str], env: dict[str, str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, check=True, env=env)


def download_verified(url: str, sha256: str, destination: Path) -> None:
    if destination.is_file():
        digest = hashlib.sha256(destination.read_bytes()).hexdigest()
        if digest == sha256:
            return
        destination.unlink()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    urllib.request.urlretrieve(url, temporary)
    digest = hashlib.sha256(temporary.read_bytes()).hexdigest()
    if digest != sha256:
        temporary.unlink(missing_ok=True)
        raise SystemExit(f"checksum mismatch for {url}: expected {sha256}, got {digest}")
    temporary.replace(destination)


def reports_version(executable: Path, version: str) -> bool:
    if not executable.is_file():
        return False
    probe = subprocess.run(
        [str(executable), "--version"], check=False, capture_output=True, text=True
    )
    return probe.returncode == 0 and probe.stdout.strip() == version


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path("contrib/local"))
    parser.add_argument("--downloads", type=Path, default=Path("contrib/downloads"))
    parser.add_argument("--opam-root", type=Path, default=Path("contrib/src/dolmen-opam"))
    parser.add_argument("--switch", type=Path, default=Path("contrib/src/dolmen-switch"))
    args = parser.parse_args()

    destination = args.prefix / "bin" / "dolmen"
    if reports_version(destination, DOLMEN_VERSION):
        print(f"Dolmen {DOLMEN_VERSION} already installed at {destination}")
        return 0

    opam = args.downloads / f"opam-{OPAM_VERSION}-x86_64-linux"
    download_verified(OPAM_URL, OPAM_SHA256, opam)
    opam.chmod(opam.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    root = args.opam_root.resolve()
    switch = args.switch.resolve()
    env = os.environ.copy()
    env.update(
        {
            "OPAMROOT": str(root),
            "OPAMROOTISOK": "1",
            "OPAMYES": "1",
            "OPAMCONFIRMLEVEL": "unsafe-yes",
        }
    )
    add_local_dependency_paths(env, args.prefix)
    opam_command = str(opam.resolve())
    if not (root / "config").is_file():
        run(
            [
                opam_command,
                "init",
                "--bare",
                "--disable-sandboxing",
                "--yes",
                "default",
                "https://opam.ocaml.org",
            ],
            env,
        )
    if not (switch / "_opam" / "bin" / "ocamlc").is_file():
        switch.parent.mkdir(parents=True, exist_ok=True)
        run(
            [
                opam_command,
                "switch",
                "create",
                str(switch),
                f"ocaml-base-compiler.{OCAML_VERSION}",
                "--yes",
            ],
            env,
        )
    # --no-depexts: opam's depext probe asks the system package manager for
    # libgmp-dev and pkg-config, which needs root. pkg-config is already on the
    # system, and GMP comes from Primo's own contrib/local prefix, which
    # add_local_dependency_paths puts on the compiler and pkg-config search
    # paths above. Letting opam try to install system packages would fail on
    # any machine where the developer is not root.
    run(
        [
            opam_command,
            "install",
            "--switch",
            str(switch),
            "--yes",
            "--no-depexts",
            f"dolmen_bin.{DOLMEN_VERSION}",
        ],
        env,
    )

    built = switch / "_opam" / "bin" / "dolmen"
    if not reports_version(built, DOLMEN_VERSION):
        raise SystemExit(f"Dolmen build did not produce version {DOLMEN_VERSION}: {built}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(built, destination)
    destination.chmod(
        destination.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
    )
    if not reports_version(destination, DOLMEN_VERSION):
        raise SystemExit(f"installed Dolmen failed its version check: {destination}")
    print(f"installed Dolmen {DOLMEN_VERSION}: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
