#!/usr/bin/env python3
"""Compose every rendered UI-audit panel into reviewable contact sheets."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("audit_dir", type=Path)
    parser.add_argument("--columns", type=int, default=4)
    parser.add_argument("--rows", type=int, default=4)
    args = parser.parse_args()

    audit_dir = args.audit_dir.resolve()
    report = json.loads((audit_dir / "report.json").read_text(encoding="utf-8"))
    entries: list[dict[str, object]] = []
    for scene in report.get("scenes", []):
        for candidate in scene.get("candidates", []):
            screenshot = candidate.get("screenshot")
            if screenshot and candidate.get("status") == "captured":
                entries.append({"state": scene.get("state"), **candidate})

    columns = max(1, args.columns)
    rows = max(1, args.rows)
    cell_width = 340
    cell_height = 278
    header_height = 28
    per_page = columns * rows
    output_dir = audit_dir / "contact-sheets"
    output_dir.mkdir(parents=True, exist_ok=True)
    pages: list[str] = []

    for page_index, start in enumerate(range(0, len(entries), per_page), 1):
        page = Image.new(
            "RGB",
            (columns * cell_width, rows * cell_height),
            "#181818",
        )
        draw = ImageDraw.Draw(page)
        for slot, entry in enumerate(entries[start : start + per_page]):
            left = (slot % columns) * cell_width
            top = (slot // columns) * cell_height
            label = (
                f"state={entry.get('state')} id={entry.get('id')} "
                f"depth={entry.get('depth')} findings={len(entry.get('findings', []))}"
            )
            draw.text((left + 7, top + 7), label, fill="white")
            source = Image.open(audit_dir / str(entry["screenshot"])).convert("RGB")
            source.thumbnail(
                (cell_width - 12, cell_height - header_height - 8),
                Image.Resampling.LANCZOS,
            )
            x = left + (cell_width - source.width) // 2
            y = top + header_height + (cell_height - header_height - source.height) // 2
            page.paste(source, (x, y))
            draw.rectangle(
                (left, top, left + cell_width - 1, top + cell_height - 1),
                outline="#505050",
            )
        path = output_dir / f"page-{page_index:02d}.png"
        page.save(path, optimize=True)
        pages.append(path.name)

    manifest = {
        "ok": True,
        "capturedPanels": len(entries),
        "pages": pages,
    }
    (output_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
