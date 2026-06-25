#!/usr/bin/env python3
"""Audit the client-wasm startup preload manifest.

This tool expands the manifest the same way the startup link step expects it:
comments are ignored, direct files must exist, and glob entries are expanded
against the repository root. The generated reports make it easier to spot
missing select-server assets before comparing the WASM scene visually.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass
from pathlib import Path


@dataclass
class PreloadEntry:
    manifest_line: int
    source: str
    destination: str
    expanded_source: str | None
    expanded_destination: str | None
    size: int | None
    status: str


def has_glob(path_spec: str) -> bool:
    return any(ch in path_spec for ch in ("*", "?", "["))


def wildcard_base(src_spec: str) -> Path | None:
    wildcard_pos = None
    for token in ("*", "?", "["):
        pos = src_spec.find(token)
        if pos != -1 and (wildcard_pos is None or pos < wildcard_pos):
            wildcard_pos = pos
    if wildcard_pos is None:
        return None

    base = src_spec[:wildcard_pos]
    slash = base.rfind("/")
    if slash == -1:
        return Path(".")

    base_dir = base[:slash].rstrip("/")
    return Path(base_dir) if base_dir else Path(".")


def destination_for(item: Path, repo_root: Path, src_spec: str, dst_spec: str, expanded_count: int) -> str | None:
    if not dst_spec:
        return None

    if not dst_spec.endswith("/"):
        return dst_spec if expanded_count == 1 else f"{dst_spec}/{item.name}"

    rel_tail = item.name
    wildcard_root = wildcard_base(src_spec)
    if wildcard_root:
        wildcard_root_abs = (repo_root / wildcard_root).resolve()
        try:
            rel_tail = item.resolve().relative_to(wildcard_root_abs).as_posix()
        except ValueError:
            rel_tail = item.name
    return f"{dst_spec}{rel_tail}"


def audit_manifest(repo_root: Path, manifest_path: Path) -> list[PreloadEntry]:
    entries: list[PreloadEntry] = []
    lines = manifest_path.read_text(encoding="utf-8").splitlines()

    for line_no, raw in enumerate(lines, start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue

        src_spec, _, dst_spec = line.partition("@")
        src_spec = src_spec.strip()
        dst_spec = dst_spec.strip()
        if not src_spec:
            entries.append(PreloadEntry(line_no, src_spec, dst_spec, None, None, None, "empty-source"))
            continue

        if not has_glob(src_spec):
            src_path = (repo_root / src_spec).resolve()
            if not src_path.is_file():
                entries.append(PreloadEntry(line_no, src_spec, dst_spec, None, None, None, "missing"))
                continue

            entries.append(
                PreloadEntry(
                    line_no,
                    src_spec,
                    dst_spec,
                    src_path.relative_to(repo_root).as_posix(),
                    dst_spec or None,
                    src_path.stat().st_size,
                    "ok",
                )
            )
            continue

        expanded = sorted((repo_root / ".").glob(src_spec))
        expanded_files = [item for item in expanded if item.is_file()]
        if not expanded_files:
            entries.append(PreloadEntry(line_no, src_spec, dst_spec, None, None, None, "empty-glob"))
            continue

        for item in expanded_files:
            src_rel = item.relative_to(repo_root).as_posix()
            entries.append(
                PreloadEntry(
                    line_no,
                    src_spec,
                    dst_spec,
                    src_rel,
                    destination_for(item, repo_root, src_spec, dst_spec, len(expanded_files)),
                    item.stat().st_size,
                    "ok",
                )
            )

    return entries


def write_markdown(report_md: Path, manifest_path: Path, entries: list[PreloadEntry]) -> None:
    ok_count = sum(1 for entry in entries if entry.status == "ok")
    missing = [entry for entry in entries if entry.status != "ok"]
    total_size = sum(entry.size or 0 for entry in entries if entry.status == "ok")

    lines: list[str] = [
        "# Startup Preload Audit",
        "",
        "## Summary",
        "",
        f"- manifest: `{manifest_path.as_posix()}`",
        f"- expanded files: **{ok_count}**",
        f"- missing or empty entries: **{len(missing)}**",
        f"- expanded byte size: **{total_size}**",
        "",
        "## Missing or Empty Entries",
        "",
    ]

    if missing:
        for entry in missing:
            lines.append(f"- line {entry.manifest_line}: `{entry.source}` ({entry.status})")
    else:
        lines.append("- none")

    lines.extend(["", "## Expanded Entries", ""])
    for entry in entries:
        if entry.status != "ok":
            continue
        dst = entry.expanded_destination or ""
        size = entry.size or 0
        lines.append(f"- `{entry.expanded_source}` -> `{dst}` ({size} bytes)")

    report_md.parent.mkdir(parents=True, exist_ok=True)
    report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")


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
    parser.add_argument(
        "--report-md",
        type=Path,
        default=Path("webclient/client-wasm/build/reports/startup-preload-audit.md"),
    )
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    manifest_path = (repo_root / args.manifest).resolve()
    report_json = (repo_root / args.report_json).resolve()
    report_md = (repo_root / args.report_md).resolve()

    if not manifest_path.is_file():
        print(f"[preload-audit] manifest not found: {manifest_path}")
        return 2

    entries = audit_manifest(repo_root, manifest_path)
    payload = {
        "manifest": manifest_path.relative_to(repo_root).as_posix(),
        "expanded_files": sum(1 for entry in entries if entry.status == "ok"),
        "missing_or_empty": sum(1 for entry in entries if entry.status != "ok"),
        "expanded_byte_size": sum(entry.size or 0 for entry in entries if entry.status == "ok"),
        "entries": [asdict(entry) for entry in entries],
    }

    report_json.parent.mkdir(parents=True, exist_ok=True)
    report_json.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    write_markdown(report_md, manifest_path.relative_to(repo_root), entries)

    print(f"[preload-audit] expanded_files={payload['expanded_files']}")
    print(f"[preload-audit] missing_or_empty={payload['missing_or_empty']}")
    print(f"[preload-audit] report_json={report_json.relative_to(repo_root)}")
    print(f"[preload-audit] report_md={report_md.relative_to(repo_root)}")
    return 0 if payload["missing_or_empty"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
