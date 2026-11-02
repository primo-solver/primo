from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from argparse import Namespace
from pathlib import Path
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[2] / "scripts"
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location(
    "run_soi_improvements", SCRIPTS / "run_soi_improvements.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MeasurementContractTests(unittest.TestCase):
    def test_speed_runs_do_not_enable_clocks_and_failures_remain_durable(self):
        for phase, failed in (("timing", False), ("profile", False), ("timing", True)):
            with (
                self.subTest(phase=phase, failed=failed),
                tempfile.TemporaryDirectory() as directory,
            ):
                root = Path(directory)
                corpus = root / "corpus"
                corpus.mkdir()
                problem = corpus / "problem"
                problem.write_text("(check-sat)\n")
                name = module.digest(problem) + ".smt2"
                problem.rename(corpus / name)
                (corpus / "expected-statuses.json").write_text(
                    json.dumps({name: "unsat"})
                )
                solver = root / "solver"
                solver.write_text("frozen binary placeholder")
                args = Namespace(
                    phase=phase,
                    out=root / "out",
                    shard=0,
                    shards=1,
                    corpus=corpus,
                    solver=solver,
                )
                plan = {
                    "solver_sha256": module.digest(solver),
                    "seed": 1,
                    "repeats": 1,
                    "common_args": ["--stats"],
                    "timeout_seconds": 1,
                    "configurations": [{"id": "test", "args": []}],
                }
                commands = []

                def run(command, _path, _timeout, commands=commands, failed=failed):
                    commands.append(command)
                    if "--lra-call-profile" in command:
                        Path(command[-1]).write_text("diagnostic trace")
                    return {
                        "status": "error" if failed else "ok",
                        "answer": "unsat",
                        "exit_code": 1 if failed else 0,
                        "timeout": "false",
                        "stats": {},
                        "stdout": "unsat\n",
                        "stderr": "",
                    }

                metadata = {
                    "solver": {"sha256": module.digest(solver)},
                    "complete": False,
                    "workflow_sha256": {},
                }
                analysis = {"coverage": "no-checks", "calls": 0, "total_pivots": 0}
                with (
                    patch.object(module.base, "metadata", return_value=metadata),
                    patch.object(module, "run_solver", side_effect=run),
                    patch.object(module, "analyze", return_value=analysis),
                ):
                    if failed:
                        with self.assertRaises(ValueError):
                            module.measure(args, plan, [name])
                    else:
                        module.measure(args, plan, [name])
                self.assertEqual(
                    "--lra-call-profile" in commands[0], phase == "profile"
                )
                output = root / "out" / phase / "shard-0"
                self.assertEqual(
                    len((output / "rows.jsonl").read_text().splitlines()), 1
                )
                self.assertEqual(
                    json.loads((output / "metadata.json").read_text())["complete"],
                    not failed,
                )


class MultiBinaryReplayTests(unittest.TestCase):
    def test_model_selection_preserves_each_rows_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = {key: root / key for key in ("candidate", "original")}
            for key, path in paths.items():
                path.write_text(key)
            registry = {
                key: {"sha256": module.digest(path), "source": key}
                for key, path in paths.items()
            }
            plan = {
                "binaries": registry,
                "configurations": [
                    {"id": "new", "binary": "candidate", "args": []},
                    {"id": "old", "binary": "original", "args": []},
                ],
            }
            plan_path = root / "plan.json"
            plan_path.write_text(json.dumps(plan))
            mapping = root / "binaries.json"
            mapping.write_text(
                json.dumps({key: str(path) for key, path in paths.items()})
            )
            parent = root / "timing/shard-7"
            parent.mkdir(parents=True)
            (parent / "metadata.json").write_text(
                json.dumps({"complete": True, "plan_sha256": module.digest(plan_path)})
            )
            rows = [
                {
                    "configuration": cfg["id"],
                    "binary": cfg["binary"],
                    "command": [str(paths[cfg["binary"]])],
                    "key": cfg["id"],
                }
                for cfg in plan["configurations"]
            ]
            (parent / "rows.jsonl").write_text(
                "".join(json.dumps(row) + "\n" for row in rows)
            )
            args = Namespace(
                out=root,
                plan=plan_path,
                binary_map=mapping,
                solver=paths["candidate"],
                shard=7,
            )
            seen = []

            def models(child, child_plan):
                selected = module.base.read_rows(
                    child.out / "timing/shard-7/rows.jsonl"
                )
                self.assertEqual(len(selected), 1)
                self.assertEqual(selected[0]["command"][0], str(child.solver))
                self.assertEqual(child.plan.name, "plan-7.json")
                self.assertEqual(
                    module.digest(child.solver), child_plan["solver_sha256"]
                )
                seen.append(selected[0]["key"])

            with (
                patch.object(
                    module.base, "metadata", return_value={"workflow_sha256": {}}
                ),
                patch.object(module.base, "models", side_effect=models),
            ):
                module.replay_models(args, plan)
            self.assertEqual(set(seen), {"new", "old"})
            self.assertEqual(module.base.read_rows(parent / "rows.jsonl"), rows)
