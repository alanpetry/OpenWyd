#!/usr/bin/env python3
"""Audit select-server route telemetry exports for the WASM startup harness.

The select-server scene already captures movement and route-progress fields in
its Emscripten telemetry struct. This checker verifies that each expected field
also has a C accessor and is exported by the startup link script, so visual
smoke snapshots can distinguish moving humans from route-stalled humans.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


EXPECTED_FIELDS = {
    "moving": "int",
    "last_route_index": "int",
    "max_route_index": "int",
}

SOURCE_PATH = Path("Projects/TMProject/TMSelectServerScene.cpp")
LINK_PATH = Path("webclient/client-wasm/tools/link_tmproject_wasm_startup.py")


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return ""


def has_accessor(source: str, name: str, return_type: str) -> bool:
    pattern = rf'extern\s+"C"\s+{re.escape(return_type)}\s+wyd_selserver_human_{re.escape(name)}\s*\('
    return re.search(pattern, source) is not None


def has_link_export(link_script: str, name: str) -> bool:
    return f'"_wyd_selserver_human_{name}"' in link_script


def build_report(repo_root: Path) -> dict[str, object]:
    source = read_text(repo_root / SOURCE_PATH)
    link_script = read_text(repo_root / LINK_PATH)

    fields: list[dict[str, object]] = []
    missing: list[str] = []
    for name, return_type in EXPECTED_FIELDS.items():
        accessor = has_accessor(source, name, return_type)
        export = has_link_export(link_script, name)
        if not accessor:
            missing.append(f"{name}:accessor")
        if not export:
            missing.append(f"{name}:export")
        fields.append(
            {
                "field": name,
                "return_type": return_type,
                "accessor": accessor,
                "startup_export": export,
            }
        )

    return {
        "ok": not missing,
        "source_path": SOURCE_PATH.as_posix(),
        "link_path": LINK_PATH.as_posix(),
        "missing": missing,
        "fields": fields,
    }


def write_markdown(path: Path, report: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Select-Server Route Telemetry Exports",
        "",
        f"- ok: **{str(report['ok']).lower()}**",
        f"- missing checks: **{len(report['missing'])}**",
        "",
        "| field | C accessor | startup export |",
        "| --- | --- | --- |",
    ]
    for field in report["fields"]:
        assert isinstance(field, dict)
        lines.append(
            f"| `{field['field']}` | {str(field['accessor']).lower()} | "
            f"{str(field['startup_export']).lower()} |"
        )
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--md-out", type=Path)
    parser.add_argument(
        "--fail-on-missing",
        action="store_true",
        help="Return exit code 1 when any accessor or startup export is missing.",
    )
    args = parser.parse_args()

    report = build_report(args.repo_root.resolve())
    print(json.dumps(report, indent=2))

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if args.md_out:
        write_markdown(args.md_out, report)

    return 1 if args.fail_on_missing and not report["ok"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
