#!/usr/bin/env python3
"""Validate actual session models against scoped query snapshots, outside timing runs."""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import os
import random
import select
import subprocess
import time
from pathlib import Path

from validate_qfuf_models_with_dolmen import command_spans, run_command, write_json


class QueryJournal:
    """Independent test oracle: retain context text, never frontend/solver IDs."""

    def __init__(self):
        self.logic = ""
        self.frames = [[]]

    def apply(self, command: str, head: tuple[str, ...]):
        kind = head[0]
        if kind == "set-logic":
            self.logic = command
        elif kind == "push":
            count = int(head[1])
            if count > 10000:
                raise ValueError("validator scope-depth budget exceeded")
            self.frames.extend([] for _ in range(count))
        elif kind == "pop":
            count = int(head[1])
            if count >= len(self.frames):
                raise ValueError("pop underflow")
            if count:
                del self.frames[-count:]
        elif kind in {"reset", "reset-assertions"}:
            self.frames = [[]]
            if kind == "reset":
                self.logic = ""
        elif kind in {
            "assert",
            "declare-sort",
            "declare-const",
            "declare-fun",
            "define-fun",
        }:
            self.frames[-1].append(command)
        elif kind not in {"check-sat", "get-model", "set-info", "set-option", "exit"}:
            raise ValueError(f"unsupported validation command: {kind}")

    def snapshot(self):
        return "\n".join(
            [
                self.logic,
                *(item for frame in self.frames for item in frame),
                "(check-sat)",
            ]
        )


class Client:
    def __init__(self, command, out: Path, timeout: float):
        self.stderr = (out / "solver.stderr").open("wb")
        self.transcript = (out / "solver.stdout").open("wb")
        self.sent = (out / "solver.smt2").open("w")
        self.process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.stderr
        )
        self.pending = b""
        self.timeout = timeout

    def send(self, command):
        self.sent.write(command + "\n")
        self.sent.flush()
        self.process.stdin.write((command + "\n").encode())
        self.process.stdin.flush()

    def response(self):
        deadline = time.monotonic() + self.timeout
        while True:
            text = self.pending.decode()
            stripped = text.lstrip()
            if stripped.startswith("("):
                end = response_end(text)
                if end is not None:
                    self.pending = text[end:].encode()
                    return text[:end].strip()
            elif "\n" in stripped:
                line, rest = stripped.split("\n", 1)
                self.pending = rest.encode()
                return line.strip()
            remaining = deadline - time.monotonic()
            if (
                remaining <= 0
                or not select.select([self.process.stdout], [], [], remaining)[0]
            ):
                raise TimeoutError("solver response timeout")
            data = os.read(self.process.stdout.fileno(), 65536)
            if not data:
                raise ValueError(f"solver closed output, exit={self.process.poll()}")
            self.transcript.write(data)
            self.transcript.flush()
            self.pending += data

    def finish(self):
        self.send("(exit)")
        self.process.stdin.close()
        if self.process.wait(timeout=self.timeout) != 0:
            raise ValueError(
                f"solver exited {self.process.returncode} after its answers"
            )
        remaining = self.process.stdout.read()
        self.transcript.write(remaining)
        if (self.pending + remaining).strip():
            raise ValueError("unexpected trailing solver output")

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait()
        for stream in (
            self.process.stdin,
            self.process.stdout,
            self.stderr,
            self.transcript,
            self.sent,
        ):
            stream.close()


def response_end(text):
    """Find one parenthesized response, including the valid empty model ()."""
    depth = 0
    quote = None
    for index, char in enumerate(text):
        if quote:
            if char == quote:
                quote = None
        elif char in {'"', "|"}:
            quote = char
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return index + 1
    return None


def validate(text, directory, args):
    directory.mkdir(parents=True, exist_ok=False)
    (directory / "original.smt2").write_text(text)
    command = [args.solver, *args.solver_arg, "--check-model"]
    write_json(
        directory / "invocation.json",
        {
            "solver": command,
            "reference": [args.ref, *args.ref_arg],
            "dolmen": args.dolmen,
            "timeout": args.timeout,
            "dolmen_timeout": args.dolmen_timeout,
            "input_sha256": hashlib.sha256(text.encode()).hexdigest(),
        },
    )
    journal = QueryJournal()
    client = Client(command, directory, args.timeout)
    rows = []
    try:
        client.send("(set-option :produce-models true)")
        client.send("(set-option :print-success false)")
        for start, end, head in command_spans(text):
            kind = head[0]
            raw = text[start:end]
            if kind == "exit":
                break
            if kind == "get-model":
                continue  # Request models only for the SAT responses actually observed.
            if kind == "set-option":
                if len(head) > 1 and head[1] in {":produce-models", ":print-success"}:
                    continue
                if head != ("set-option", ":global-declarations", "false"):
                    raise ValueError(f"unsupported validation option: {head}")
            journal.apply(raw, head)
            client.send(raw)
            if kind == "reset":
                client.send("(set-option :produce-models true)")
                client.send("(set-option :print-success false)")
            if kind != "check-sat":
                continue
            answer = client.response()
            query = directory / f"query-{len(rows):04d}"
            query.mkdir()
            snapshot = query / "context.smt2"
            snapshot.write_text(journal.snapshot() + "\n")
            row = {
                "query": len(rows),
                "answer": answer,
                "outcome": "pending",
                "context_sha256": hashlib.sha256(snapshot.read_bytes()).hexdigest(),
            }
            rows.append(row)
            write_json(query / "result.json", row)
            status, code, stdout, stderr, _ = run_command(
                [args.ref, *args.ref_arg, str(snapshot)], args.timeout
            )
            (query / "reference.stdout").write_text(stdout)
            (query / "reference.stderr").write_text(stderr)
            if status != "done" or code != 0:
                raise ValueError(f"reference failure: {status}, exit={code}")
            if answer not in {"sat", "unsat"} or stdout.strip() != answer:
                raise ValueError(
                    f"answer disagreement or inconclusive: {answer!r}, {stdout!r}"
                )
            if answer == "sat":
                client.send("(get-model)")
                model = client.response()
                if not model.startswith("(") or model.startswith("(error"):
                    raise ValueError(f"missing model: {model}")
                response = query / "model.rsmt2"
                response.write_text("sat\n" + model + "\n")
                dolmen_command = [
                    args.dolmen,
                    "--check-model=true",
                    "--strict=false",
                    "--size=1G",
                    "-r",
                    str(response),
                    str(snapshot),
                ]
                status, code, stdout, stderr, _ = run_command(
                    dolmen_command, args.dolmen_timeout
                )
                (query / "dolmen.stdout").write_text(stdout)
                (query / "dolmen.stderr").write_text(stderr)
                row["dolmen_command"] = dolmen_command
                if status != "done" or code != 0:
                    raise ValueError(f"Dolmen did not pass: {status}, exit={code}")
            row["outcome"] = "provisional-pass"
            write_json(query / "result.json", row)
        client.finish()  # A post-answer crash invalidates all provisional results.
        for row in rows:
            row["outcome"] = "pass"
            write_json(directory / f"query-{row['query']:04d}" / "result.json", row)
        result = {"outcome": "pass", "queries": rows}
    except (ValueError, OSError, TimeoutError, subprocess.TimeoutExpired) as error:
        result = {"outcome": "failure", "message": str(error), "queries": rows}
    finally:
        client.close()
    write_json(directory / "result.json", result)
    return result


