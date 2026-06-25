#!/usr/bin/env python3
"""Check select-server telemetry against the current clean WASM baseline.

This is a guardrail for visual parity runs. It verifies counters that should
stay at zero before a human screenshot comparison is accepted.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


ALIASES = {
    "glErrorTotal": (
        "glErrorTotal",
        "gl_error_total",
        "wyd_d3d9_gl_error_total",
    ),
    "depthWriteGuardForcedDraws": (
        "depthWriteGuardForcedDraws",
        "depth_write_guard_forced_draws",
        "wyd_d3d9_depth_write_guard_forced_draws",
    ),
    "shaderDrawsSkipped": (
        "shaderDrawsSkipped",
        "shader_draws_skipped",
        "wyd_d3d9_shader_draws_skipped",
    ),
    "skinSuspiciousTextureDraws": (
        "skinSuspiciousTextureDraws",
        "skin_suspicious_texture_draws",
        "wyd_d3d9_skin_suspicious_texture_draws",
    ),
}


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SystemExit(f"{path}: invalid JSON: {exc}") from exc


def iter_objects(value: Any) -> Any:
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from iter_objects(child)
    elif isinstance(value, list):
        for child in value:
            yield from iter_objects(child)


def coerce_number(value: Any) -> float | None:
    if isinstance(value, bool):
        return float(value)
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str):
        try:
            return float(value)
        except ValueError:
            return None
    return None


def first_counter(payload: Any, names: tuple[str, ...]) -> tuple[str, float] | None:
    for obj in iter_objects(payload):
        for name in names:
            if name in obj:
                number = coerce_number(obj[name])
                if number is not None:
                    return name, number
    return None


def check_payload(path: Path, payload: Any, *, require_all: bool) -> dict[str, Any]:
    counters: dict[str, dict[str, Any]] = {}
    failures: list[str] = []
    missing: list[str] = []

    for canonical, aliases in ALIASES.items():
        found = first_counter(payload, aliases)
        if found is None:
            missing.append(canonical)
            continue

        source_name, value = found
        counters[canonical] = {
            "source": source_name,
            "value": value,
            "ok": value == 0.0,
        }
        if value != 0.0:
            failures.append(canonical)

    if require_all:
        failures.extend(f"missing:{name}" for name in missing)

    return {
        "path": str(path),
        "ok": not failures,
        "counters": counters,
        "missing": missing,
        "failures": failures,
    }


def write_markdown(path: Path, reports: list[dict[str, Any]]) -> None:
    lines = [
        "# Select-Server Baseline Counters",
        "",
        "Counters are guardrails only. Passing this check does not replace the required 1280x720 screenshot comparison.",
        "",
    ]
    for report in reports:
        lines.append(f"## {report['path']}")
        lines.append("")
        lines.append(f"- ok: **{str(report['ok']).lower()}**")
        if report["missing"]:
            lines.append(f"- missing counters: {', '.join(report['missing'])}")
        if report["failures"]:
            lines.append(f"- failures: {', '.join(report['failures'])}")
        for name, counter in sorted(report["counters"].items()):
            lines.append(f"- `{name}` from `{counter['source']}`: `{counter['value']}`")
        lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("telemetry", nargs="+", type=Path, help="Telemetry JSON files to check.")
    parser.add_argument("--json-out", type=Path, help="Optional path for a JSON report.")
    parser.add_argument("--md-out", type=Path, help="Optional path for a Markdown report.")
    parser.add_argument(
        "--require-all",
        action="store_true",
        help="Fail when any expected baseline counter is absent.",
    )
    args = parser.parse_args()

    reports = [
        check_payload(path, load_json(path), require_all=args.require_all)
        for path in args.telemetry
    ]
    ok = all(report["ok"] for report in reports)

    result = {
        "ok": ok,
        "checked_files": len(reports),
        "zero_baseline_counters": sorted(ALIASES),
        "reports": reports,
    }

    text = json.dumps(result, indent=2, sort_keys=True)
    print(text)

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(text + "\n", encoding="utf-8")
    if args.md_out:
        write_markdown(args.md_out, reports)

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
