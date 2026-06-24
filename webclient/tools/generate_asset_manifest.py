#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import posixpath
from collections import Counter, defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_RELEASE_DIR = (SCRIPT_DIR / "../../v769ClientRelease").resolve()
DEFAULT_OUTPUT_DIR = (SCRIPT_DIR / "../generated").resolve()

BOOTSTRAP_REQUEST_PATHS = [
    "Config.bin",
    "serverlist.bin",
    "sn.bin",
    "ItemList.bin",
    "Itemname.bin",
    "SkillData.bin",
    "object.bin",
    "sound\\soundlist.txt",
    "UI\\UITextureListN.bin",
    "UI\\UITextureSetList.txt",
    "UI\\UIString.txt",
    "UI\\strdef.bin",
    "UI\\SelServerScene2.bin",
    "UI\\SelCharScene2.bin",
    "Effect\\EffectTextureList.bin",
    "Mesh\\MeshTextureList.bin",
    "Mesh\\MeshList.txt",
    "Mesh\\ValidIndex.bin",
    "Mesh\\BoneAni4.txt",
    "Env\\EnvTextureList3.bin",
    "Shader\\skinmesh1.bin",
    "Shader\\skinmesh2.bin",
    "Shader\\skinmesh3.bin",
    "Shader\\skinmesh4.bin",
    "Shader\\skinmesh5.bin",
    "Shader\\skinmesh6.bin",
    "Shader\\skinmesh7.bin",
    "Shader\\skinmesh8.bin",
    "Shader\\vseffect1.bin",
    "Shader\\vseffect2.bin",
    "Shader\\vseffect3.bin",
    "Shader\\vseffect4.bin",
    "Shader\\pseffect1.bin",
    "Shader\\pseffect2.bin",
    "Shader\\pseffect3.bin",
    "Shader\\pseffect4.bin",
    "Shader\\pseffect5.bin",
    "Shader\\pseffect6.bin",
]


@dataclass(frozen=True)
class AssetEntry:
    path: str
    size: int
    ext: str
    top_level: str


def normalize_client_path(path: str) -> str:
    normalized = path.strip().replace("\\", "/")
    normalized = normalized.lstrip("./")
    normalized = posixpath.normpath(normalized)

    if normalized == ".":
        return ""

    normalized = normalized.replace("//", "/")
    return normalized


def iter_assets(release_dir: Path) -> Iterable[AssetEntry]:
    for file_path in release_dir.rglob("*"):
        if not file_path.is_file():
            continue

        rel = file_path.relative_to(release_dir).as_posix()
        ext = file_path.suffix.lower().lstrip(".")
        top_level = rel.split("/", 1)[0] if "/" in rel else "(root)"
        yield AssetEntry(path=rel, size=file_path.stat().st_size, ext=ext or "(none)", top_level=top_level)


def build_case_index(entries: Iterable[AssetEntry]) -> Tuple[Dict[str, str], Dict[str, List[str]]]:
    lookup: Dict[str, str] = {}
    collisions: Dict[str, List[str]] = defaultdict(list)

    for entry in entries:
        key = entry.path.lower()
        if key not in lookup:
            lookup[key] = entry.path
            continue

        if lookup[key] != entry.path:
            if not collisions[key]:
                collisions[key].append(lookup[key])
            collisions[key].append(entry.path)

    return lookup, collisions


def resolve_path(case_index: Dict[str, str], requested_path: str) -> str | None:
    normalized = normalize_client_path(requested_path)
    if not normalized or normalized.startswith("../"):
        return None

    return case_index.get(normalized.lower())


def generate_manifest(release_dir: Path, output_dir: Path) -> None:
    entries = sorted(iter_assets(release_dir), key=lambda e: e.path.lower())
    case_index, collisions = build_case_index(entries)

    extension_count = Counter(entry.ext for entry in entries)
    top_level_count = Counter(entry.top_level for entry in entries)

    assets_total_size = sum(entry.size for entry in entries)

    bootstrap_files = []
    missing_bootstrap = []
    for request_path in BOOTSTRAP_REQUEST_PATHS:
        resolved = resolve_path(case_index, request_path)
        normalized = normalize_client_path(request_path)

        if resolved is None:
            missing_bootstrap.append({
                "requestPath": request_path,
                "normalizedPath": normalized,
            })
            continue

        size = next((e.size for e in entries if e.path == resolved), None)
        bootstrap_files.append({
            "requestPath": request_path,
            "normalizedPath": normalized,
            "resolvedPath": resolved,
            "size": size,
        })

    output_dir.mkdir(parents=True, exist_ok=True)

    manifest = {
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "releaseRoot": str(release_dir),
        "totals": {
            "files": len(entries),
            "bytes": assets_total_size,
        },
        "counts": {
            "byExtension": dict(sorted(extension_count.items(), key=lambda kv: (-kv[1], kv[0]))),
            "byTopLevel": dict(sorted(top_level_count.items(), key=lambda kv: (-kv[1], kv[0]))),
        },
        "files": [
            {
                "path": entry.path,
                "size": entry.size,
                "ext": entry.ext,
                "topLevel": entry.top_level,
            }
            for entry in entries
        ],
    }

    case_index_payload = {
        "generatedAt": manifest["generatedAt"],
        "entries": len(case_index),
        "collisions": dict(collisions),
        "pathByLowercase": case_index,
    }

    bootstrap_manifest = {
        "generatedAt": manifest["generatedAt"],
        "files": bootstrap_files,
        "missing": missing_bootstrap,
    }

    (output_dir / "asset-manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    (output_dir / "asset-index.json").write_text(
        json.dumps(case_index_payload, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    (output_dir / "bootstrap-manifest.json").write_text(
        json.dumps(bootstrap_manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate WYD client asset manifest and case-insensitive index")
    parser.add_argument(
        "--release-dir",
        default=str(DEFAULT_RELEASE_DIR),
        help="Path to v769ClientRelease directory",
    )
    parser.add_argument(
        "--output-dir",
        default=str(DEFAULT_OUTPUT_DIR),
        help="Directory where generated JSON files will be written",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    release_dir = Path(args.release_dir).resolve()
    output_dir = Path(args.output_dir).resolve()

    if not release_dir.exists() or not release_dir.is_dir():
        raise FileNotFoundError(f"Release directory not found: {release_dir}")

    generate_manifest(release_dir=release_dir, output_dir=output_dir)
    print(f"Generated manifests in {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
