"""Bounded real-pipe regression: never send the next query before its response."""

import os
import select
import subprocess
import sys


def main():
    process = subprocess.Popen(
        [sys.argv[1], "--quiet", "--check-model", "--max-theory-conflicts=1000"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    try:
        def exchange(command, expected):
            process.stdin.write(command.encode())
            process.stdin.flush()
            ready, _, _ = select.select([process.stdout], [], [], 5)
            assert ready, f"no response before EOF to {command!r}"
            output = os.read(process.stdout.fileno(), 4096).decode()
            assert output == expected, (command, output)

        exchange("(set-option :print-success true)", "success\n")
        exchange("(set-option :produce-models true)", "success\n")
        exchange("(set-logic QF_LIA)", "success\n")
        exchange("(declare-const x Int)", "success\n")
        exchange("(assert (>= x 0))", "success\n")
        exchange("(check-sat)", "sat\n")
        exchange("(push 1)", "success\n")
        exchange("(assert (<= x (- 1)))", "success\n")
        exchange("(check-sat)", "unsat\n")
        exchange("(pop 1)", "success\n")
        exchange("(check-sat)", "sat\n")
        exchange("(exit)", "success\n")
        assert process.wait(timeout=5) == 0
    finally:
        if process.poll() is None:
            process.kill()
        process.communicate(timeout=5)


if __name__ == "__main__":
    main()
