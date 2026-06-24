#!/usr/bin/env python3
"""Audit the WASM startup preload manifest without linking the client."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path


@dataclass
class ManifestEntry:
    line: int
    source: str
    destination: str
    expanded_count: int


def wildcard_root(src_spec: str) -> Path:
    wildcard_pos = min((pos for token in ("*", "?", "[") if (pos := src_spec.find(token)) != -1), default=-1)
    if wildcard_pos == -1:
        return Path(src_spec).parent

    prefix = src_spec[:wildcard_pos]
    slash = prefix.rfind("/")
    if slash == -1:
        return Path(".")

    base_dir = prefix[:slash].rstrip("/")
    return Path(base_dir) if base_dir else Path(".")


def destination_for(source: Path, repo_root: Path, wildcard_root_abs: Path | None, dst_spec: str, expanded_count: int) -> str:
    if not dst_spec:
        return source.relative_to(repo_root).as_posix()

    if dst_spec.endswith("/"):
        rel_tail = source.name
        if wildcard_root_abs:
            try:
                rel_tail = source.resolve().relative_to(wildcard_root_abs).as_posix()
            except ValueError:
                rel_tail = source.name
        return f"{dst_spec}{rel_tail}"

    if expanded_count == 1:
        return dst_spec

    return f"{dst_spec.rstrip('/')}/{source.name}"


def audit_manifest(repo_root: Path, manifest_path: Path) -> tuple[list[ManifestEntry], list[str], list[str], list[str]]:
    entries: list[ManifestEntry] = []
    missing_files: list[str] = []
    empty_globs: list[str] = []
    malformed_lines: list[str] = []

    for line_number, raw in enumerate(manifest_path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue

        src_spec, separator, dst_spec = line.partition("@")
        src_spec = src_spec.strip()
        dst_spec = dst_spec.strip()
        if not src_spec or not separator:
            malformed_lines.append(f"{line_number}: {raw}")
            continue

        has_glob = any(ch in src_spec for ch in ("*", "?", "["))
        if not has_glob:
            src_path = (repo_root / src_spec).resolve()
            if not src_path.exists() or not src_path.is_file():
                missing_files.append(f"{line_number}: {src_spec}")
                continue
            entries.append(ManifestEntry(line_number, src_spec, dst_spec, 1))
            continue

        expanded = sorted(path for path in repo_root.glob(src_spec) if path.is_file())
        if not expanded:
            empty_globs.append(f"{line_number}: {src_spec}")
            continue

        root_abs = (repo_root / wildcard_root(src_spec)).resolve()
        for item in expanded:
            src_rel = item.relative_to(repo_root).as_posix()
            dst = destination_for(item, repo_root, root_abs, dst_spec, len(expanded))
            entries.append(ManifestEntry(line_number, src_rel, dst, len(expanded)))

    destinations = [entry.destination for entry in entries]
    duplicate_destinations = sorted(dst for dst, count in Counter(destinations).items() if count > 1)
    return entries, missing_files, empty_globs, malformed_lines + duplicate_destinations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("webclient/client-wasm/config/startup-preload-manifest.txt"),
    )
    parser.add_argument("--json", type=Path, help="Optional path for a machine-readable audit report.")
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    manifest_path = (repo_root / args.manifest).resolve()
    if not manifest_path.exists():
        raise SystemExit(f"manifest not found: {manifest_path}")

    entries, missing_files, empty_globs, problems = audit_manifest(repo_root, manifest_path)
    payload = {
        "manifest": manifest_path.relative_to(repo_root).as_posix(),
        "expanded_entries": len(entries),
        "missing_files": missing_files,
        "empty_globs": empty_globs,
        "malformed_or_duplicate_destinations": problems,
        "entries": [asdict(entry) for entry in entries],
    }

    if args.json:
        report_path = (repo_root / args.json).resolve()
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    print(f"[preload-audit] manifest={payload['manifest']}")
    print(f"[preload-audit] expanded_entries={len(entries)}")
    print(f"[preload-audit] missing_files={len(missing_files)}")
    print(f"[preload-audit] empty_globs={len(empty_globs)}")
    print(f"[preload-audit] malformed_or_duplicate_destinations={len(problems)}")

    if missing_files:
        print("[preload-audit] missing file specs:")
        for item in missing_files:
            print(f"  - {item}")
    if empty_globs:
        print("[preload-audit] empty glob specs:")
        for item in empty_globs:
            print(f"  - {item}")
    if problems:
        print("[preload-audit] malformed lines or duplicate WASM destinations:")
        for item in problems:
            print(f"  - {item}")

    return 1 if missing_files or empty_globs or problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
