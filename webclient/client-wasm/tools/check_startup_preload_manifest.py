#!/usr/bin/env python3
"""Validate the WASM preload manifest with the same glob rules as the linker."""

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path


def wildcard_base(source: str) -> Path:
    positions = [source.find(token) for token in ("*", "?", "[") if token in source]
    if not positions:
        return Path(source).parent
    prefix = source[: min(positions)]
    slash = prefix.rfind("/")
    return Path(prefix[:slash].rstrip("/") or ".") if slash >= 0 else Path(".")


def destination_for(repo_root: Path, source: Path, source_spec: str, destination: str, count: int) -> str:
    if not destination:
        return source.relative_to(repo_root).as_posix()
    if not destination.endswith("/"):
        return destination if count == 1 else f"{destination}/{source.name}"
    try:
        tail = source.resolve().relative_to((repo_root / wildcard_base(source_spec)).resolve()).as_posix()
    except ValueError:
        tail = source.name
    return f"{destination}{tail}"


def audit(repo_root: Path, manifest: Path) -> dict[str, object]:
    missing: list[dict[str, object]] = []
    empty: list[dict[str, object]] = []
    destinations: dict[str, list[str]] = defaultdict(list)
    expanded_count = 0

    for line_no, raw in enumerate(manifest.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        source_spec, separator, destination = line.partition("@")
        source_spec = source_spec.strip()
        destination = destination.strip()
        if not separator or not source_spec:
            missing.append({"line": line_no, "source": source_spec, "reason": "malformed"})
            continue

        has_glob = any(token in source_spec for token in ("*", "?", "["))
        if has_glob:
            matches = sorted(path for path in repo_root.glob(source_spec) if path.is_file())
            if not matches:
                empty.append({"line": line_no, "source": source_spec})
                continue
        else:
            source = repo_root / source_spec
            if not source.is_file():
                missing.append({"line": line_no, "source": source_spec, "reason": "missing"})
                continue
            matches = [source]

        expanded_count += len(matches)
        for source in matches:
            destination_path = destination_for(repo_root, source, source_spec, destination, len(matches))
            destinations[destination_path].append(source.relative_to(repo_root).as_posix())

    duplicates = {target: sources for target, sources in destinations.items() if len(sources) > 1}
    return {
        "ok": not missing and not empty and not duplicates,
        "manifest": manifest.relative_to(repo_root).as_posix(),
        "expanded_file_count": expanded_count,
        "missing": missing,
        "empty_globs": empty,
        "duplicate_destinations": duplicates,
    }


def main() -> int:
    default_root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=default_root)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("webclient/client-wasm/config/startup-preload-manifest.txt"),
    )
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    manifest = args.manifest if args.manifest.is_absolute() else repo_root / args.manifest
    report = audit(repo_root, manifest.resolve())
    rendered = json.dumps(report, indent=2, ensure_ascii=False)
    print(rendered)
    if args.json_out:
        output = args.json_out if args.json_out.is_absolute() else repo_root / args.json_out
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered + "\n", encoding="utf-8")
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
