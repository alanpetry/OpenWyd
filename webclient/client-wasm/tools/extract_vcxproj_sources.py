#!/usr/bin/env python3
"""Extract ClCompile source files from a Visual Studio .vcxproj."""

from __future__ import annotations

import argparse
import json
import xml.etree.ElementTree as ET
from pathlib import Path

NS = {"msb": "http://schemas.microsoft.com/developer/msbuild/2003"}


def extract_sources(vcxproj: Path) -> list[dict[str, str]]:
    root = ET.parse(vcxproj).getroot()
    out: list[dict[str, str]] = []

    for node in root.findall(".//msb:ClCompile", NS):
        include = node.attrib.get("Include")
        if not include:
            continue

        src = (vcxproj.parent / include).resolve()
        out.append(
            {
                "include": include.replace("\\", "/"),
                "absolute": str(src),
                "exists": "true" if src.exists() else "false",
            }
        )

    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--vcxproj",
        default="Projects/TMProject/TMProject.vcxproj",
        type=Path,
        help="Path to TMProject.vcxproj",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("webclient/client-wasm/build/reports/tmproject-sources.json"),
        help="Output JSON path",
    )
    args = parser.parse_args()

    sources = extract_sources(args.vcxproj)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({"count": len(sources), "sources": sources}, indent=2), encoding="utf-8")

    print(f"[extract] vcxproj={args.vcxproj}")
    print(f"[extract] count={len(sources)}")
    print(f"[extract] out={args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
