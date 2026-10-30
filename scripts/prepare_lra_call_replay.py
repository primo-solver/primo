#!/usr/bin/env python3
"""Validate the restricted generated conjunction syntax and deduplicate exports."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path


def check_conjunction(text):
    # This is an auditor for our generated format, not an SMT-LIB parser.
    tokens = re.findall(r"\(|\)|[^\s()]+", re.sub(r";[^\n]*", "", text))
    stack, commands = [], []
    for token in tokens:
        if token == "(":
            stack.append([])
        elif token == ")":
            assert stack, "unbalanced export"
            value = stack.pop()
            (stack[-1] if stack else commands).append(value)
        else:
            assert stack, "token outside command"
            stack[-1].append(token)
    assert not stack
    variables = set()
    counts = {"equalities": 0, "bounds": 0, "strict_bounds": 0, "queries": 0}
    expected = None

    def concrete_coefficient(term):
        if isinstance(term, str):
            return bool(re.fullmatch(r"[0-9]+", term))
        if len(term) == 2 and term[0] == "-":
            return isinstance(term[1], str) and bool(re.fullmatch(r"[0-9]+", term[1]))
        return (
            len(term) == 3
            and term[0] == "/"
            and concrete_coefficient(term[1])
            and isinstance(term[2], str)
            and bool(re.fullmatch(r"[1-9][0-9]*", term[2]))
            and (isinstance(term[1], str) or term[1][0] == "-")
        )

    def degree(term):
        if isinstance(term, str):
            assert term in variables or re.fullmatch(r"[0-9]+", term), term
            return int(term in variables)
        op, *args = term
        degrees = [degree(arg) for arg in args]
        assert op in ("+", "-", "*", "/")
        assert (
            (op in ("+", "*") and len(args) >= 1)
            or (op == "-" and len(args) in (1, 2))
            or (op == "/" and len(args) == 2)
        )
        if op == "*":
            assert (
                len(args) == 2
                and concrete_coefficient(args[0])
                and isinstance(args[1], str)
                and args[1] in variables
            ), "nonconforming QF_LRA coefficient"
        if op == "/":
            assert degrees[1] == 0, "nonconstant denominator"
            return degrees[0]
        result = sum(degrees) if op == "*" else max(degrees)
        assert result <= 1, "nonlinear export"
        return result

    for command in commands:
        head, *args = command
        if head == "set-logic":
            assert args == ["QF_LRA"]
        elif head == "set-info":
            assert (
                len(args) == 2
                and args[0] == ":status"
                and args[1] in ("sat", "unsat", "unknown")
            )
            assert expected is None
            expected = args[1]
        elif head == "declare-fun":
            name, parameters, sort = args
            assert re.fullmatch(r"v[0-9]+", name) and name not in variables
            assert parameters == [] and sort == "Real"
            variables.add(name)
        elif head == "assert":
            assert len(args) == 1 and len(args[0]) == 3
            relation, lhs, rhs = args[0]
            assert relation in ("=", "<", ">", "<=", ">=")
            degree(lhs)
            degree(rhs)
            counts["equalities" if relation == "=" else "bounds"] += 1
            counts["strict_bounds"] += relation in ("<", ">")
        else:
            assert head == "check-sat" and not args
            counts["queries"] += 1
    assert counts["queries"] == 1 and expected
    return {**counts, "variables": len(variables), "expected": expected}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("extraction", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    cubes = args.out / "cubes"
    cubes.mkdir(parents=True, exist_ok=False)
    unique = {}
    for path in sorted(args.extraction.glob("shard-*/summary.json")):
        shard = json.loads(path.read_text())
        assert shard["complete"]
        for parent in shard["records"]:
            assert not parent["wrong"]
            for call in parent["analysis"].get("exports", []):
                source = (
                    path.parent
                    / f"case-{parent['index']:04d}"
                    / "cubes"
                    / call["export"]
                )
                digest = hashlib.sha256(source.read_bytes()).hexdigest()
                if digest not in unique:
                    syntax = check_conjunction(source.read_text())
                    assert syntax["expected"] == (
                        "unsat" if call["conflict"] else "sat"
                    )
                    shutil.copyfile(source, cubes / (digest + ".smt2"))
                    unique[digest] = {
                        "sha256": digest,
                        "syntax": syntax,
                        "occurrences": [],
                    }
                unique[digest]["occurrences"].append(
                    {
                        "parent": parent["file"],
                        "parent_sha256": parent["input_sha256"],
                        "parent_coverage": parent["analysis"]["coverage"],
                        "call": call,
                    }
                )
    (args.out / "manifest.txt").write_text(
        "".join("cubes/" + digest + ".smt2\n" for digest in sorted(unique))
    )
    (args.out / "occurrences.json").write_text(
        json.dumps(list(unique.values()), indent=2) + "\n"
    )
    print(
        f"{sum(len(row['occurrences']) for row in unique.values())} occurrences, {len(unique)} unique conjunctions"
    )


if __name__ == "__main__":
    main()
