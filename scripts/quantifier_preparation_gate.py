#!/usr/bin/env python3
"""Original generated/seed-mutation correctness gate for ground-term preparation.

Uses only the standard library and repository command scanning. Native mutations
wrap assertions in equivalences using fresh Boolean binders, preserving source
declaration environments. This is a targeted metamorphic gate, not TypeFuzz.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import random
import shlex
import socket
from collections import Counter
from pathlib import Path

from compare_preprocessing_size import parse_stats
from validate_qfuf_models_with_dolmen import command_spans, run_command, write_json

ORDERS = ("sum", "max", "max-colex", "leximax", "deepening", "random")
DEFINITE = {"sat", "unsat"}


def digest(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


def integer(value: int) -> str:
    return str(value) if value >= 0 else f"(- {-value})"


def quantified(seed: int, index: int) -> tuple[str, str]:
    rng = random.Random(seed + index)
    prelude = """(set-logic UF)
(declare-sort U 0)
(declare-const a U) (declare-const c U)
(declare-fun h (Bool) U) (declare-fun f (U) U)
(declare-fun p (U) Bool) (declare-fun q (U U) Bool)
"""
    term, ground = "x", "a"
    for _ in range(rng.randrange(4)):
        term, ground = f"(f {term})", f"(f {ground})"
    boolean = "b" if rng.randrange(2) else "(not b)"
    bodies = [
        (
            [
                (
                    f"(exists ((b Bool)) (and (distinct (h {boolean}) (h true)) "
                    f"(distinct (h {boolean}) (h false))))"
                )
            ],
            "unsat",
        ),
        ([f"(exists ((b Bool)) (= (h {boolean}) (h true)))"], "sat"),
        (
            [
                f"(forall ((x U)) (! (p {term}) :pattern ((p {term}))))",
                f"(not (p {ground}))",
            ],
            "unsat",
        ),
        (
            [
                (
                    f"(exists ((x U)) (exists ((b Bool)) "
                    f"(or (p {term}) (= (h (and {boolean} (p x))) (h true)))))"
                )
            ],
            "sat",
        ),
        (
            [
                "(exists ((x U)) (forall ((y U)) (q x y)))",
                "(forall ((z U)) (not (q z a)))",
            ],
            "unsat",
        ),
        (
            [
                f"(forall ((b Bool)) (= (h {boolean}) (h true)))",
                "(distinct (h false) (h true))",
            ],
            "unsat",
        ),
        (
            [
                (
                    f"(forall ((x U)) (forall ((b Bool)) "
                    f"(= (h (and {boolean} (p {term}))) (h true))))"
                ),
                f"(p {ground})",
                "(distinct (h false) (h true))",
            ],
            "unsat",
        ),
        (["(not (forall ((b Bool)) (distinct (h b) (h true))))"], "sat"),
    ]
    assertions, expected = bodies[index % len(bodies)]
    # Boolean wrappers vary sharing/rewriting without changing the oracle.
    for _ in range(rng.randrange(4)):
        position = rng.randrange(len(assertions))
        body = assertions[position]
        assertions[position] = rng.choice(
            [
                f"(not (not {body}))",
                f"(and true {body})",
                f"(or false {body})",
                f"(ite (p c) {body} {body})",
            ]
        )
    return prelude + "".join(
        f"(assert {body})\n" for body in assertions
    ) + "(check-sat)\n", expected


def arithmetic(seed: int, index: int) -> tuple[str, str]:
    rng = random.Random(seed + index)
    value, offset, divisor = (
        rng.randrange(-100, 101),
        rng.randrange(-10, 11),
        rng.randrange(2, 12),
    )
    term = f"(+ x {integer(offset)})"
    evaluated = value + offset
    if index % 3 == 0:
        term, evaluated = f"(abs {term})", abs(evaluated)
    elif index % 3 == 1:
        term = f"(ite (< x 0) (- {term}) {term})"
        evaluated = -evaluated if value < 0 else evaluated
    remainder = f"(mod {term} {divisor})"
    quotient = f"(div {term} {divisor})"
    want = evaluated % divisor + (index % 2)
    mixed = index % 4 == 0
    prelude = (
        f"(set-logic {'QF_UFLIA' if mixed else 'QF_LIA'})\n(declare-const x Int)\n"
    )
    assertions = [
        f"(= x {integer(value)})",
        f"(= {remainder} {want})",
        f"(= (+ (* {divisor} {quotient}) {remainder}) {term})",
    ]
    if mixed:
        prelude += "(declare-fun h (Bool) Int)\n"
        assertions.append(f"(= (h (= {remainder} {want})) (h true))")
    return prelude + "".join(
        f"(assert {body})\n" for body in assertions
    ) + "(check-sat)\n", ("sat" if index % 2 == 0 else "unsat")


def mutate(source: str, seed: int, index: int) -> str:
    rng = random.Random(seed + index)
    commands = []
    serial = 0
    spans = command_spans(source)
    if sum(head[0] == "check-sat" for _, _, head in spans) != 1:
        raise ValueError("native mutation requires exactly one query")
    for begin, end, head in spans:
        command = source[begin:end]
        if head[0] == "assert":
            body = command[command.index("assert") + len("assert") : -1].strip()
            name = f"primo_gate_{seed}_{index}_{serial}"
            while name in source:
                name += "_fresh"
            serial += 1
            binder = f"|{name}|"
            body = rng.choice(
                [
                    f"(forall (({binder} Bool)) (or {binder} {body}))",
                    f"(exists (({binder} Bool)) (and {binder} {body}))",
                    f"(exists (({binder} Bool)) (ite {binder} {body} {body}))",
                    f"(not (not {body}))",
                ]
            )
            commands.append(f"(assert {body})")
        elif head[0] in {
            "set-logic",
            "declare-sort",
            "declare-fun",
            "declare-const",
            "define-fun",
        }:
            commands.append(command)
        elif head[0] not in {
            "set-info",
            "set-option",
            "check-sat",
            "exit",
            "get-model",
        }:
            raise ValueError(f"unsupported seed command {head[0]}")
    if not serial:
        raise ValueError("seed has no assertion")
    return "\n".join(commands) + "\n(check-sat)\n"


def seeds(root: Path) -> list[Path]:
    families: dict[str, list[Path]] = {}
    for path in root.rglob("*.smt2"):
        if path.stat().st_size <= 6000:
            families.setdefault(path.relative_to(root).parts[0], []).append(path)
    return [
        path
        for family in sorted(families)
        for path in sorted(families[family], key=lambda p: (p.stat().st_size, str(p)))[
            :4
        ]
    ]


def classify(status: str, code: int | None, stdout: str) -> str:
    if status == "timeout":
        return "timeout"
    if code != 0:
        return "crash" if code is not None and code < 0 else "error"
    answers = [
        line.strip()
        for line in stdout.splitlines()
        if line.strip() in DEFINITE | {"unknown", "unsupported"}
    ]
    return answers[0] if len(answers) == 1 and "(error" not in stdout else "error"


def preprocessing_stopped_before_search(answer: str, stderr: str) -> bool:
    stats = parse_stats(stderr)
    return (
        answer in {"unknown", "unsat"}
        and stats.get(":sat.solve-calls") == "0"
        and all(
            stats.get(key, "0") == "0"
            for key in (":quant.instances", ":quant.skolem-lemmas", ":time.solve-us")
        )
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--mode", choices=("generated", "mutations", "arithmetic"), required=True
    )
    parser.add_argument("--solver", required=True)
    parser.add_argument("--expected-git", help="Require this embedded Primo revision")
    parser.add_argument(
        "--check-preprocess-only",
        action="store_true",
        help="Also require the preprocessing diagnostic to stop without search",
    )
    parser.add_argument(
        "--ref", action="append", required=True, help="Shell-split command, no shell"
    )
    parser.add_argument("--corpus", type=Path)
    parser.add_argument("--seed", type=int, default=20260914)
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--shards", type=int, default=1)
    parser.add_argument("--shard-index", type=int, default=0)
    parser.add_argument("--timeout", type=float, default=5)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.count < 1 or args.shards < 1 or not 0 <= args.shard_index < args.shards:
        parser.error("invalid count/shard configuration")
    if args.mode == "mutations" and args.corpus is None:
        parser.error("mutations require --corpus")
    args.out.mkdir(parents=True, exist_ok=False)
    sources = seeds(args.corpus) if args.mode == "mutations" else []
    if args.mode == "mutations" and not sources:
        parser.error("no native seeds found")
    identity = {
        "args": vars(args) | {"out": str(args.out), "corpus": str(args.corpus)},
        "host": socket.gethostname(),
        "binaries": {},
        "native_seeds": [
            {"path": str(p), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
            for p in sources
        ],
    }
    commands = {
        "primo": [args.solver],
        **{f"ref{i}": shlex.split(ref) for i, ref in enumerate(args.ref)},
    }
    for label, command in commands.items():
        path = Path(command[0]).resolve()
        status, code, stdout, stderr, _ = run_command([str(path), "--version"], 10)
        identity["binaries"][label] = {
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "version": stdout + stderr,
            "status": status,
            "exit": code,
        }
        if label == "primo":
            revision = next(
                (
                    line.removeprefix("git: ")
                    for line in stdout.splitlines()
                    if line.startswith("git: ")
                ),
                None,
            )
            if status != "done" or code != 0 or not revision:
                raise SystemExit("Primo version probe failed")
            if args.expected_git is not None and revision != args.expected_git:
                raise SystemExit(f"wrong candidate revision: {revision}")
            identity["commit"] = revision
    write_json(args.out / "run.json", identity)
    counts: Counter[str] = Counter()
    failures = 0
    with (args.out / "rows.jsonl").open("w", encoding="utf-8") as rows:
        for index in range(args.shard_index, args.count, args.shards):
            folder = args.out / f"case-{index:05d}"
            folder.mkdir()
            if args.mode == "mutations":
                source = sources[index % len(sources)]
                original = source.read_text()
                text = mutate(original, args.seed, index)
                expected = next(
                    (
                        head[2]
                        for _, _, head in command_spans(original)
                        if len(head) == 3
                        and head[:2] == ("set-info", ":status")
                        and head[2] in DEFINITE
                    ),
                    None,
                )
                (folder / "original.smt2").write_bytes(source.read_bytes())
            else:
                generator = arithmetic if args.mode == "arithmetic" else quantified
                text, expected = generator(args.seed, index)
            path = folder / "input.smt2"
            path.write_text(text)
            results = []
            for label, command in commands.items():
                configurations = [("", [])]
                if label == "primo":
                    orders = ORDERS if args.mode == "generated" else ("sum",)
                    configurations = [
                        (
                            order,
                            [
                                f"--quantifier-tuple-order={order}",
                            ],
                        )
                        for order in orders
                    ]
                    if args.check_preprocess_only:
                        configurations.append(
                            ("preprocess-only", ["--preprocess-only", "--stats"])
                        )
                for configuration, options in configurations:
                    actual = command + options
                    if label == "primo":
                        actual += [
                            "--quiet",
                            "--max-theory-conflicts=1000",
                            "--quantifier-max-instances=2000",
                        ]
                    actual += [str(path)]
                    status, code, stdout, stderr, elapsed = run_command(
                        actual, args.timeout
                    )
                    answer = classify(status, code, stdout)
                    if (
                        configuration == "preprocess-only"
                        and not preprocessing_stopped_before_search(answer, stderr)
                    ):
                        answer = "error"
                    key = f"{label}-{configuration}".rstrip("-")
                    (folder / f"{key}.stdout").write_text(stdout)
                    (folder / f"{key}.stderr").write_text(stderr)
                    row = {
                        "index": index,
                        "profile": key,
                        "command": actual,
                        "answer": answer,
                        "exit": code,
                        "seconds": elapsed,
                        "input_sha256": digest(text),
                    }
                    rows.write(json.dumps(row) + "\n")
                    rows.flush()
                    results.append(row)
                    counts[f"{key}:{answer}"] += 1
            decisive = {row["answer"] for row in results if row["answer"] in DEFINITE}
            if expected is not None:
                decisive.add(expected)
            bad = len(decisive) > 1 or any(
                row["answer"] in {"error", "crash", "unsupported"} for row in results
            )
            if bad:
                failures += 1
            refs = {
                row["answer"] for row in results if row["profile"].startswith("ref")
            }
            counts["cases"] += 1
            counts[
                "failed"
                if bad
                else "reference-definite"
                if refs & DEFINITE
                else "reference-inconclusive"
            ] += 1
            write_json(
                args.out / "summary.json",
                {"counts": dict(counts), "failures": failures},
            )
            if bad:
                print(f"blocking result at {folder}", flush=True)
                return 1
    print(json.dumps({"counts": dict(counts), "failures": failures}), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
