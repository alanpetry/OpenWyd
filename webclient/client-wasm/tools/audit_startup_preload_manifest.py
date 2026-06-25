#!/usr/bin/env python3
"""Audit the startup preload manifest used by the WASM select-server harness."""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path


@dataclass
class ManifestEntry:
    line_no: int
    source: str
    target: str
    expanded: list[str]


def wildcard_base(source: str) -> Path:
    wildcard_pos: int | None = None
    for token in ("*", "?", "["):
        pos = source.find(token)
        if pos != -1 and (wildcard_pos is None or pos < wildcard_pos):
            wildcard_pos = pos

    if wildcard_pos is None:
        return Path(source).parent

    base = source[:wildcard_pos]
    slash = base.rfind("/")
    if slash == -1:
        return Path(".")
    base_dir = base[:slash].rstrip("/")
    return Path(base_dir or ".")


def expand_entry(repo_root: Path, source: str, target: str) -> list[str]:
    has_glob = any(ch in source for ch in ("*", "?", "["))
    if not has_glob:
        return [source] if (repo_root / source).is_file() else []

    root = (repo_root / wildcard_base(source)).resolve()
    expanded: list[str] = []
    for item in sorted(repo_root.glob(source)):
        if not item.is_file():
            continue
        try:
            tail = item.resolve().relative_to(root).as_posix()
        except ValueError:
            tail = item.name
        expanded.append(tail if target.endswith("/") else item.name)
    return expanded


def load_manifest(repo_root: Path, manifest: Path) -> tuple[list[ManifestEntry], list[dict[str, object]]]:
    entries: list[ManifestEntry] = []
    malformed: list[dict[str, object]] = []

    for line_no, raw in enumerate(manifest.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue

        source, sep, target = line.partition("@")
        source = source.strip()
        target = target.strip()
        if not source or not sep or not target:
            malformed.append({"line": line_no, "text": raw})
            continue

        entries.append(
            ManifestEntry(
                line_no=line_no,
                source=source,
                target=target,
                expanded=expand_entry(repo_root, source, target),
            )
        )

    return entries, malformed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("webclient/client-wasm/config/startup-preload-manifest.txt"),
    )
    parser.add_argument(
        "--report-json",
        type=Path,
        default=Path("webclient/client-wasm/build/reports/startup-preload-audit.json"),
    )
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    manifest = (repo_root / args.manifest).resolve()
    report_json = (repo_root / args.report_json).resolve()

    entries, malformed = load_manifest(repo_root, manifest)
    empty = [entry for entry in entries if not entry.expanded]
    expanded_total = sum(len(entry.expanded) for entry in entries)

    report = {
        "ok": not malformed and not empty,
        "manifest": manifest.relative_to(repo_root).as_posix(),
        "entry_count": len(entries),
        "expanded_file_count": expanded_total,
        "malformed": malformed,
        "empty_or_missing": [
            {"line": entry.line_no, "source": entry.source, "target": entry.target}
            for entry in empty
        ],
    }

    report_json.parent.mkdir(parents=True, exist_ok=True)
    report_json.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print(f"[preload-audit] manifest={report['manifest']}")
    print(f"[preload-audit] entries={len(entries)} expanded_files={expanded_total}")
    print(f"[preload-audit] malformed={len(malformed)} empty_or_missing={len(empty)}")
    print(f"[preload-audit] report_json={report_json.relative_to(repo_root)}")

    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
