#!/usr/bin/env python3
"""Match runtime shader hashes captured from smoke JSON to real shader .bin files."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct


REPO_ROOT = Path(__file__).resolve().parents[3]
SHADER_DIR = REPO_ROOT / "v769ClientRelease" / "shader"
DEFAULT_SMOKE = REPO_ROOT / "webclient" / "client-wasm" / "build" / "reports" / "startup-smoke.json"
REPORT_DIR = REPO_ROOT / "webclient" / "client-wasm" / "build" / "reports"


def fnv1a64(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def version_name(version_token: int) -> str:
    if version_token == 0xFFFE0101:
        return "vs_1_1"
    if version_token == 0xFFFF0101:
        return "ps_1_1"
    return f"0x{version_token:08x}"


def shader_hash_from_runtime_style(blob: bytes) -> tuple[int, int, int]:
    dwords = [dw[0] for dw in struct.iter_unpack("<I", blob[: len(blob) - (len(blob) % 4)])]
    if not dwords:
        return 0, 0, 0
    end = len(dwords)
    for i, token in enumerate(dwords):
        if token == 0x0000FFFF:
            end = i + 1
            break
    packed = struct.pack("<" + "I" * end, *dwords[:end])
    return fnv1a64(packed), end, dwords[0]


def scan_shader_files() -> dict[int, dict]:
    out: dict[int, dict] = {}
    for path in sorted(SHADER_DIR.glob("*.bin")):
        blob = path.read_bytes()
        h_runtime, dword_count, version = shader_hash_from_runtime_style(blob)
        out[h_runtime] = {
            "file": str(path.relative_to(REPO_ROOT)),
            "hash": h_runtime,
            "hash_hex": f"0x{h_runtime:016x}",
            "dword_count": dword_count,
            "version_token": version,
            "version": version_name(version),
            "size_bytes": len(blob),
        }
    return out


def combine_hi_lo(hi: int | None, lo: int | None) -> int:
    if hi is None or lo is None:
        return 0
    return ((int(hi) & 0xFFFFFFFF) << 32) | (int(lo) & 0xFFFFFFFF)


def collect_runtime_hashes(smoke: dict) -> dict[str, list[dict]]:
    out: dict[str, list[dict]] = {"vs": [], "ps": []}

    avs = combine_hi_lo(smoke.get("activeVsHashHi"), smoke.get("activeVsHashLo"))
    aps = combine_hi_lo(smoke.get("activePsHashHi"), smoke.get("activePsHashLo"))
    if avs:
        out["vs"].append({"label": "active", "hash": avs})
    if aps:
        out["ps"].append({"label": "active", "hash": aps})

    for item in smoke.get("vsTop", []) or []:
        h = combine_hi_lo(item.get("hashHi"), item.get("hashLo"))
        if h:
            out["vs"].append({
                "label": f"top{item.get('rank', 0)}",
                "hash": h,
                "binds": int(item.get("binds", 0)),
                "uses": int(item.get("uses", 0)),
                "skips": int(item.get("skips", 0)),
                "version_token": int(item.get("version", 0)),
            })

    for item in smoke.get("psTop", []) or []:
        h = combine_hi_lo(item.get("hashHi"), item.get("hashLo"))
        if h:
            out["ps"].append({
                "label": f"top{item.get('rank', 0)}",
                "hash": h,
                "binds": int(item.get("binds", 0)),
                "uses": int(item.get("uses", 0)),
                "skips": int(item.get("skips", 0)),
                "version_token": int(item.get("version", 0)),
            })

    # de-dup while preserving first appearance
    for key in ("vs", "ps"):
        seen: set[int] = set()
        dedup = []
        for item in out[key]:
            h = item["hash"]
            if h in seen:
                continue
            seen.add(h)
            dedup.append(item)
        out[key] = dedup

    return out


def write_reports(report: dict) -> None:
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    json_path = REPORT_DIR / "shader-runtime-match.json"
    md_path = REPORT_DIR / "shader-runtime-match.md"
    json_path.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")

    lines = [
        "# Runtime Shader Hash Match",
        "",
        f"- smoke source: `{report['smoke_source']}`",
        f"- matched VS entries: **{report['summary']['vs_matched']}** / {report['summary']['vs_total']}",
        f"- matched PS entries: **{report['summary']['ps_matched']}** / {report['summary']['ps_total']}",
        "",
        "## Vertex Shaders",
        "",
    ]

    for item in report["runtime_vs"]:
        lines.append(
            f"- `{item['label']}` `{item['hash_hex']}` => `{item.get('matched_file', 'unmatched')}` "
            f"(binds={item.get('binds', 0)}, uses={item.get('uses', 0)}, skips={item.get('skips', 0)}, version={item.get('version', 'n/a')})"
        )

    lines += ["", "## Pixel Shaders", ""]
    for item in report["runtime_ps"]:
        lines.append(
            f"- `{item['label']}` `{item['hash_hex']}` => `{item.get('matched_file', 'unmatched')}` "
            f"(binds={item.get('binds', 0)}, uses={item.get('uses', 0)}, skips={item.get('skips', 0)}, version={item.get('version', 'n/a')})"
        )

    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"[shader-match] wrote {json_path.relative_to(REPO_ROOT)}")
    print(f"[shader-match] wrote {md_path.relative_to(REPO_ROOT)}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--smoke", type=Path, default=DEFAULT_SMOKE)
    args = parser.parse_args()

    smoke_path = args.smoke if args.smoke.is_absolute() else (REPO_ROOT / args.smoke)
    smoke = json.loads(smoke_path.read_text(encoding="utf-8"))
    runtime = collect_runtime_hashes(smoke)
    catalog = scan_shader_files()

    def annotate(items: list[dict]) -> tuple[list[dict], int]:
        matched = 0
        out = []
        for item in items:
            h = item["hash"]
            row = dict(item)
            row["hash_hex"] = f"0x{h:016x}"
            row["version"] = version_name(int(row.get("version_token", 0))) if row.get("version_token") else "n/a"
            hit = catalog.get(h)
            if hit:
                matched += 1
                row["matched_file"] = hit["file"]
                row["matched_version"] = hit["version"]
                row["matched_dwords"] = hit["dword_count"]
            out.append(row)
        return out, matched

    runtime_vs, vs_matched = annotate(runtime["vs"])
    runtime_ps, ps_matched = annotate(runtime["ps"])

    report = {
        "smoke_source": str(smoke_path.relative_to(REPO_ROOT)),
        "runtime_vs": runtime_vs,
        "runtime_ps": runtime_ps,
        "summary": {
            "vs_total": len(runtime_vs),
            "vs_matched": vs_matched,
            "ps_total": len(runtime_ps),
            "ps_matched": ps_matched,
        },
    }
    write_reports(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
