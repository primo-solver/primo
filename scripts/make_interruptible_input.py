#!/usr/bin/env python3
"""Generate a QF_UF input whose parse and preprocessing phases take seconds.

The cancellation tests need an input that is still inside a long single-shot
phase -- parsing, frontend construction, preprocessing -- when a signal
arrives, rather than inside the SAT solver, which polls cancellation on its
own. Size, not hardness, is what puts the solver there, so the formula is
deliberately shallow and satisfiable.
"""

import argparse
import random
import sys


def generate(assertions: int, constants: int, seed: int) -> str:
    random.seed(seed)
    lines = ["(set-logic QF_UF)", "(declare-sort U 0)", "(declare-fun f (U) U)"]
    lines.extend("(declare-fun c%d () U)" % index for index in range(constants))
    for _ in range(assertions):
        left = random.randrange(constants)
        right = random.randrange(constants)
        lines.append(
            "(assert (or (= (f c%d) c%d) (not (= c%d c%d))))"
            % (left, right, left, right)
        )
    lines.append("(check-sat)")
    lines.append("(exit)")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output")
    parser.add_argument("--assertions", type=int, default=300000)
    parser.add_argument("--constants", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=7)
    args = parser.parse_args()
    if args.assertions < 1 or args.constants < 2:
        print("assertions and constants must be positive", file=sys.stderr)
        return 2
    with open(args.output, "w", encoding="utf-8") as output:
        output.write(generate(args.assertions, args.constants, args.seed))
    return 0


if __name__ == "__main__":
    sys.exit(main())
