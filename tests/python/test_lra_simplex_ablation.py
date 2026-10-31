import json
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

with patch.object(
    sys, "path", [str(Path(__file__).resolve().parents[2] / "scripts"), *sys.path]
):
    from run_lra_simplex_ablation import check_run, decisive, model_groups, schedule


class SimplexAblationTests(unittest.TestCase):
    def setUp(self):
        self.plan = json.loads(
            (
                Path(__file__).resolve().parents[2]
                / "benchmarks/qf-lra-simplex-ablation/plan.json"
            ).read_text()
        )

    def test_matrix_covers_each_factor_combination_once(self):
        factors = {
            (c["pivot_rule"], c["gaussian_elimination"], c["fixed_elimination"])
            for c in self.plan["configurations"]
        }
        self.assertEqual(len(factors), 16)
        self.assertEqual(len(self.plan["configurations"]), 16)
        baseline = next(
            c for c in self.plan["configurations"] if c["id"] == self.plan["baseline"]
        )
        self.assertEqual(
            (
                baseline["pivot_rule"],
                baseline["gaussian_elimination"],
                baseline["fixed_elimination"],
            ),
            ("sparse", True, False),
        )

    def test_schedule_pairs_inputs_without_losing_repeats_or_configs(self):
        names = [f"cubes/{index}.smt2" for index in range(13)]
        parts = [list(schedule(self.plan, names, shard, 4)) for shard in range(4)]
        rows = [r for part in parts for r in part]
        self.assertEqual(len(rows), 13 * 16 * 3)
        self.assertEqual(
            len({(name, rep, c["id"]) for name, rep, c in rows}), len(rows)
        )
        for name in names:
            self.assertEqual(sum(any(r[0] == name for r in part) for part in parts), 1)
        self.assertEqual(
            parts[0], list(schedule(self.plan, list(reversed(names)), 0, 4))
        )
        block_orders = [
            tuple(c["id"] for n, r, c in rows if n == names[0] and r == rep)
            for rep in range(3)
        ]
        self.assertGreater(len(set(block_orders)), 1)

    def test_timeout_and_post_answer_crash_are_never_successes(self):
        good = dict(status="ok", answer="sat", exit_code=0, timeout="false")
        check_run(good, "sat")
        with self.assertRaises(ValueError):
            check_run(good, "unsat")
        for run in [
            dict(good, status="timeout", timeout="true"),
            dict(good, status="error", exit_code=-11),
        ]:
            self.assertIsNone(decisive(run))
            self.assertEqual(
                model_groups([dict(run=run, configuration="x", repeat=0)]), {}
            )
        with self.assertRaises(ValueError):
            check_run(dict(good, status="error", exit_code=-11), "sat")

    def test_complete_model_join_works_with_python39_zip(self):
        import csv
        import tempfile
        from types import SimpleNamespace

        with patch.object(
            sys,
            "path",
            [str(Path(__file__).resolve().parents[2] / "scripts"), *sys.path],
        ):
            import run_lra_simplex_ablation as runner
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            corpus = root / "corpus"
            (corpus / "cubes").mkdir(parents=True)
            source = corpus / "cubes/a.smt2"
            source.write_text("(set-logic QF_LRA)\n(assert true)\n(check-sat)\n")
            (corpus / "manifest.txt").write_text("cubes/a.smt2\n")
            (corpus / "expected-statuses.json").write_text('{"cubes/a.smt2":"sat"}\n')
            solver = root / "solver"
            solver.write_text("solver identity")
            dolmen = root / "dolmen"
            dolmen.write_text("validator identity")
            plan = dict(
                seed=1,
                timeout_seconds=15,
                model_timeout_seconds=90,
                common_args=["--quiet"],
                configurations=[dict(id="x", args=["--lra-pivoting-rule", "bland"])],
            )
            planpath = root / "plan.json"
            planpath.write_text(json.dumps(plan))
            args = SimpleNamespace(
                phase="models",
                plan=planpath,
                corpus=corpus,
                solver=solver,
                dolmen=dolmen,
                out=root / "results",
                shard=0,
                shards=1,
            )
            timing = args.out / "timing/shard-0"
            timing.mkdir(parents=True)
            row = dict(
                key="a-r0-x",
                file="cubes/a.smt2",
                repeat=0,
                configuration="x",
                input_sha256=runner.digest(source),
                run=dict(status="ok", answer="sat", exit_code=0, timeout="false"),
            )
            (timing / "rows.jsonl").write_text(json.dumps(row) + "\n")

            def fake_identity(path):
                return dict(sha256=runner.digest(path), version="fixture")

            with patch.object(runner, "identity", fake_identity):
                before = runner.metadata(args, plan)
                before["complete"] = True
                (timing / "metadata.json").write_text(json.dumps(before))

                def fake_validator(command, **kwargs):
                    target = Path(command[command.index("--out") + 1])
                    target.mkdir()
                    (target / "00000-a.smt2.smt2").write_text(
                        runner.model_problem_text(source.read_text())
                    )
                    with (target / "summary.csv").open("w") as stream:
                        writer = csv.DictWriter(
                            stream,
                            fieldnames=[
                                "file",
                                "answer",
                                "outcome",
                                "validator",
                                "solver_exit",
                                "dolmen_exit",
                            ],
                        )
                        writer.writeheader()
                        writer.writerow(
                            dict(
                                file=str(source),
                                answer="sat",
                                outcome="pass",
                                validator="dolmen",
                                solver_exit=0,
                                dolmen_exit=0,
                            )
                        )
                    return SimpleNamespace(returncode=0)

                # Python 3.9's zip rejects keyword arguments. Exercise the whole
                # model-stage join without launching either external tool.
                with (
                    patch.object(
                        runner, "zip", lambda *items: zip(*items), create=True
                    ),
                    patch.object(runner.subprocess, "run", fake_validator),
                ):
                    runner.models(args, plan)
            meta = json.loads((args.out / "models/shard-0/metadata.json").read_text())
            self.assertTrue(meta["complete"])
            self.assertEqual(meta["passed"], 1)
            self.assertEqual(meta["timing_workflow_sha256"], before["workflow_sha256"])

    def test_models_preserve_repetition_and_profile_identity(self):
        run = dict(status="ok", answer="sat", exit_code=0, timeout="false")
        rows = [
            dict(run=run, configuration=c, repeat=r)
            for c in ["a", "b"]
            for r in range(3)
        ]
        self.assertEqual(len(model_groups(rows)), 6)


