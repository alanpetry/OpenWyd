#!/usr/bin/env python3
"""Catalog DX9 shader bytecode assets and source references for the WASM port.

Scans v769ClientRelease/shader/*.bin, infers shader model from bytecode header,
collects hashes/sizes, and maps references in C++ source.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

DX9_VERSION_TYPE = {
    0xFFFE: "vertex",
    0xFFFF: "pixel",
}

SHADER_NAME_RE = re.compile(r"\b([A-Za-z0-9_\-]+\.bin)\b", re.IGNORECASE)
SHADER_FAMILY_RE = re.compile(r"\b(skinmesh|vseffect|pseffect)\b", re.IGNORECASE)


def parse_shader_header(blob: bytes) -> dict[str, object]:
    info: dict[str, object] = {
        "valid_dx9_header": False,
        "shader_type": "unknown",
        "shader_model": "unknown",
        "version_token": None,
    }
    if len(blob) < 4:
        return info

    token = int.from_bytes(blob[:4], "little", signed=False)
    hi = (token >> 16) & 0xFFFF
    major = (token >> 8) & 0xFF
    minor = token & 0xFF
    shader_type = DX9_VERSION_TYPE.get(hi, "unknown")

    info["version_token"] = f"0x{token:08X}"
    info["shader_type"] = shader_type
    if shader_type != "unknown":
        info["valid_dx9_header"] = True
        prefix = "vs" if shader_type == "vertex" else "ps"
        info["shader_model"] = f"{prefix}_{major}_{minor}"
    return info


def collect_shader_assets(shader_dir: Path) -> list[dict[str, object]]:
    assets: list[dict[str, object]] = []
    for path in sorted(shader_dir.glob("*.bin")):
        blob = path.read_bytes()
        header = parse_shader_header(blob)
        assets.append(
            {
                "name": path.name,
                "relative_path": str(path).replace("\\", "/"),
                "size_bytes": len(blob),
                "sha1": hashlib.sha1(blob).hexdigest(),
                **header,
            }
        )
    return assets


def collect_source_refs(source_root: Path, shader_names: set[str]) -> dict[str, list[dict[str, object]]]:
    refs: dict[str, list[dict[str, object]]] = {name: [] for name in sorted(shader_names)}
    family_refs: dict[str, list[dict[str, object]]] = {"skinmesh": [], "vseffect": [], "pseffect": []}

    for ext in ("*.cpp", "*.h", "*.c"):
        for src in source_root.rglob(ext):
            try:
                text = src.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            lines = text.splitlines()
            for idx, line in enumerate(lines, start=1):
                for m in SHADER_NAME_RE.finditer(line):
                    token = m.group(1)
                    lower = token.lower()
                    for name in shader_names:
                        if name.lower() == lower:
                            refs[name].append(
                                {
                                    "file": str(src).replace("\\", "/"),
                                    "line": idx,
                                    "snippet": line.strip(),
                                }
                            )
                            break
                for fm in SHADER_FAMILY_RE.finditer(line):
                    fam = fm.group(1).lower()
                    if fam in family_refs:
                        family_refs[fam].append(
                            {
                                "file": str(src).replace("\\", "/"),
                                "line": idx,
                                "snippet": line.strip(),
                            }
                        )
    return {"by_file": refs, "by_family": family_refs}


def summarize(assets: list[dict[str, object]], refs: dict[str, object]) -> dict[str, object]:
    by_type = {"vertex": 0, "pixel": 0, "unknown": 0}
    by_model: dict[str, int] = {}
    total_bytes = 0

    for asset in assets:
        stype = str(asset.get("shader_type", "unknown"))
        by_type[stype] = by_type.get(stype, 0) + 1
        model = str(asset.get("shader_model", "unknown"))
        by_model[model] = by_model.get(model, 0) + 1
        total_bytes += int(asset.get("size_bytes", 0))

    refs_by_file: dict[str, list[dict[str, object]]] = refs.get("by_file", {})
    family_refs: dict[str, list[dict[str, object]]] = refs.get("by_family", {})
    referenced = sum(1 for name in refs_by_file if refs_by_file[name])
    unreferenced = sum(1 for name in refs_by_file if not refs_by_file[name])
    family_ref_count = {k: len(v) for k, v in family_refs.items()}

    return {
        "shader_count": len(assets),
        "total_bytes": total_bytes,
        "by_type": by_type,
        "by_model": dict(sorted(by_model.items())),
        "referenced_shader_count": referenced,
        "unreferenced_shader_count": unreferenced,
        "family_reference_hits": family_ref_count,
    }


def write_markdown(report_md: Path, summary: dict[str, object], assets: list[dict[str, object]], refs: dict[str, object]) -> None:
    refs_by_file: dict[str, list[dict[str, object]]] = refs.get("by_file", {})
    family_refs: dict[str, list[dict[str, object]]] = refs.get("by_family", {})
    lines: list[str] = []
    lines.append("# DX9 Shader Inventory")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append(f"- shader files: **{summary['shader_count']}**")
    lines.append(f"- total bytes: **{summary['total_bytes']}**")
    lines.append(f"- by type: `{summary['by_type']}`")
    lines.append(f"- by model: `{summary['by_model']}`")
    lines.append(f"- referenced in source: **{summary['referenced_shader_count']}**")
    lines.append(f"- unreferenced in source: **{summary['unreferenced_shader_count']}**")
    lines.append(f"- family hits (`skinmesh|vseffect|pseffect`): `{summary['family_reference_hits']}`")
    lines.append("")
    lines.append("## Assets")
    lines.append("")

    for asset in assets:
        lines.append(
            f"- `{asset['name']}` | `{asset['shader_model']}` | `{asset['shader_type']}` | "
            f"`{asset['size_bytes']} bytes` | `sha1:{asset['sha1'][:12]}`"
        )

    lines.append("")
    lines.append("## Source References")
    lines.append("")

    for name in sorted(refs_by_file):
        entries = refs_by_file[name]
        lines.append(f"### `{name}`")
        if not entries:
            lines.append("- no source references found")
            lines.append("")
            continue
        for ref in entries:
            lines.append(f"- `{ref['file']}:{ref['line']}` — `{ref['snippet']}`")
        lines.append("")

    lines.append("## Family References")
    lines.append("")
    for fam in ("skinmesh", "vseffect", "pseffect"):
        entries = family_refs.get(fam, [])
        lines.append(f"### `{fam}`")
        if not entries:
            lines.append("- no source references found")
            lines.append("")
            continue
        for ref in entries:
            lines.append(f"- `{ref['file']}:{ref['line']}` — `{ref['snippet']}`")
        lines.append("")

    report_md.parent.mkdir(parents=True, exist_ok=True)
    report_md.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument(
        "--shader-dir",
        type=Path,
        default=Path("v769ClientRelease/shader"),
    )
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path("Projects/TMProject"),
    )
    parser.add_argument(
        "--report-json",
        type=Path,
        default=Path("webclient/client-wasm/build/reports/dx9-shader-inventory.json"),
    )
    parser.add_argument(
        "--report-md",
        type=Path,
        default=Path("webclient/client-wasm/build/reports/dx9-shader-inventory.md"),
    )
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    shader_dir = (repo_root / args.shader_dir).resolve()
    source_root = (repo_root / args.source_root).resolve()
    report_json = (repo_root / args.report_json).resolve()
    report_md = (repo_root / args.report_md).resolve()

    if not shader_dir.exists():
        raise SystemExit(f"shader dir not found: {shader_dir}")
    if not source_root.exists():
        raise SystemExit(f"source root not found: {source_root}")

    assets = collect_shader_assets(shader_dir)
    names = {a["name"] for a in assets}
    refs = collect_source_refs(source_root, names)
    summary = summarize(assets, refs)

    payload = {
        "ok": True,
        "shader_dir": str(shader_dir).replace("\\", "/"),
        "source_root": str(source_root).replace("\\", "/"),
        "summary": summary,
        "assets": assets,
        "references": refs,
    }

    report_json.parent.mkdir(parents=True, exist_ok=True)
    report_json.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    write_markdown(report_md, summary, assets, refs)

    print(f"[dx9-shader-inventory] shader_dir={shader_dir}")
    print(f"[dx9-shader-inventory] shaders={summary['shader_count']} total_bytes={summary['total_bytes']}")
    print(f"[dx9-shader-inventory] by_type={summary['by_type']}")
    print(f"[dx9-shader-inventory] by_model={summary['by_model']}")
    print(f"[dx9-shader-inventory] referenced={summary['referenced_shader_count']} unreferenced={summary['unreferenced_shader_count']}")
    print(f"[dx9-shader-inventory] report_json={report_json}")
    print(f"[dx9-shader-inventory] report_md={report_md}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
