#!/usr/bin/env python3
"""Package the complete TMProject WASM build for GitHub Pages."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


RUNTIME_FILES = (
    "startup_harness.html",
    "tmproject_startup.js",
    "tmproject_startup.wasm",
    "tmproject_startup.data",
)

INDEX_HTML = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <meta http-equiv="refresh" content="0; url=./startup_harness.html?mode=play&state=0&logical=800x600&fit=actual&fieldMode=real&autoboot=1&autostart=1" />
  <title>OpenWyd Field Preview</title>
</head>
<body>
  <p>Opening OpenWyd Field preview...</p>
</body>
</html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--link-dir", type=Path, default=Path("webclient/client-wasm/build/link"))
    parser.add_argument("--out-dir", type=Path, default=Path("webclient/client-wasm/build/pages"))
    parser.add_argument("--max-bytes", type=int, default=900 * 1024 * 1024)
    args = parser.parse_args()

    link_dir = args.link_dir.resolve()
    out_dir = args.out_dir.resolve()
    missing = [name for name in RUNTIME_FILES if not (link_dir / name).is_file()]
    if missing:
        raise SystemExit(f"missing linked runtime files: {', '.join(missing)}")

    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)

    for name in RUNTIME_FILES:
        shutil.copy2(link_dir / name, out_dir / name)
    (out_dir / "index.html").write_text(INDEX_HTML, encoding="utf-8")
    (out_dir / ".nojekyll").write_text("", encoding="utf-8")

    size = sum(path.stat().st_size for path in out_dir.rglob("*") if path.is_file())
    if size > args.max_bytes:
        raise SystemExit(f"Pages payload is {size} bytes, above the {args.max_bytes} byte safety limit")

    print(f"[pages] payload={size} bytes output={out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
