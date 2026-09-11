"""Bounded real-pipe regression: never send the next query before its response."""

import os
import random
import select
import subprocess
import sys


def main():
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 0)
    process = subprocess.Popen(
        [sys.argv[1], "--quiet", "--check-model", "--max-theory-conflicts=1000"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    try:
        def exchange(command, expected):
            data = command.encode()
            while data:
                size = rng.randint(1, min(9, len(data)))
                process.stdin.write(data[:size])
                process.stdin.flush()
                data = data[size:]
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
        exchange("(declare-const |gate (;)| Bool)", "success\n")
        # Split within quoting/comment syntax, then require a response without EOF.
        for fragment in ['(set-info :source "a "', '"quoted"', '" (; | text"']:
            process.stdin.write(fragment.encode())
            process.stdin.flush()
            assert not select.select([process.stdout], [], [], 0.01)[0], \
                "response to an incomplete command"
        process.stdin.write(b")")
        process.stdin.flush()
        ready, _, _ = select.select([process.stdout], [], [], 5)
        assert ready, "fragmented string command did not complete"
        assert os.read(process.stdout.fileno(), 4096) == b"success\n"
        exchange("(assert ; discarded ) (\r|gate (;)|)", "success\n")
        exchange("(assert (let ((|x| 0)) (= x 0)))", "success\n")
        exchange("(set-info :custom (:tag let (assert (! true bogus))))", "success\n")
        exchange("(assert (! true :first :second 0 :third |let|))", "success\n")
        exchange("(check-sat)", "sat\n")
        exchange("(exit)", "success\n")
        assert process.wait(timeout=5) == 0
    finally:
        if process.poll() is None:
            process.kill()
        process.communicate(timeout=5)

    for text in [
        b"(set-info :source \"unfinished", b"(assert |unfinished", b"(assert true",
        b"(set-info :source :bad)",
        b"(set-logic QF_UF)(assert (! true bogus))",
        b"(set-option :custom :bad)",
    ]:
        result = subprocess.run(
            [sys.argv[1], "--quiet", "--max-theory-conflicts=1000"],
            input=text, capture_output=True, timeout=5, check=False,
        )
        assert result.returncode == 2, result
        assert result.stdout.startswith(b'(error "'), result.stdout


if __name__ == "__main__":
    main()