def generated_session(seed):
    rng = random.Random(seed)
    logic = ("QF_UF", "QF_LIA", "QF_LRA", "QF_UFLIA", "QF_UFLRA")[seed % 5]
    sort = "Int" if logic.endswith("LIA") else "Real"
    commands = [f"(set-logic {logic})", "(declare-const b Bool)"]
    commands.extend(
        [
            "(declare-const global_flag Bool)",
            "(define-fun capture ((unused Bool)) Bool global_flag)",
            "(assert (not global_flag))",
            "(assert (let ((global_flag true)) (not (capture global_flag))))",
        ]
    )
    if logic != "QF_UF":
        commands.extend(
            [f"(declare-const x {sort})", f"(assert (>= x {rng.randrange(5)}))"]
        )
    commands.append("(check-sat)")
    for _ in range(4):
        commands.extend(["(push 1)", "(declare-const local Bool)"])
        if logic == "QF_UF":
            commands.extend(
                [
                    "(declare-sort U 0)",
                    "(declare-const a U)",
                    "(declare-fun f (U) U)",
                    "(assert (= (f a) a))",
                ]
            )
            clause = rng.choice(["local", "(not local)", "(or b local)"])
            commands.extend(
                [f"(assert {clause})", "(check-sat)", f"(assert (not {clause}))"]
            )
        else:
            if "UFL" in logic:
                commands.extend(
                    [f"(declare-fun f ({sort}) {sort})", "(assert (= (f x) (+ x 1)))"]
                )
            commands.extend(
                [
                    f"(define-fun next ((v {sort})) {sort} (+ v 1))",
                    "(assert (= (next x) (+ x 1)))",
                    "(check-sat)",
                    f"(assert (<= x (- {rng.randrange(1, 6)})))",
                ]
            )
        commands.extend(["(check-sat)", "(pop 1)", "(check-sat)"])
    commands.extend(
        [
            "(reset-assertions)",
            "(declare-const x Bool)",
            "(assert x)",
            "(check-sat)",
            "(reset)",
            "(set-logic QF_UF)",
            "(assert false)",
            "(check-sat)",
            "(exit)",
        ]
    )
    return "\n".join(commands) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="*", type=Path)
    parser.add_argument("--count", type=int, default=0)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--solver", required=True)
    parser.add_argument("--solver-arg", action="append", default=[])
    parser.add_argument("--ref", default="contrib/local/bin/z3")
    parser.add_argument("--ref-arg", action="append", default=[])
    parser.add_argument("--dolmen", default="dolmen")
    parser.add_argument("--timeout", type=float, default=10)
    parser.add_argument("--dolmen-timeout", type=float, default=20)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.count < 0 or (not args.files and not args.count):
        parser.error("provide files or a positive --count")
    args.out.mkdir(parents=True, exist_ok=False)
    version = subprocess.run(
        [args.solver, "--version"], capture_output=True, text=True, check=True
    )
    write_json(
        args.out / "run.json",
        {
            "solver_version": version.stdout,
            "solver_sha256": hashlib.sha256(Path(args.solver).read_bytes()).hexdigest(),
            "seed": args.seed,
            "count": args.count,
            "arguments": vars(args)
            | {"out": str(args.out), "files": [str(path) for path in args.files]},
        },
    )
    results = []
    inputs = ((str(path), path.read_text()) for path in args.files)
    generated = (
        (f"seed-{seed}", generated_session(seed))
        for seed in range(args.seed, args.seed + args.count)
    )
    for index, (name, text) in enumerate(itertools.chain(inputs, generated)):
        result = validate(text, args.out / f"session-{index:05d}", args)
        result["input"] = name
        results.append(result)
        write_json(args.out / "summary.json", results)
        if result["outcome"] != "pass":
            print(json.dumps(result))
            return 1
    print(
        json.dumps(
            {
                "sessions": len(results),
                "queries": sum(len(r["queries"]) for r in results),
                "sat_models": sum(
                    q["answer"] == "sat" for r in results for q in r["queries"]
                ),
            }
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
