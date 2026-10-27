#!/usr/bin/env python3
"""Export matched, audited timing snapshots without advancing campaign barriers."""

from __future__ import annotations

import argparse
import csv
import html
import itertools
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

from campaign import allocation, check_rows, decisive, digest, load, write
from report import metrics, timing_shard

DISPLAY = {'primo': 'Primo default', 'primo-phase-mono': 'Primo phase+mono',
           'z3': 'Z3', 'cvc5': 'cvc5', 'yices': 'Yices', 'opensmt': 'OpenSMT'}
CSV_FIELDS = ['file', 'family', 'status', 'answer', 'seconds', 'exit_code', 'timeout',
              'wrong', 'stderr_head', 'stdout_log', 'stderr_log']


def shared_shards(campaign: Path, runs: list[dict]) -> tuple[list[int], list[str]]:
    """Freeze completion intersection; never select by solver answer or runtime."""
    complete, unreadable = [], []
    for run in runs:
        indices = set()
        root = campaign / 'timing' / run['logic'] / run['label']
        for index in range(run['shards']):
            path = root / f'shard-{index}' / 'provenance.json'
            try:
                value = load(path)
            except FileNotFoundError:
                continue
            except (OSError, ValueError) as error:
                # A live writer may be updating this record. Selected complete
                # records receive the strict terminal audit below.
                unreadable.append(f'{path}: {error}')
                continue
            if value['status'] == 'complete':
                indices.add(index)
        complete.append(indices)
    return sorted(set.intersection(*complete)), unreadable


def best_profile(results: dict) -> str:
    return min((label for label in results if label.startswith('primo')),
               key=lambda label: (-results[label]['solved'], results[label]['par2_sum'], label))


def paired_rows(campaign: Path, plan: dict, runs: list[dict], indices: list[int],
                expected: dict) -> tuple[dict, dict]:
    merged, certificates = {}, {}
    for run in runs:
        rows = {}
        for index in indices:
            root, _, values = timing_shard(campaign, plan, run, index, expected)
            for row in values:
                if row['file'] in rows:
                    raise ValueError('duplicate input in snapshot')
                rows[row['file']] = row
            certificates[f"{run['logic']}/{run['label']}/{index}"] = {
                name: digest(root / name) for name in
                ('answers.csv', 'provenance.json', 'execution-timing.json')}
        merged[run['label']] = rows
    cohorts = [set(rows) for rows in merged.values()]
    if not cohorts or not cohorts[0] or any(names != cohorts[0] for names in cohorts):
        raise ValueError('snapshot series do not cover the same nonempty input set')
    for first, second in itertools.combinations(merged.values(), 2):
        check_rows(list(first.values()), list(second.values()), allow_execution_errors=True)
    return merged, certificates


