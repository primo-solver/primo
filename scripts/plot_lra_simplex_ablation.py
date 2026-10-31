#!/usr/bin/env python3
"""Render qualified simplex results as standalone SVG/PDF (requires matplotlib)."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True, help="output stem")
    args = parser.parse_args()
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import LogNorm

    report = json.loads(args.report.read_text())
    if not report["status"].startswith("qualified-"):
        raise ValueError("plot requires qualified results")
    plt.rcParams.update({"font.size": 10, "svg.fonttype": "none"})
    rules = ["bland", "dantzig", "sparse", "soi"]
    settings = [(0, 0), (0, 1), (1, 0), (1, 1)]
    scores = [
        [
            report["per_configuration"][f"{r}-g{g}-f{f}"]["mean_repeat_par2_seconds"]
            for g, f in settings
        ]
        for r in rules
    ]
    low, high = min(min(row) for row in scores), max(max(row) for row in scores)
    fig, (left, right) = plt.subplots(
        1, 2, figsize=(13, 7), gridspec_kw={"width_ratios": [1.1, 1]}
    )
    im = left.imshow(
        scores,
        cmap="cividis",
        norm=LogNorm(vmin=low, vmax=max(high, low * 1.001)),
        aspect="auto",
    )
    left.set_xticks(
        range(4), ["G off\nF off", "G off\nF on", "G on\nF off", "G on\nF on"]
    )
    left.set_yticks(range(4), ["Bland", "Dantzig", "Sparse", "SOI"])
    left.set_title("Whole-process PAR2 and stable solves")
    left.set_xlabel("G: Gaussian elimination; F: fixed-variable elimination")
    for i, rule in enumerate(rules):
        for j, (g, f) in enumerate(settings):
            info = report["per_configuration"][f"{rule}-g{g}-f{f}"]
            color = "white" if im.norm(scores[i][j]) < 0.5 else "black"
            left.text(
                j,
                i,
                f"{scores[i][j]:.1f} s\n{info['solved_all_repeats']}/{info['inputs']} solved",
                ha="center",
                va="center",
                color=color,
            )
    fig.colorbar(
        im,
        ax=left,
        orientation="horizontal",
        pad=0.15,
        label="Mean-repeat PAR2 sum (seconds; lower is better)",
    )
    labels = []
    for i, (rule, g, f) in enumerate((r, g, f) for r in rules for g, f in settings):
        key = f"{rule}-g{g}-f{f}"
        value = report["baseline_comparisons"][key]["check_ns"]
        labels.append(f"{rule}, G{g} F{f}  (n={value['pairs']})")
        point = value["geomean"]
        if point is not None:
            lo, hi = value["ci95"]
            right.hlines(i, lo, hi, color="#32678b", linewidth=1.5)
            right.plot([lo, hi], [i, i], "|", color="#32678b", markersize=6)
            right.plot(point, i, "o", color="#32678b", markersize=4)
    right.set_yticks(range(16), labels)
    right.invert_yaxis()
    right.set_xscale("log")
    right.axvline(1, color="#777777", linestyle="--", linewidth=1)
    right.grid(axis="x", alpha=0.2)
    right.set_title("Paired check speedup vs sparse G1 F0")
    right.set_xlabel(
        "Geometric mean speedup (higher is better)\n95% counting-parent bootstrap interval"
    )
    fig.suptitle(
        "Primo simplex: 999 labelled conjunctions, three repetitions per configuration",
        y=1.01,
    )
    fig.tight_layout()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    for extension in ("svg", "pdf"):
        fig.savefig(args.out.with_suffix("." + extension), bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    main()
