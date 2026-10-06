#!/usr/bin/env python3
"""Sample native stacks with GDB/MI; instrumented wall time is not benchmark time."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import random
import re
import select
import signal
import subprocess
import time
from pathlib import Path


class InferiorExited(Exception):
    """GDB reported that the inferior exited while a command was pending."""


class Sampler:
    def __init__(self, command: list[str], out: Path, timeout: float) -> None:
        env = dict(os.environ, DEBUGINFOD_URLS="")
        self.proc = subprocess.Popen(
            ["gdb", "--quiet", "--nx", "--interpreter=mi2", "--args", *command],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            start_new_session=True, env=env,
        )
        self.log = (out / "gdb-mi.log").open("wb")
        self.buffer = b""
        self.lines: collections.deque[str] = collections.deque()
        self.stops: collections.deque[str] = collections.deque()
        self.token = 0
        self.deadline = time.monotonic() + timeout
        self.exit_reason: str | None = None
        self.inferior_pid: int | None = None

    def line(self) -> str:
        while not self.lines:
            remaining = self.deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("profile deadline exceeded")
            assert self.proc.stdout is not None
            if not select.select([self.proc.stdout], [], [], min(remaining, 1))[0]:
                continue
            block = os.read(self.proc.stdout.fileno(), 65536)
            if not block:
                raise RuntimeError("GDB output closed before inferior exit")
            self.log.write(block)
            self.log.flush()
            self.buffer += block
            parts = self.buffer.split(b"\n")
            self.buffer = parts.pop()
            self.lines.extend(p.decode("utf-8", errors="replace") for p in parts)
        line = self.lines.popleft()
        match = re.search(r'=thread-group-started,.*pid="(\d+)"', line)
        if match:
            self.inferior_pid = int(match.group(1))
        if line.startswith('*stopped,reason="exited'):
            self.exit_reason = line
            self.inferior_pid = None
            raise InferiorExited
        if line.startswith("*stopped,"):
            self.stops.append(line)
        return line

    def send(self, command: str) -> str:
        self.token += 1
        prefix = str(self.token) + "^"
        assert self.proc.stdin is not None
        self.proc.stdin.write(f"{self.token}{command}\n".encode())
        self.proc.stdin.flush()
        while True:
            line = self.line()
            if line.startswith(prefix):
                if line.startswith(prefix + "error"):
                    raise RuntimeError(line)
                return line

    def close(self) -> None:
        # Always reap the inferior as well as GDB, including deadline/error paths.
        if self.inferior_pid is not None:
            try:
                os.kill(self.inferior_pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        if self.proc.poll() is None:
            os.killpg(self.proc.pid, signal.SIGKILL)
        self.proc.wait()
        self.log.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--interval", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command or args.interval <= 0 or args.timeout <= 0:
        parser.error("command and positive interval/timeout required")
    args.out.mkdir(parents=True, exist_ok=False)
    rng = random.Random(args.seed)
    started = time.monotonic()
    sampler = Sampler(command, args.out, args.timeout)
    count = 0
    status = "failed"
    diagnostic = None
    try:
        sampler.send("-gdb-set pagination off")
        sampler.send("-gdb-set confirm off")
        sampler.send("-gdb-set mi-async on")
        sampler.send("-exec-run")
        with (args.out / "samples.jsonl").open("w") as stream:
            while True:
                # Jitter reduces periodic aliasing. Samples are wall-clock stack
                # observations, not hardware CPU events or call counts.
                time.sleep(args.interval * rng.uniform(0.8, 1.2))
                sampler.send("-exec-interrupt")
                while not sampler.stops:
                    sampler.line()
                stopped = sampler.stops.popleft()
                if 'signal-name="SIGINT"' not in stopped:
                    raise RuntimeError(f"unexpected inferior stop: {stopped}")
                response = sampler.send("-stack-list-frames 0 40")
                frames = [json.loads('"' + name + '"') for name in re.findall(
                    r'func="((?:[^"\\]|\\.)*)"', response
                )]
                stream.write(json.dumps({"seconds": time.monotonic() - started,
                                         "frames": frames}) + "\n")
                stream.flush()
                count += 1
                sampler.send("-exec-continue")
    except InferiorExited:
        status = "complete" if 'reason="exited-normally"' in (sampler.exit_reason or "") else "failed"
    except (RuntimeError, TimeoutError, OSError) as error:
        diagnostic = str(error)
    finally:
        sampler.close()
    report = {
        "status": status, "diagnostic": diagnostic, "command": command,
        "executable_sha256": hashlib.sha256(Path(command[0]).read_bytes()).hexdigest(),
        "exit_reason": sampler.exit_reason, "samples": count,
        "profile_wall_seconds": time.monotonic() - started,
        "interval": args.interval, "seed": args.seed,
        "note": "GDB wall-clock stack samples include profiler perturbation; do not use as benchmark timing.",
    }
    (args.out / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))
    return 0 if status == "complete" else 1


if __name__ == "__main__":
    raise SystemExit(main())