# These tests use synthetic measurements only; no solver process is launched.
class SimplexSummaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with patch.object(
            sys,
            "path",
            [str(Path(__file__).resolve().parents[2] / "scripts"), *sys.path],
        ):
            import summarize_lra_simplex_ablation as summary
        cls.summary = summary

    def origin(self):
        return dict(
            expected="sat",
            counting_parent="family/parent.smt2",
            fresh_coverage="complete",
            fresh_completed_checks=1,
            fresh_completed_pivots=129,
        )

    def row(self, status="ok", calls=1):
        return dict(
            file="cubes/a.smt2",
            configuration="a",
            run=dict(
                status=status,
                answer="sat",
                exit_code=0,
                timeout="false" if status == "ok" else "true",
                seconds="1",
                stats={},
            ),
            analysis=dict(
                coverage="complete" if status == "ok" else "prefix",
                calls=calls,
                total_pivots=12 if calls else 0,
                total_check_ns=100 if calls else 0,
                checkers=[],
            ),
        )

    def test_timeout_prefix_is_not_complete_work_and_penalty_keeps_failure(self):
        value = self.summary.compact(
            [self.row(), self.row(), self.row("timeout")], self.origin(), 15
        )
        self.assertEqual(value["successful_repeats"], 2)
        self.assertIsNone(value["pivots"])
        self.assertIsNone(value["check_ns"])
        self.assertIsNone(value["process_seconds"])
        self.assertEqual(value["median_par2_seconds"], 1)
        self.assertAlmostEqual(value["mean_par2_seconds"], 32 / 3)
        self.assertEqual(value["outcomes"], {"sat": 2, "timeout": 1})
        self.assertEqual(value["stratum"], "128-1023")

    def test_no_checker_success_is_separate_from_missing_check_time(self):
        value = self.summary.compact([self.row(calls=0)] * 3, self.origin(), 15)
        self.assertEqual(value["pivots"], 0)
        self.assertIsNone(value["check_ns"])
        self.assertEqual(self.summary.summarize_group([value])["no_check_triplets"], 1)

    def test_parent_cluster_bootstrap_respects_equal_ratio(self):
        result = self.summary.clustered_ratio([("p", 2), ("p", 2), ("q", 2)], 19, 100)
        self.assertEqual(result["pairs"], 3)
        self.assertEqual(result["parents"], 2)
        self.assertAlmostEqual(result["geomean"], 2)
        self.assertTrue(all(abs(x - 2) < 1e-10 for x in result["ci95"]))

    def test_parent_cluster_difference_retains_failed_process_penalties(self):
        result = self.summary.clustered_difference(
            [("p", -2), ("p", -2), ("q", -2)], 19, 100
        )
        self.assertEqual(result["inputs"], 3)
        self.assertEqual(result["parents"], 2)
        self.assertEqual(result["mean_per_input_seconds"], -2)
        self.assertEqual(result["ci95"], [-2, -2])

    def test_unknown_answer_has_own_outcome(self):
        row = self.row()
        row["run"]["answer"] = "unknown"
        self.assertEqual(self.summary.outcome(row["run"]), "unknown")

    def test_audit_rejects_changed_configuration_and_missing_model_join(self):
        import tempfile
        from types import SimpleNamespace

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = SimpleNamespace(
                results=root, shards=1, plan=root / "plan.json", verify_artifacts=False
            )
            plan = dict(
                seed=1,
                repeats=3,
                solver_sha256="binary",
                manifest_sha256="manifest",
                expected_statuses_sha256="labels",
                common_args=["--quiet"],
                configurations=[dict(id="x", args=["--lra-pivoting-rule", "bland"])],
            )
            args.plan.write_text(json.dumps(plan))
            native = dict(sha256="binary", version="git: test")
            meta = dict(
                complete=True,
                shard=0,
                shards=1,
                rows=3,
                solver=native,
                plan_sha256=self.summary.digest(args.plan),
                manifest_sha256="manifest",
                expected_statuses_sha256="labels",
                workflow_sha256={"test": "hash"},
                started="2026-01-01T00:00:00+00:00",
                finished="2026-01-01T00:00:01+00:00",
            )
            timing = root / "timing/shard-0"
            models = root / "models/shard-0"
            timing.mkdir(parents=True)
            models.mkdir(parents=True)
            (timing / "metadata.json").write_text(json.dumps(meta))
            mm = dict(
                meta,
                passed=3,
                started="2026-01-01T00:00:02+00:00",
                dolmen={"sha256": "validator"},
            )
            (models / "metadata.json").write_text(json.dumps(mm))
            rows = []
            joins = []
            for rep in range(3):
                row = self.row()
                row.update(
                    key=f"a-r{rep}-x",
                    repeat=rep,
                    configuration="x",
                    expected="sat",
                    input_sha256="a",
                    command=[
                        "solver",
                        "--quiet",
                        "--lra-pivoting-rule",
                        "bland",
                        "--lra-call-profile",
                        "trace",
                        "/corpus/cubes/a.smt2",
                    ],
                )
                rows.append(row)
                joins.append(dict(key=row["key"], outcome="pass", validator="dolmen"))

            def save():
                (timing / "rows.jsonl").write_text(
                    "".join(json.dumps(r) + "\n" for r in rows)
                )
                (models / "joins.json").write_text(json.dumps(joins))

            save()
            origins = {"cubes/a.smt2": dict(sha256="a", expected="sat")}
            with patch.object(
                self.summary, "zip", lambda *items: zip(*items), create=True
            ):
                self.summary.load_audit(args, plan, origins)
            rows[0]["command"][3] = "sparse"
            save()
            with self.assertRaisesRegex(ValueError, "arguments differ"):
                self.summary.load_audit(args, plan, origins)
            rows[0]["command"][3] = "bland"
            joins.pop()
            save()
            with self.assertRaisesRegex(ValueError, "model joins"):
                self.summary.load_audit(args, plan, origins)


if __name__ == "__main__":
    unittest.main()
