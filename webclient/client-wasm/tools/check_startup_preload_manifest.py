#!/usr/bin/env python3
"""Validate the startup preload manifest used by the WASM select-server harness.

The startup link step expands this manifest into Emscripten --preload-file
arguments. This checker keeps that expansion auditable by reporting missing
literal files, empty globs, duplicate WASM destinations, and the final expanded
entry count before a visual capture run is trusted.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class PreloadEntry:
    line: int
    source: str
    destination: str
    expanded_source: str | None = None
    expanded_destination: str | None = None


def split_manifest_line(raw: str) -> tuple[str, str]:
    source, _, destination = raw.partition("@")
    return source.strip(), destination.strip()


def has_glob_pattern(value: str) -> bool:
    return any(ch in value for ch in ("*", "?", "["))


def wildcard_base(source_spec: str) -> Path:
    wildcard_pos: int | None = None
    for token in ("*", "?", "["):
        pos = source_spec.find(token)
        if pos != -1 and (wildcard_pos is None or pos < wildcard_pos):
            wildcard_pos = pos
    if wildcard_pos is None:
        return Path(".")

    prefix = source_spec[:wildcard_pos]
    slash = prefix.rfind("/")
    if slash == -1:
        return Path(".")

    base = prefix[:slash].rstrip("/")
    return Path(base) if base else Path(".")


def expand_destination(source: Path, source_root: Path, destination_spec: str, expanded_count: int) -> str:
    if not destination_spec:
        return source.as_posix()
    if destination_spec.endswith("/"):
        try:
            rel_tail = source.relative_to(source_root).as_posix()
        except ValueError:
            rel_tail = source.name
        return f"{destination_spec}{rel_tail}"
    if expanded_count == 1:
        return destination_spec
    return f"{destination_spec}/{source.name}"


def parse_manifest(repo_root: Path, manifest_path: Path) -> tuple[list[PreloadEntry], list[PreloadEntry], list[PreloadEntry]]:
    expanded: list[PreloadEntry] = []
    missing_literals: list[PreloadEntry] = []
    empty_globs: list[PreloadEntry] = []

    for line_no, raw in enumerate(manifest_path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue

        source_spec, destination_spec = split_manifest_line(line)
        if not source_spec:
            continue

        if not has_glob_pattern(source_spec):
            source_abs = repo_root / source_spec
            entry = PreloadEntry(line_no, source_spec, destination_spec)
            if not source_abs.exists():
                missing_literals.append(entry)
                continue
            expanded.append(
                PreloadEntry(
                    line_no,
                    source_spec,
                    destination_spec,
                    source_spec,
                    destination_spec or source_spec,
                )
            )
            continue

        matches = sorted(path for path in repo_root.glob(source_spec) if path.is_file())
        if not matches:
            empty_globs.append(PreloadEntry(line_no, source_spec, destination_spec))
            continue

        source_root = repo_root / wildcard_base(source_spec)
        for match in matches:
            source_rel = match.relative_to(repo_root)
            expanded.append(
                PreloadEntry(
                    line_no,
                    source_spec,
                    destination_spec,
                    source_rel.as_posix(),
                    expand_destination(source_rel, source_root.relative_to(repo_root), destination_spec, len(matches)),
                )
            )

    return expanded, missing_literals, empty_globs


def duplicate_destinations(entries: list[PreloadEntry]) -> dict[str, list[dict[str, object]]]:
    by_destination: dict[str, list[dict[str, object]]] = {}
    for entry in entries:
        if not entry.expanded_destination:
            continue
        by_destination.setdefault(entry.expanded_destination, []).append(
            {"line": entry.line, "source": entry.expanded_source}
        )
    return {dest: values for dest, values in by_destination.items() if len(values) > 1}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("webclient/client-wasm/config/startup-preload-manifest.txt"),
    )
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--md-out", type=Path)
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    manifest_path = args.manifest if args.manifest.is_absolute() else repo_root / args.manifest
    expanded, missing_literals, empty_globs = parse_manifest(repo_root, manifest_path)
    duplicates = duplicate_destinations(expanded)

    ok = not missing_literals and not empty_globs and not duplicates
    payload = {
        "ok": ok,
        "manifest": str(manifest_path.relative_to(repo_root)),
        "expanded_entry_count": len(expanded),
        "missing_literal_count": len(missing_literals),
        "empty_glob_count": len(empty_globs),
        "duplicate_destination_count": len(duplicates),
        "missing_literals": [entry.__dict__ for entry in missing_literals],
        "empty_globs": [entry.__dict__ for entry in empty_globs],
        "duplicate_destinations": duplicates,
    }

    text = json.dumps(payload, indent=2)
    print(text)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(text + "\n", encoding="utf-8")
    if args.md_out:
        args.md_out.parent.mkdir(parents=True, exist_ok=True)
        lines = [
            "# Startup Preload Manifest Check",
            "",
            f"- ok: **{str(ok).lower()}**",
            f"- expanded entries: **{len(expanded)}**",
            f"- missing literal files: **{len(missing_literals)}**",
            f"- empty globs: **{len(empty_globs)}**",
            f"- duplicate WASM destinations: **{len(duplicates)}**",
            "",
        ]
        args.md_out.write_text("\n".join(lines), encoding="utf-8")

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
