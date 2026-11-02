from __future__ import annotations

import csv
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[2] / "scripts"
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location(
    "summarize_soi_improvements", SCRIPTS / "summarize_soi_improvements.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PairedSummaryTests(unittest.TestCase):
    def test_intermittent_timeout_loses_a_stable_solve(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plan = {
                "seed": 1,
                "inputs": 1,
                "repeats": 3,
                "selected_files": ["case.smt2"],
                "timeout_seconds": 15,
                "solver_sha256": "binary",
                "configurations": [{"id": "baseline"}, {"id": "candidate"}],
            }
            (root / "plan.json").write_text(json.dumps(plan))
            shard = root / "timing/shard-0"
            shard.mkdir(parents=True)
            meta = {
                "complete": True,
                "shards": 1,
                "shard": 0,
                "plan_sha256": module.digest(root / "plan.json"),
                "solver": {"sha256": "binary"},
            }
            (shard / "metadata.json").write_text(json.dumps(meta))
            rows = []
            for name, repeat, config in module.schedule(plan, ["case.smt2"], 0, 1):
                timeout = config["id"] == "candidate" and repeat == 1
                rows.append(
                    {
                        "file": name,
                        "repeat": repeat,
                        "configuration": config["id"],
                        "expected": "sat",
                        "analysis": {"coverage": "uninstrumented"},
                        "run": {
                            "status": "timeout" if timeout else "ok",
                            "answer": "sat",
                            "timeout": "true" if timeout else "false",
                            "exit_code": 0,
                            "seconds": 15 if timeout else 1,
                            "stats": {},
                        },
                    }
                )
            (shard / "rows.jsonl").write_text(
                "".join(json.dumps(r) + "\n" for r in rows)
            )
            historical = root / "historical.csv"
            with historical.open("w", newline="") as stream:
                writer = csv.DictWriter(
                    stream, fieldnames=["file", "configuration", "parent", "family"]
                )
                writer.writeheader()
                writer.writerow(
                    {
                        "file": "case.smt2",
                        "configuration": "soi-g1-f0",
                        "parent": "p",
                        "family": "f",
                    }
                )
            report = module.summarize([root], historical, ["baseline"])
            self.assertEqual(
                report["comparisons"]["candidate-vs-baseline"]["stable_solve_losses"],
                ["case.smt2"],
            )
            self.assertGreater(
                report["comparisons"]["candidate-vs-baseline"]["par2_change"][
                    "mean_per_input_seconds"
                ],
                0,
            )
            meta["complete"] = False
            (shard / "metadata.json").write_text(json.dumps(meta))
            with self.assertRaisesRegex(ValueError, "incomplete"):
                module.load_run(root)
