from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from summarize_soi_qualification import digest, model_coverage


class ModelJoinTests(unittest.TestCase):
    def test_replay_must_cover_exact_sat_keys_and_binary(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plan = root / "plan.json"
            plan.write_text(json.dumps({"binaries": {"candidate": {"sha256": "abc"}}}))
            parent = root / "timing/shard-0"
            parent.mkdir(parents=True)
            (parent / "metadata.json").write_text(
                json.dumps({"shards": 1, "complete": True, "plan_sha256": digest(plan)})
            )
            (parent / "rows.jsonl").write_text(
                json.dumps(
                    {
                        "key": "sat-key",
                        "configuration": "final",
                        "binary": "candidate",
                        "run": {
                            "answer": "sat",
                            "status": "ok",
                            "exit_code": 0,
                            "timeout": "false",
                        },
                    }
                )
                + "\n"
            )
            selection = root / "model-selections/candidate"
            chosen = selection / "timing/shard-0"
            chosen.mkdir(parents=True)
            (chosen / "metadata.json").write_text(
                json.dumps(
                    {
                        "derived_selection": True,
                        "parent_metadata_sha256": digest(parent / "metadata.json"),
                    }
                )
            )
            models = selection / "models/shard-0"
            models.mkdir(parents=True)
            metadata = {
                "complete": True,
                "solver": {"sha256": "abc"},
                "passed": 1,
                "dolmen": {"sha256": "validator"},
            }
            (models / "metadata.json").write_text(json.dumps(metadata))
            joins = [{"key": "sat-key", "outcome": "pass", "validator": "dolmen"}]
            (models / "joins.json").write_text(json.dumps(joins))
            self.assertEqual(model_coverage(root)["passed"], {"final": 1})
            joins[0]["key"] = "another-input"
            (models / "joins.json").write_text(json.dumps(joins))
            with self.assertRaisesRegex(ValueError, "model joins"):
                model_coverage(root)
            joins[0]["key"] = "sat-key"
            (models / "joins.json").write_text(json.dumps(joins))
            metadata["solver"]["sha256"] = "other-binary"
            (models / "metadata.json").write_text(json.dumps(metadata))
            with self.assertRaisesRegex(ValueError, "identity/count"):
                model_coverage(root)
