#!/usr/bin/env python3
"""Freeze paired SOI mechanism/leave-one-out profiles and immutable binaries."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from benchmark_model_replay import digest, write_json


def configurations(parameters):
    short = [
        "--lra-soi-selection=short-column",
        f"--lra-soi-pricing-candidates={parameters['budget']}",
        f"--lra-soi-stall-limit={parameters['stall_limit']}",
    ]
    cache = ["--lra-soi-cached-conflicts"]
    linear = ["--lra-soi-linear-maintenance"]
    growing = ["--lra-soi-minimize=row-growing"]
    variants = {
        "sparse": ["--lra-pivoting-rule=sparse"],
        "historical": ["--lra-pivoting-rule=soi"],
        "paper": ["--lra-pivoting-rule=soi", "--lra-soi-selection=paper"],
        "short": ["--lra-pivoting-rule=soi", *short],
        "cache": ["--lra-pivoting-rule=soi", *short, *cache],
        "linear": ["--lra-pivoting-rule=soi", *short, *cache, *linear],
        "final": ["--lra-pivoting-rule=soi", *short, *cache, *linear, *growing],
        "no-short": [
            "--lra-pivoting-rule=soi",
            "--lra-soi-selection=paper",
            *cache,
            *linear,
            *growing,
        ],
        "no-cache": ["--lra-pivoting-rule=soi", *short, *linear, *growing],
        "no-linear": ["--lra-pivoting-rule=soi", *short, *cache, *growing],
        "deletion": [
            "--lra-pivoting-rule=soi",
            *short,
            *cache,
            *linear,
            "--lra-soi-minimize=deletion",
        ],
        "none": [
            "--lra-pivoting-rule=soi",
            *short,
            *cache,
            *linear,
            "--lra-soi-minimize=none",
        ],
    }
    configs = []
    for fixed in (0, 1):
        suffix = ["--lra-fixed-elimination"] if fixed else []
        for name, args in variants.items():
            configs.append(
                {
                    "id": f"{name}-g1-f{fixed}",
                    "binary": "candidate",
                    "args": [*args, *suffix],
                }
            )
        for name in ("sparse", "soi"):
            configs.append(
                {
                    "id": f"original-{name}-g1-f{fixed}",
                    "binary": "original",
                    "args": [f"--lra-pivoting-rule={name}", *suffix],
                }
            )
    return configs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parameters", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--candidate-sha256", required=True)
    parser.add_argument("--candidate-source", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    parameters = json.loads(args.parameters.read_text())
    names = (args.corpus / "manifest.txt").read_text().splitlines()
    if len(names) != 1000 or len(set(names)) != 1000:
        raise ValueError("expected the frozen 1000-input corpus")
    plan = {
        "schema": 1,
        "experiment": "soi-mechanisms-1000-20261102",
        "inputs": 1000,
        "repeats": 3,
        "seed": 20261102,
        "timeout_seconds": 15,
        "model_timeout_seconds": 90,
        "common_args": ["--quiet", "--stats", "--gaussian-elimination"],
        "solver_sha256": args.candidate_sha256,
        "solver_source": args.candidate_source,
        "binaries": {
            "candidate": {
                "sha256": args.candidate_sha256,
                "source": args.candidate_source,
            },
            "original": {
                "sha256": "710929c58c18970cf4fccf7eb479fb95bb1410e6b46559433e4af80b55a9eee8",
                "source": "d4944ebbe6cf202c0485e783655209ba204df71c",
            },
        },
        "manifest_sha256": digest(args.corpus / "manifest.txt"),
        "expected_statuses_sha256": digest(args.corpus / "expected-statuses.json"),
        "parameters_sha256": digest(args.parameters),
        "selected_files": names,
        "baseline": "original-soi-g1-f0",
        "configurations": configurations(parameters),
        "aliases": {"no-growing": "linear", "final-recursive": "linear"},
        "pairing": "All binaries/configurations/repetitions for one input execute in the same SLURM task on the same CPU. Different inputs may use different compute hosts; host/CPU/affinity recorded per shard. Process clocks disabled; diagnostics and SAT models are separate phases.",
    }
    write_json(args.out, plan)


if __name__ == "__main__":
    main()
