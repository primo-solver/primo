#!/usr/bin/env python3
"""Queue audited timing and model stages after an existing full-corpus array."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--full-job", required=True, type=int)
    parser.add_argument("--ssh-config", required=True)
    parser.add_argument("--source-snapshot", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    # Reserve the record before submitting anything; never silently queue duplicates.
    with args.out.open("x") as stream:
        json.dump({"full30": str(args.full_job), "status": "submitting", "commands": []}, stream)
    record = json.loads(args.out.read_text())
    work = "/home/janotmik/primo-lra-migration-ablation-20261010"
    campaign = f"{work}/results/lra-migration-ablation-20261010"
    common = {
        "PRIMO_WORKDIR": work, "PRIMO_EXPECTED_GIT": "4200525",
        "PRIMO_WORKFLOW": f"{campaign}/workflow", "PRIMO_SHARDS": "64",
    }
    ssh = ["ssh", "-F", args.ssh_config, "primo-cluster"]

    def submit(key: str, dependency: str, dataset: str, phase: str,
               expected: int, timeout: int, require_models: bool = False) -> str:
        preflight = [
            sys.executable, "scripts/slurm_prepare_benchmark_workspace.py",
            "--local-dir", args.source_snapshot, "--ssh-config", args.ssh_config,
            "--remote", "primo-cluster", "--remote-dir", work,
            "--cmake", "/home/janotmik/primo-cadical-profile/.venv-build/bin/cmake",
            "--reuse-dependencies", "--check-only", "--no-sync",
            "--cxx-header-manifest", "/home/janotmik/primo-symmetry-headers.txt",
            "--require-tool", "contrib/local/bin/z3",
            "--require-tool", "contrib/local/bin/dolmen",
        ]
        with args.out.with_name(f"preflight-{key}.log").open("w") as log:
            subprocess.run(preflight, check=True, stdout=log, stderr=subprocess.STDOUT)
        env = dict(common, PRIMO_CAMPAIGN_OUT=f"{campaign}/{dataset}",
                   PRIMO_EXPECTED_INPUTS=str(expected), PRIMO_TIMEOUT=str(timeout))
        command = ["sbatch", "--parsable", "--kill-on-invalid-dep=yes",
                   f"--dependency=afterok:{dependency}", f"--job-name=lra-ablate-{key}"]
        if phase == "audit":
            env.update(PRIMO_AUDIT_NAME="qualified-audit" if require_models else "timing-audit",
                       PRIMO_REQUIRE_MODELS=str(require_models).lower())
            wrapper = "audit.sbatch"
        else:
            env["PRIMO_PHASE"] = phase
            command.append("--array=0-255%48")
            wrapper = "guarded.sbatch"
            if phase == "benchmark":
                env.update(
                    PRIMO_CAMPAIGN_ID="lra-migration-ablation-selection1200-20261010",
                    PRIMO_CORPUS_ROOT="tmp/QF_LRA",
                    PRIMO_MANIFEST=f"{work}/benchmarks/qf-lra-smtcomp2026-selection.txt",
                    PRIMO_MANIFEST_SHA256="0ae74669e5ee69c8894cee44e415ce644047be41942ec23d1de8eef4afecb18c",
                )
            else:
                env["PRIMO_MODEL_TIMEOUT"] = "3600" if dataset == "selection1200" else "120"
                command.append("--time=12:00:00" if dataset == "selection1200" else "--time=04:00:00")
        command += ["--export=ALL," + ",".join(f"{k}={v}" for k, v in env.items()),
                    f"{campaign}/workflow/{wrapper}"]
        remote = f"cd {shlex.quote(work)} && {shlex.join(command)}"
        job = subprocess.check_output([*ssh, remote], text=True).strip()
        if not job.isdigit():
            raise ValueError(f"unexpected sbatch reply: {job}")
        record[key] = job
        record["commands"].append({"key": key, "preflight": preflight, "ssh": [*ssh, remote]})
        args.out.write_text(json.dumps(record, indent=2) + "\n")
        print(f"{key}: {job}", flush=True)
        return job

    full_audit = submit("full30-audit", str(args.full_job), "full30", "audit", 1753, 30)
    selected = submit("selection1200", full_audit, "selection1200", "benchmark", 519, 1200)
    selected_audit = submit("selection1200-audit", selected, "selection1200", "audit", 519, 1200)
    # No model replay competes with either timing dataset for cluster resources.
    full_models = submit("full30-models", selected_audit, "full30", "replay", 1753, 30)
    selected_models = submit("selection1200-models", selected_audit, "selection1200", "replay", 519, 1200)
    submit("full30-qualified", full_models, "full30", "audit", 1753, 30, True)
    submit("selection1200-qualified", selected_models, "selection1200", "audit", 519, 1200, True)
    record["status"] = "queued"
    args.out.write_text(json.dumps(record, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
