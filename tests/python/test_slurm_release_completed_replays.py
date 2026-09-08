from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[2] / "scripts/slurm_release_completed_replays.py"
SPEC = importlib.util.spec_from_file_location("release_replays", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ReplayDependencyTests(unittest.TestCase):
    def test_only_successful_array_tasks_are_complete(self) -> None:
        records = ("10_0|COMPLETED|0:0\n10_1|RUNNING|0:0\n10_2|COMPLETED|1:0\n"
                   "10_3|FAILED|0:0\n10_4.batch|COMPLETED|0:0\n11_5|COMPLETED|0:0\n")
        self.assertEqual(MODULE.completed_indices(records, "10"), {0})

    def test_plan_requires_all_four_exact_timing_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            paths = []
            for rnd in (0, 1):
                for arm in ("baseline", "candidate"):
                    shard = root / f"round-{rnd}" / arm / "shard-0"
                    shard.mkdir(parents=True)
                    path = shard / "provenance.json"
                    paths.append(path)
                    path.write_text(json.dumps({
                        "status": "complete", "inputs_unchanged": True,
                        "run_id": f"component-ab-10-{rnd}-{arm}-0",
                        "shard_index": 0, "shards": 2, "solver": {"sha256": arm},
                        "inputs": {"case.smt2": "hash"}, "timeout": 60,
                        "solver_args": ["--quiet", "--stats"],
                    }))
                    (shard / "answers.csv").write_text(
                        "file,status,wrong\ncase.smt2,ok,false\n"
                    )
            calls = []

            def query(command: list[str]) -> str:
                calls.append(command)
                if command[0] == "sacct":
                    return "10_0|COMPLETED|0:0\n10_1|RUNNING|0:0\n"
                return "20_0\n20_1\n"

            def plan() -> list[str]:
                return MODULE.plan_releases(root, "10", ["20"],
                                            {"baseline": "baseline", "candidate": "candidate"},
                                            60, ["--quiet", "--stats"], query)

            self.assertEqual(plan(), ["20_0"])
            self.assertTrue(all(call[0] in {"sacct", "squeue"} for call in calls))
            target = paths[-1]
            original = target.read_text()
            for field, bad in (("status", "running"), ("inputs_unchanged", False),
                               ("run_id", "unrelated-run"), ("solver", {"sha256": "other"}),
                               ("inputs", {"case.smt2": "other"}), ("shard_index", 1),
                               ("timeout", 30), ("solver_args", ["--other-profile"])):
                with self.subTest(field=field):
                    metadata = json.loads(original)
                    metadata[field] = bad
                    target.write_text(json.dumps(metadata))
                    with self.assertRaises(ValueError):
                        plan()
                    target.write_text(original)
            (target.parent / "answers.csv").write_text("file,status,wrong\n")
            with self.assertRaisesRegex(ValueError, "timing rows"):
                plan()


if __name__ == "__main__":
    unittest.main()
