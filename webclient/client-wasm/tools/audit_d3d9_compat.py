#!/usr/bin/env python3
"""Generate a D3D9 compatibility coverage report for the WASM bridge."""

from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
SOURCE_ROOT = REPO_ROOT / "Projects" / "TMProject"
HEADER = REPO_ROOT / "webclient" / "client-wasm" / "compat" / "include" / "d3d9.h"
STUB = REPO_ROOT / "webclient" / "client-wasm" / "compat" / "src" / "win32_emscripten_stubs.cpp"
REPORT_DIR = REPO_ROOT / "webclient" / "client-wasm" / "build" / "reports"


RENDER_STATE_CHECKLIST = {
    "D3DRS_ZENABLE",
    "D3DRS_ZWRITEENABLE",
    "D3DRS_ZFUNC",
    "D3DRS_ALPHABLENDENABLE",
    "D3DRS_SRCBLEND",
    "D3DRS_DESTBLEND",
    "D3DRS_ALPHATESTENABLE",
    "D3DRS_ALPHAREF",
    "D3DRS_ALPHAFUNC",
    "D3DRS_LIGHTING",
    "D3DRS_AMBIENT",
    "D3DRS_TEXTUREFACTOR",
    "D3DRS_CULLMODE",
    "D3DRS_COLORVERTEX",
    "D3DRS_DIFFUSEMATERIALSOURCE",
    "D3DRS_SPECULARMATERIALSOURCE",
    "D3DRS_AMBIENTMATERIALSOURCE",
    "D3DRS_EMISSIVEMATERIALSOURCE",
    "D3DRS_FOGENABLE",
    "D3DRS_FOGCOLOR",
    "D3DRS_FOGVERTEXMODE",
    "D3DRS_FOGTABLEMODE",
    "D3DRS_FOGSTART",
    "D3DRS_FOGEND",
    "D3DRS_FOGDENSITY",
    "D3DRS_RANGEFOGENABLE",
}

METHOD_CHECKLIST = {
    "SetMaterial",
    "GetMaterial",
    "SetLight",
    "GetLight",
    "LightEnable",
    "GetLightEnable",
    "SetRenderState",
    "SetTextureStageState",
    "SetSamplerState",
    "SetTransform",
    "SetTexture",
    "SetFVF",
    "SetStreamSource",
    "SetIndices",
    "DrawPrimitiveUP",
    "DrawIndexedPrimitiveUP",
    "DrawIndexedPrimitive",
    "CreateVertexBuffer",
    "CreateIndexBuffer",
    "CreateVertexDeclaration",
    "SetVertexDeclaration",
    "CreateVertexShader",
    "SetVertexShader",
    "CreatePixelShader",
    "SetPixelShader",
}


def source_files() -> list[Path]:
    return [p for p in SOURCE_ROOT.rglob("*") if p.suffix.lower() in {".cpp", ".h"}]


def count_calls(pattern: str) -> Counter[str]:
    rx = re.compile(pattern)
    counts: Counter[str] = Counter()
    for path in source_files():
        text = path.read_text(errors="ignore")
        for match in rx.finditer(text):
            counts[match.group(1)] += 1
    return counts


def implemented_render_states() -> set[str]:
    text = STUB.read_text(errors="ignore")
    return set(re.findall(r"case\s+(D3DRS_[A-Z0-9_]+)\s*:", text))


def implemented_methods() -> set[str]:
    text = HEADER.read_text(errors="ignore")
    methods = set()
    for name in METHOD_CHECKLIST:
        if re.search(rf"\bHRESULT\s+{re.escape(name)}\s*\(", text):
            methods.add(name)
    return methods


def no_op_device_methods() -> list[str]:
    text = HEADER.read_text(errors="ignore")
    out = []
    for match in re.finditer(r"HRESULT\s+([A-Za-z0-9_]+)\s*\([^)]*\)\s*\{\s*return\s+(S_OK|E_NOTIMPL)\s*;", text):
        name, retval = match.groups()
        if name in METHOD_CHECKLIST:
            out.append(f"{name}:{retval}")
    return out


def write_reports(data: dict) -> None:
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    json_path = REPORT_DIR / "d3d9-compat-coverage.json"
    md_path = REPORT_DIR / "d3d9-compat-coverage.md"
    json_path.write_text(json.dumps(data, indent=2, sort_keys=True), encoding="utf-8")

    lines = [
        "# D3D9 WASM Compat Coverage",
        "",
        "## Summary",
        "",
        f"- render states used: **{len(data['render_state_usage'])}**",
        f"- render states implemented from used set: **{len(data['implemented_render_states_used'])}**",
        f"- render states missing from used set: **{len(data['missing_render_states_used'])}**",
        f"- checked device methods implemented: **{len(data['implemented_methods'])}/{len(METHOD_CHECKLIST)}**",
        "",
        "## Missing Render States Used By Source",
        "",
    ]
    if data["missing_render_states_used"]:
        for item in data["missing_render_states_used"]:
            lines.append(f"- `{item['name']}`: {item['count']} uses")
    else:
        lines.append("- none")

    lines += ["", "## Top Render States", ""]
    for item in data["top_render_states"]:
        status = "implemented" if item["implemented"] else "missing"
        lines.append(f"- `{item['name']}`: {item['count']} uses ({status})")

    lines += ["", "## No-op Device Methods In Checklist", ""]
    if data["no_op_device_methods"]:
        for item in data["no_op_device_methods"]:
            lines.append(f"- `{item}`")
    else:
        lines.append("- none")

    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"[d3d9-compat-audit] wrote {json_path.relative_to(REPO_ROOT)}")
    print(f"[d3d9-compat-audit] wrote {md_path.relative_to(REPO_ROOT)}")


def main() -> int:
    render_state_usage = count_calls(r"SetRenderState\s*\(\s*(D3DRS_[A-Z0-9_]+|\d+)")
    texture_stage_usage = count_calls(r"SetTextureStageState\s*\([^,]+,\s*(D3DTSS_[A-Z0-9_]+|\d+)")
    sampler_usage = count_calls(r"SetSamplerState\s*\([^,]+,\s*(D3DSAMP_[A-Z0-9_]+|\d+)")
    implemented_states = implemented_render_states()
    implemented_methods_set = implemented_methods()

    missing_used = [
        {"name": name, "count": count}
        for name, count in render_state_usage.most_common()
        if name.startswith("D3DRS_") and name not in implemented_states
    ]
    data = {
        "render_state_usage": dict(render_state_usage.most_common()),
        "texture_stage_usage": dict(texture_stage_usage.most_common()),
        "sampler_usage": dict(sampler_usage.most_common()),
        "implemented_render_states_used": sorted(
            name for name in render_state_usage if name in implemented_states
        ),
        "missing_render_states_used": missing_used,
        "top_render_states": [
            {"name": name, "count": count, "implemented": name in implemented_states}
            for name, count in render_state_usage.most_common(40)
        ],
        "implemented_methods": sorted(implemented_methods_set),
        "missing_methods": sorted(METHOD_CHECKLIST - implemented_methods_set),
        "no_op_device_methods": no_op_device_methods(),
        "references": {
            "source_root": str(SOURCE_ROOT.relative_to(REPO_ROOT)),
            "compat_header": str(HEADER.relative_to(REPO_ROOT)),
            "compat_stub": str(STUB.relative_to(REPO_ROOT)),
        },
    }
    write_reports(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
