#!/usr/bin/env python3
"""Original SMT-LIB 2.7 attribute-grammar mutations, not copied solver tests."""

import argparse
import json
import random


def generate(count, seed):
    rng = random.Random(seed)
    values = [
        "0",
        "0.25",
        "#b01",
        "#x0f",
        '"a""b"',
        "symbol",
        "|let|",
        "|:keyword|",
        "()",
        "(:key let (assert (! true bogus)))",
    ]
    cases = []
    for index in range(count):
        tag = f":primo-{rng.randrange(1000000)}"
        value = rng.choice(values)
        valid = index % 2 == 0
        body = rng.choice(["true", "false"])
        annotation = index % 4 < 2
        if annotation:
            attributes = f"{tag} {value} :flag :other"
            if not valid:
                attributes = rng.choice(
                    [
                        "",
                        "bogus",
                        "|:flag|",
                        f"{tag} let",
                        f"{tag} {value} extra",
                        "(:flag)",
                    ]
                )
            text = f"(set-logic QF_UF)(assert (! {body} {attributes}))(check-sat)"
        else:
            payload = f"{tag} {value}"
            if not valid:
                payload = rng.choice([f"{tag} :bad", f"{tag} let", f"{tag} 0 1"])
            text = f"(set-info {payload})(set-logic QF_UF)(assert {body})(check-sat)"
        cases.append(
            {
                "id": f"attribute_{seed}_{index}",
                "group": "attribute-grammar",
                "valid": valid,
                "answers": ["sat" if body == "true" else "unsat"],
                "rule": "SMT-LIB 2.7 Sections 3.4 and 3.6",
                "input": text,
            }
        )
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--count", type=int, default=200)
    parser.add_argument("--seed", type=int, default=20260911)
    args = parser.parse_args()
    if args.count < 0:
        parser.error("count must be nonnegative")
    print(json.dumps({"cases": generate(args.count, args.seed)}, indent=2))


if __name__ == "__main__":
    main()
