#!/usr/bin/env python3
"""Assemble the current and historical books and preserve original URL targets."""

from __future__ import annotations

import argparse
import html
import json
import os
import shutil
from collections import defaultdict
from pathlib import Path, PurePosixPath
from urllib.parse import unquote, urlsplit

from check_doc_links import Page


def target(root: Path, uri: str) -> tuple[Path, str]:
    url = urlsplit(uri)
    path = PurePosixPath(unquote(url.path))
    if url.scheme or url.netloc or path.is_absolute() or ".." in path.parts:
        raise ValueError(f"unsafe compatibility target: {uri}")
    result = root / path
    if not result.resolve().is_relative_to(root.resolve()):
        raise ValueError(f"compatibility target escapes site: {uri}")
    return result, unquote(url.fragment)


def routing_script(routes: dict[str, str]) -> str:
    # Both keys and destinations originate in our manifest, never a query string.
    data = json.dumps(routes, ensure_ascii=True).replace("<", "\\u003c")
    return f"""<script data-primo-compatibility>
(() => {{
  const routes = {data};
  let fragment;
  try {{ fragment = decodeURIComponent(location.hash.slice(1)); }}
  catch (_) {{ return; }}
  if (Object.prototype.hasOwnProperty.call(routes, fragment))
    location.replace(routes[fragment]);
}})();
</script>"""


def assemble(current: Path, history: Path, output: Path, manifest: dict) -> dict:
    for source in (current, history):
        if not (source / "index.html").is_file():
            raise ValueError(f"book is not built: {source}")
        if output.resolve().is_relative_to(source.resolve()) or source.resolve().is_relative_to(
            output.resolve()
        ):
            raise ValueError("assembled site must be separate from the source books")
    # Validate every historical target before replacing any previous site.
    pages: dict[Path, Page] = {}
    grouped: dict[str, list[tuple[str, str]]] = defaultdict(list)
    for route in manifest["routes"]:
        old_file, old_fragment = target(current, route["old_path"])
        archive_file, archive_fragment = target(output, route["archive_path"])
        archive_relative = archive_file.relative_to(output)
        if archive_relative.parts[:2] != ("archive", "original"):
            raise ValueError("archive route must be under archive/original")
        original = history.joinpath(*archive_relative.parts[2:])
        if not original.is_file():
            raise ValueError(f"missing archived page: {original}")
        if original not in pages:
            pages[original] = Page(original.read_text(encoding="utf-8"))
        if archive_fragment and archive_fragment not in pages[original].ids:
            raise ValueError(f"missing archived anchor: {route['archive_path']}")
        relative = old_file.relative_to(current).as_posix()
        grouped[relative].append((old_fragment, route["archive_path"]))

    if output.exists():
        shutil.rmtree(output)
    shutil.copytree(current, output)
    shutil.copytree(history, output / "archive/original")
    report = {"targets": len(manifest["routes"]), "redirect_pages": 0, "fragment_routes": 0}
    for relative, entries in sorted(grouped.items()):
        page = output / relative
        exists = page.is_file()
        text = page.read_text(encoding="utf-8") if exists else ""
        ids = Page(text).ids
        routes = {}
        links = []
        for fragment, archive_uri in entries:
            if exists and (not fragment or fragment in ids):
                continue
            archive_path, _, anchor = archive_uri.partition("#")
            href = os.path.relpath(output / archive_path, page.parent)
            if anchor:
                href += "#" + anchor
            routes[fragment] = href
            label = fragment or "Historical page"
            id_attribute = f' id="{html.escape(fragment, quote=True)}"' if fragment else ""
            links.append(
                f'<li{id_attribute}><a href="{html.escape(href, quote=True)}">'
                f"{html.escape(label)}</a></li>"
            )
        if not routes:
            continue
        fallback = (
            '<aside aria-label="Historical documentation"><details><summary>'
            "Sections from the historical manual</summary><ul>"
            + "".join(links)
            + "</ul></details></aside>"
            + routing_script(routes)
        )
        if exists:
            text = text.replace("</body>", fallback + "</body>")
            report["fragment_routes"] += len(routes)
        else:
            page.parent.mkdir(parents=True, exist_ok=True)
            text = (
                '<!doctype html><html lang="en"><head><meta charset="utf-8">'
                "<title>Historical Primo documentation</title></head><body>"
                "<h1>This page is in the historical manual</h1>"
                + fallback
                + "</body></html>"
            )
            report["redirect_pages"] += 1
        page.write_text(text, encoding="utf-8")
    assembled_pages: dict[Path, Page] = {}
    for route in manifest["routes"]:
        for key in ("old_path", "archive_path"):
            page, anchor = target(output, route[key])
            if page.is_file() and page not in assembled_pages:
                assembled_pages[page] = Page(page.read_text(encoding="utf-8"))
            if page not in assembled_pages or (anchor and anchor not in assembled_pages[page].ids):
                raise ValueError(f"unresolved compatibility target: {route[key]}")
    (output / ".nojekyll").touch()
    (output / "compatibility-report.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--current", type=Path, default=Path("docs/_build/html"))
    parser.add_argument("--history", type=Path, default=Path("docs-history/_build/html"))
    parser.add_argument("--output", type=Path, default=Path("docs/_build/site"))
    parser.add_argument("--manifest", type=Path, default=Path("docs/coverage/compatibility.json"))
    args = parser.parse_args()
    print(json.dumps(assemble(args.current, args.history, args.output, json.loads(args.manifest.read_text()))))


if __name__ == "__main__":
    main()