def gallery(snapshot: dict) -> str:
    esc = html.escape
    rows, figures = [], []
    for logic, result in snapshot['divisions'].items():
        name = logic.lower() + '-1200.svg'
        coverage = f"{result['inputs']:,} / {result['full_corpus_inputs']:,}"
        scope = 'Full timing coverage' if result['full_timing_coverage'] else 'Partial matched cohort'
        chosen = DISPLAY[result['best_static_profile']]
        rows.append(f'<tr><td><a href="#{logic}">{logic}</a></td><td>{coverage}</td>'
                    f'<td>{scope}</td><td>{esc(chosen)}</td></tr>')
        stats = ''.join(
            '<tr>' + ''.join(f'<td>{esc(str(value))}</td>' for value in
                            (DISPLAY[label], data['solved'], data['sat'], data['unsat'],
                             data['timeouts'], data['unknown'], data['memory_out'], data['errors'],
                             round(data['par2_sum']))) + '</tr>'
            for label, data in result['runs'].items())
        figures.append(f'''<section id="{logic}"><h2>{logic}</h2>
<p><span class="scope">{scope}</span> {coverage} inputs. Selected profile: {esc(chosen)}.</p>
<div class="chart"><a href="{name}"><img src="{name}" alt="{logic} cactus plot: {esc(scope)}"></a></div>
<p><a href="{name}" download>Download SVG</a></p>
<details><summary>All measured profiles and outcome counts</summary><div class="table">
<table><thead><tr><th>Solver</th><th>Solved</th><th>SAT</th><th>UNSAT</th><th>Timeout</th>
<th>Unknown</th><th>Explicit OOM</th><th>Other error</th><th>PAR2 sum (s)</th></tr></thead>
<tbody>{stats}</tbody></table></div></details></section>''')
    return f'''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Primo vs references: current 1200-second cactus plots</title>
<style>
:root{{--canvas:#f5f7fb;--surface:#ffffff;--text:#1f2937;--muted:#4b5563;--border:#cbd5e1;--accent:#0057b8}}
*{{box-sizing:border-box}}body{{margin:0;background:var(--canvas);color:var(--text);font:16px/1.5 system-ui,sans-serif}}
main{{max-width:1000px;margin:auto;padding:24px}}h1{{font-size:1.8rem;line-height:1.2}}h2{{margin-top:0}}
a{{color:var(--accent)}}a:focus-visible,summary:focus-visible{{outline:3px solid var(--accent);outline-offset:3px}}
section{{background:var(--surface);border:1px solid var(--border);border-radius:8px;padding:20px;margin:24px 0;scroll-margin-top:16px}}
p{{max-width:88ch}}.muted{{color:var(--muted)}}.scope{{font-weight:650}}.chart,.table{{overflow-x:auto}}
.chart img{{display:block;width:900px;max-width:none;height:auto}}table{{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}}
th,td{{padding:9px;text-align:left;border-bottom:1px solid var(--border);white-space:nowrap}}th{{font-weight:650}}
summary{{cursor:pointer}}@media(max-width:600px){{main{{padding:12px}}section{{padding:12px}}h1{{font-size:1.5rem}}}}
</style><main><h1>Primo vs reference solvers</h1>
<p><strong>Current timing snapshot · 1200 seconds per input · 8 GiB address-space cap.</strong></p>
<p>Diagnostic plots; independent SAT-model validation is pending. Partial divisions contain only
shards completed by every compared profile. Completion-selected subsets can change the final ranking.
No easy-input filter or per-instance Primo portfolio is used. Unknown, timeout, memory-out and error rows count as unsolved.</p>
<p class="muted">{esc(snapshot['started_at_utc'])} to {esc(snapshot['finished_at_utc'])}. Native source 4419dc9.
One physical CPU per measured solver. Quantified UF printed-model validation is unsupported;
OpenSMT is unavailable for UF. Scroll charts horizontally on small screens, or open their SVG links.</p>
<div class="table"><table><thead><tr><th>Division</th><th>Matched / full inputs</th><th>Coverage</th>
<th>Selected Primo</th></tr></thead><tbody>{''.join(rows)}</tbody></table></div>
<p><a href="summary.json">Machine-readable summary and provenance</a> · <a href="selection.json">Frozen shard selection</a></p>
{''.join(figures)}</main></html>'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('preview',))
    parser.add_argument('--workspace', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True, help='Existing campaign; read only.')
    parser.add_argument('--snapshot', type=Path, default=os.environ.get('PRIMO_PREVIEW_OUT'))
    parser.add_argument('--selection', type=Path, help='Replay a prior selection in a fresh output directory.')
    args = parser.parse_args()
    if args.snapshot is None:
        parser.error('--snapshot or PRIMO_PREVIEW_OUT is required')
    campaign, target = args.out.resolve(), args.snapshot.resolve()
    target.mkdir(parents=True, exist_ok=False)
    plan = load(campaign / 'plan.json')
    execution = allocation(plan['config'])
    if load(campaign / 'gate-audit.json')['status'] != 'complete':
        raise ValueError('correctness gate audit missing')
    if digest(Path(__file__).with_name('campaign.py')) != plan['driver_sha256']:
        raise ValueError('preview adapter differs from the frozen campaign worker')
    for folder in ('failures', 'task-failures'):
        if list((campaign / folder).glob('*.json')):
            raise ValueError('campaign has recorded failures; inspect before plotting')
    started = datetime.now(timezone.utc).isoformat()
    selected = load(args.selection) if args.selection else {
        'plan_sha256': digest(campaign / 'plan.json'), 'common_shards': {}, 'unreadable_live_provenance': []}
    if selected['plan_sha256'] != digest(campaign / 'plan.json'):
        raise ValueError('selection belongs to another campaign')
    grouped = {logic: [run for run in plan['runs'] if run['logic'] == logic]
               for logic in plan['config']['corpora']}
    if not args.selection:
        for logic, runs in grouped.items():
            indices, unreadable = shared_shards(campaign, runs)
            selected['common_shards'][logic] = indices
            selected['unreadable_live_provenance'].extend(unreadable)
    write(target / 'selection.json', selected)
    sys.path.insert(0, str(args.workspace / 'scripts'))
    from plot_cactus import order_series_by_solved, svg

    snapshot = {'status': 'diagnostic-timing-preview', 'started_at_utc': started,
                'native_source_git': plan['config']['native_source_git'], 'tools': plan['tools'],
                'timeout_seconds': plan['config']['timeout_seconds'], 'execution': execution,
                'model_validation': 'Pending; quantified printed models unsupported.',
                'selection_rule': 'One static native profile per matched cohort: most solved, then lowest PAR2, then name.',
                'script_sha256': digest(Path(__file__)), 'report_sha256': digest(Path(__file__).with_name('report.py')),
                'plotter_sha256': digest(args.workspace / 'scripts/plot_cactus.py'),
                'plan_sha256': selected['plan_sha256'], 'divisions': {}}
    for logic, runs in grouped.items():
        inventory_path = campaign / 'inventory' / f'{logic}.json'
        inventory = load(inventory_path)
        expected = {name: data['sha256'] for name, data in inventory['inputs'].items()}
        indices = selected['common_shards'][logic]
        if (not indices or len(set(indices)) != len(indices)
                or any(index < 0 or index >= runs[0]['shards'] for index in indices)):
            raise ValueError('invalid or empty common shard cohort')
        all_rows, certificates = paired_rows(campaign, plan, runs, indices, expected)
        reports = {label: metrics(list(rows.values()), plan['config']['timeout_seconds'])
                   for label, rows in all_rows.items()}
        best = best_profile(reports)
        names = set(next(iter(all_rows.values())))
        for run in runs:
            label = run['label']
            reports[label]['arguments'] = run['arguments']
            reports[label]['families'] = {
                family: metrics([r for r in all_rows[label].values() if r['family'] == family],
                                plan['config']['timeout_seconds'])
                for family in sorted({r['family'] for r in all_rows[label].values()})}
            directory = target / 'series' / logic / label
            directory.mkdir(parents=True)
            with (directory / 'shard-0.csv').open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS, extrasaction='ignore')
                writer.writeheader()
                writer.writerows(all_rows[label].values())
        write(target / 'inputs' / f'{logic}.json', {name: expected[name] for name in sorted(names)})
        full = names == set(expected)
        snapshot['divisions'][logic] = {
            'inputs': len(names), 'full_corpus_inputs': len(expected), 'full_timing_coverage': full,
            'common_shards': indices, 'inventory_sha256': digest(inventory_path),
            'runs': reports, 'best_static_profile': best, 'source_certificates': certificates}
        plotted_labels = [best, *(run['label'] for run in runs if run['solver'] != 'primo')]
        curves = [(DISPLAY[label], sorted(float(r['seconds']) for r in all_rows[label].values() if decisive(r)))
                  for label in plotted_labels]
        split = {DISPLAY[label]: (reports[label]['sat'], reports[label]['unsat']) for label in plotted_labels}
        scope = 'full timing coverage' if full else 'partial matched cohort'
        title = f'{logic}: {len(names):,}/{len(expected):,} inputs ({scope})'
        caption = [f'{len(names):,} matched inputs; {plan["config"]["timeout_seconds"]}s per input. Only SAT/UNSAT decisions are plotted.',
                   f'Snapshot {started[:10]} UTC; Primo 4419dc9; one CPU; hard 8 GiB address space per process.',
                   'One static Primo profile selected on this cohort; no per-input portfolio or easy-input filtering.',
                   'SAT-model validation pending. Unknown, timeout, OOM and process errors count as unsolved.',
                   'Times below 0.01s share the left edge. Partial-cohort rankings may change on the full corpus.']
        if logic == 'UF':
            caption.append('Quantified UF: printed-model validation unsupported; OpenSMT unavailable for this logic.')
        (target / (logic.lower() + '-1200.svg')).write_text(
            svg(order_series_by_solved(curves), plan['config']['timeout_seconds'], len(names),
                title, caption, DISPLAY[best], split))
        print(f'{logic}: {len(names)}/{len(expected)} inputs; selected {best}', flush=True)
    snapshot['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
    write(target / 'summary.json', snapshot)
    (target / 'index.html').write_text(gallery(snapshot))
    print('Snapshot complete:', target, flush=True)


if __name__ == '__main__':
    main()
