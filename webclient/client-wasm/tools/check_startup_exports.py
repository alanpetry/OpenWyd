#!/usr/bin/env python3
"""Audit startup WASM exports used by select-server validation harnesses.

The startup linker keeps Emscripten's EXPORTED_FUNCTIONS list in
link_tmproject_wasm_startup.py.  Select-server visual parity checks depend on
debug/telemetry functions being callable from Playwright, so this script fails
when selected C exports exist in the source tree but are missing from the
startup export list.
"""

from __future__ import annotations

import argparse
import ast
import json
import re
from pathlib import Path

DEFAULT_PREFIXES = (
    "wyd_selserver_human_",
)

EXPORT_RE = re.compile(
    r'extern\s+"C"\s+(?:[A-Za-z_][\w:<>*&\s]*?\s+)+'
    r'(?P<name>[A-Za-z_]\w*)\s*\(',
    re.MULTILINE,
)
EXPORTED_FUNCTIONS_RE = re.compile(
    r'-sEXPORTED_FUNCTIONS=(?P<value>\[[^\]]*\])',
    re.DOTALL,
)
EXTRA_EXPORTS_RE = re.compile(
    r'extra_exports\s*=\s*(?P<value>\[[^\]]*\])',
    re.DOTALL,
)


def parse_python_string_list(value: str, *, source: str) -> list[str]:
    try:
        parsed = ast.literal_eval(value)
    except (SyntaxError, ValueError) as exc:
        raise ValueError(f"could not parse {source} as a Python string list: {exc}") from exc

    if not isinstance(parsed, list) or not all(isinstance(item, str) for item in parsed):
        raise ValueError(f"{source} must be a Python string list")
    return parsed


def exported_functions(linker_text: str) -> set[str]:
    exports: set[str] = set()

    main_match = EXPORTED_FUNCTIONS_RE.search(linker_text)
    if not main_match:
        raise ValueError("could not find -sEXPORTED_FUNCTIONS list in linker script")
    exports.update(parse_python_string_list(main_match.group("value"), source="EXPORTED_FUNCTIONS"))

    extra_match = EXTRA_EXPORTS_RE.search(linker_text)
    if extra_match:
        exports.update(parse_python_string_list(extra_match.group("value"), source="extra_exports"))

    return exports


def source_exports(source_text: str, prefixes: tuple[str, ...]) -> list[str]:
    names: list[str] = []
    for match in EXPORT_RE.finditer(source_text):
        name = match.group("name")
        if any(name.startswith(prefix) for prefix in prefixes):
            names.append(name)
    return sorted(set(names))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument(
        "--source",
        type=Path,
        default=Path("Projects/TMProject/TMSelectServerScene.cpp"),
        help="C++ source containing select-server telemetry externs.",
    )
    parser.add_argument(
        "--linker",
        type=Path,
        default=Path("webclient/client-wasm/tools/link_tmproject_wasm_startup.py"),
        help="Startup linker script containing EXPORTED_FUNCTIONS.",
    )
    parser.add_argument(
        "--prefix",
        action="append",
        dest="prefixes",
        help="Required C export prefix. May be repeated.",
    )
    parser.add_argument("--json-out", type=Path, help="Optional path for machine-readable audit output.")
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    source_path = (repo_root / args.source).resolve()
    linker_path = (repo_root / args.linker).resolve()
    prefixes = tuple(args.prefixes or DEFAULT_PREFIXES)

    source_text = source_path.read_text(encoding="utf-8")
    linker_text = linker_path.read_text(encoding="utf-8")

    required = source_exports(source_text, prefixes)
    exports = exported_functions(linker_text)
    missing = [name for name in required if f"_{name}" not in exports]

    payload = {
        "ok": not missing,
        "source": str(source_path.relative_to(repo_root)),
        "linker": str(linker_path.relative_to(repo_root)),
        "prefixes": prefixes,
        "required_count": len(required),
        "missing_count": len(missing),
        "missing": missing,
    }

    if args.json_out:
        json_out = (repo_root / args.json_out).resolve()
        json_out.parent.mkdir(parents=True, exist_ok=True)
        json_out.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    print(f"source: {payload['source']}")
    print(f"linker: {payload['linker']}")
    print(f"required exports: {len(required)}")
    if missing:
        print(f"missing startup exports: {len(missing)}")
        for name in missing:
            print(f"- {name}")
        return 1

    print("all required startup exports are present")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
