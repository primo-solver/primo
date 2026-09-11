#!/usr/bin/env python3
"""Create a reproducible Primo source and rendered-documentation archive."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import subprocess
import tarfile
from pathlib import Path, PurePosixPath

ROOT_FILES = {
    ".clang-format",
    ".editorconfig",
    "CMakeLists.txt",
    "LICENSE",
    "Makefile",
}
CODE_PREFIXES = ("contrib/", "scripts/", "src/", "tests/")
FORBIDDEN_SUFFIXES = {".markdown", ".md"}
FORBIDDEN_COMPONENTS = {".git", "_sources"}


def run_git(repo: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=repo,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def is_allowed_tracked_path(relative: PurePosixPath) -> bool:
    path = relative.as_posix()
    if path in ROOT_FILES:
        return True
    if path.startswith("benchmarks/"):
        return relative.suffix == ".smt2"
    if path.startswith(CODE_PREFIXES):
        return (
            relative.name != ".gitkeep"
            and relative.suffix.lower() not in FORBIDDEN_SUFFIXES
        )
    return False


def is_forbidden_archive_path(relative: PurePosixPath) -> bool:
    return (
        any(component in FORBIDDEN_COMPONENTS for component in relative.parts)
        or relative.suffix.lower() in FORBIDDEN_SUFFIXES
    )


def release_readme(revision: str) -> bytes:
    text = f"""Primo source snapshot
=====================

Revision: {revision}

This archive contains the Primo source code, build and test support, SMT-LIB
test inputs, and prebuilt HTML documentation. It intentionally contains no Git
history and no Markdown/MyST documentation sources.

Build requirements
------------------

- CMake 3.20 or newer
- a C++20 compiler
- GNU Make
- Python 3
- Git and network access for the first dependency bootstrap

Build and test
--------------

    make build
    make test
    make smoke

The first command downloads and builds pinned CLI11, CaDiCaL, and GMP
dependencies under contrib/local. The resulting solver is build/primo.

Small examples
--------------

    ./build/primo benchmarks/seeds/qf_uf/seed_basic.smt2
    ./build/primo benchmarks/seeds/qf_lra/seed_unsat_pivot.smt2
    ./build/primo benchmarks/seeds/qf_uflra/real_congruence_forced_unsat.smt2

The expected answers are sat, unsat, and unsat, respectively.

Documentation
-------------

Open html/index.html in a browser. The rendered site is included as-is except
for Sphinx's _sources tree, which is omitted to keep the original documentation
sources out of this distribution. Rebuilding the documentation therefore
requires the full Git checkout rather than this source snapshot.
"""
    return text.encode("utf-8")


def normalized_tar_info(
    name: str, size: int, epoch: int, executable: bool
) -> tarfile.TarInfo:
    info = tarfile.TarInfo(name)
    info.size = size
    info.mtime = epoch
    info.mode = 0o755 if executable else 0o644
    info.uid = 0
    info.gid = 0
    info.uname = "root"
    info.gname = "root"
    return info


def add_bytes(
    archive: tarfile.TarFile,
    name: str,
    contents: bytes,
    epoch: int,
    executable: bool = False,
) -> None:
    archive.addfile(
        normalized_tar_info(name, len(contents), epoch, executable),
        io.BytesIO(contents),
    )


def tracked_release_files(repo: Path) -> list[PurePosixPath]:
    output = run_git(repo, "ls-files", "-z")
    return sorted(
        PurePosixPath(item)
        for item in output.split("\0")
        if item and is_allowed_tracked_path(PurePosixPath(item))
    )


def html_release_files(repo: Path) -> list[Path]:
    html_root = repo / "docs" / "_build" / "html"
    if not (html_root / "index.html").is_file():
        raise SystemExit("rendered documentation is missing; run `make docs` first")
    files = []
    for path in html_root.rglob("*"):
        if not path.is_file():
            continue
        relative = PurePosixPath(path.relative_to(html_root).as_posix())
        if not is_forbidden_archive_path(relative):
            files.append(path)
    return sorted(files)


def create_archive(
    repo: Path, output_dir: Path, allow_dirty: bool
) -> tuple[Path, Path]:
    full_revision = run_git(repo, "rev-parse", "HEAD")
    short_revision = run_git(repo, "rev-parse", "--short=12", "HEAD")
    dirty = bool(run_git(repo, "status", "--short", "--untracked-files=all"))
    if dirty and not allow_dirty:
        raise SystemExit(
            "tracked files are modified; commit them or pass --allow-dirty"
        )
    revision = f"{full_revision}-dirty" if dirty else full_revision
    label = f"{short_revision}-dirty" if dirty else short_revision
    epoch = int(run_git(repo, "show", "-s", "--format=%ct", "HEAD"))
    root_name = f"primo-{label}"

    output_dir.mkdir(parents=True, exist_ok=True)
    archive_path = output_dir / f"{root_name}.tar.gz"
    checksum_path = archive_path.with_suffix(archive_path.suffix + ".sha256")

    tracked = tracked_release_files(repo)
    html = html_release_files(repo)
    with (
        archive_path.open("wb") as raw,
        gzip.GzipFile(
            filename="", mode="wb", fileobj=raw, mtime=epoch, compresslevel=9
        ) as compressed,
        tarfile.open(
            fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT
        ) as archive,
    ):
        add_bytes(archive, f"{root_name}/README.txt", release_readme(revision), epoch)
        add_bytes(
            archive,
            f"{root_name}/SOURCE-REVISION",
            f"{revision}\n".encode(),
            epoch,
        )
        for relative in tracked:
            path = repo / relative
            if path.is_symlink() or not path.is_file():
                raise SystemExit(f"unsupported release entry: {relative}")
            executable = bool(path.stat().st_mode & 0o111)
            add_bytes(
                archive,
                f"{root_name}/{relative.as_posix()}",
                path.read_bytes(),
                epoch,
                executable,
            )
        html_root = repo / "docs" / "_build" / "html"
        for path in html:
            relative = PurePosixPath(path.relative_to(html_root).as_posix())
            add_bytes(
                archive,
                f"{root_name}/html/{relative.as_posix()}",
                path.read_bytes(),
                epoch,
                bool(path.stat().st_mode & 0o111),
            )

    digest = hashlib.sha256(archive_path.read_bytes()).hexdigest()
    checksum_path.write_text(f"{digest}  {archive_path.name}\n", encoding="ascii")
    print(f"created {archive_path}")
    print(f"created {checksum_path}")
    print(f"revision {revision}")
    print(f"tracked files {len(tracked)}, HTML files {len(html)}")
    return archive_path, checksum_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("dist"))
    parser.add_argument(
        "--allow-dirty",
        action="store_true",
        help="create an explicitly -dirty labeled development archive",
    )
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    output_dir = args.output_dir
    if not output_dir.is_absolute():
        output_dir = repo / output_dir
    create_archive(repo, output_dir, args.allow_dirty)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
