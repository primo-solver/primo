#!/usr/bin/env python3
"""Build Primo against selected CaDiCaL tags and run the profile check.

This is the version axis for CaDiCaL dependency campaigns.  Primo still links
one CaDiCaL library per binary; this script creates one build directory per
CaDiCaL tag and then runs the same instance/profile comparison for each binary.
"""

from __future__ import annotations

import argparse
import csv
import re
import subprocess
from pathlib import Path


def safe_tag(tag: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", tag)


def run(command: list[str], cwd: Path) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, check=True)


def solver_version(solver: Path, repo: Path) -> str:
    proc = subprocess.run(
        [str(solver), "--version"],
        cwd=repo,
        check=False,
        text=True,
        capture_output=True,
        timeout=10,
    )
    return (proc.stdout + proc.stderr).strip().replace("\n", " | ")


def read_profile_rows(path: Path, tag: str, version: str) -> list[dict[str, str]]:
    with path.open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    for row in rows:
        row["cadical_tag"] = tag
        row["solver_version"] = version
    return rows


def write_merged(rows: list[dict[str, str]], out: Path) -> None:
    fieldnames = [
        "cadical_tag",
        "solver_version",
        "profile",
        "file",
        "command",
        "status",
        "answer",
        "seconds",
        "exit_code",
        "stderr_head",
    ]
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def write_summary(rows: list[dict[str, str]], out: Path, timeout: float) -> None:
    by_tag: dict[str, list[dict[str, str]]] = {}
    for row in rows:
        by_tag.setdefault(row["cadical_tag"], []).append(row)

    with out.open("w", encoding="utf-8") as handle:
        handle.write("# CaDiCaL Version Profile Check\n\n")
        handle.write(f"- Timeout: {timeout:g}s\n\n")
        for tag, tag_rows in by_tag.items():
            handle.write(f"## {tag}\n\n")
            handle.write(f"- Solver: `{tag_rows[0]['solver_version']}`\n\n")
            handle.write("| profile | file | status | answer | seconds |\n")
            handle.write("|---|---|---|---|---:|\n")
            for row in tag_rows:
                handle.write(
                    f"| {row['profile']} | `{row['file']}` | {row['status']} | "
                    f"{row['answer']} | {row['seconds']} |\n"
                )
            handle.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument("--tags", nargs="+", default=["rel-1.9.5", "rel-3.0.0"])
    parser.add_argument(
        "--prefix-root", type=Path, default=Path("contrib/cadical-versions")
    )
    parser.add_argument(
        "--build-root", type=Path, default=Path("build-cadical-versions")
    )
    parser.add_argument(
        "--out", type=Path, default=Path("results/cadical-version-profile/profile.csv")
    )
    parser.add_argument(
        "--summary",
        type=Path,
        default=Path("results/cadical-version-profile/profile.md"),
    )
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()

    repo = args.repo.resolve()
    all_rows: list[dict[str, str]] = []
    for tag in args.tags:
        tag_id = safe_tag(tag)
        prefix = (repo / args.prefix_root / tag_id).resolve()
        build_dir = (repo / args.build_root / tag_id).resolve()
        profile_dir = args.out.parent / tag_id
        profile_csv = profile_dir / "profile.csv"
        profile_md = profile_dir / "profile.md"

        run(
            [
                "python3",
                "contrib/install_cadical.py",
                "--prefix",
                str(prefix),
                "--tag",
                tag,
                "--clean-build",
            ],
            repo,
        )
        run(
            [
                "cmake",
                "-S",
                ".",
                "-B",
                str(build_dir),
                f"-DPRIMO_CADICAL_ROOT={prefix}",
                f"-DCMAKE_PREFIX_PATH={prefix};{repo / 'contrib/local'}",
            ],
            repo,
        )
        run(["cmake", "--build", str(build_dir), "--target", "primo", "-j4"], repo)

        solver = build_dir / "primo"
        version = solver_version(solver, repo)
        run(
            [
                "python3",
                "scripts/check_cadical_profile.py",
                "--solver",
                str(solver),
                "--timeout",
                str(args.timeout),
                "--out",
                str(profile_csv),
                "--summary",
                str(profile_md),
                *[str(file) for file in args.files],
            ],
            repo,
        )
        all_rows.extend(read_profile_rows(repo / profile_csv, tag, version))

    write_merged(all_rows, repo / args.out)
    write_summary(all_rows, repo / args.summary, args.timeout)
    print(f"wrote {args.out}")
    print(f"wrote {args.summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
