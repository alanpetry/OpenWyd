#!/usr/bin/env python3
"""Package the complete TMProject WASM build for GitHub Pages."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

from convert_wyt_to_png import convert_wyt_to_png


STATIC_RUNTIME_FILES = (
    "startup_harness.html",
    "tmproject_startup.js",
    "tmproject_startup.state.json",
    "openwyd_assets.js",
    "openwyd_assets.state.json",
)
LOADING_ART_NAME = "openwyd_loading.png"

INDEX_HTML = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>OpenWyd</title>
  <script>
    (() => {
      const resolutionKey = 'openwyd.displayResolution.v1';
      const fitKey = 'openwyd.displayFit.v1';
      const displayModeKey = 'openwyd.displayMode.v2';
      const validResolutions = new Set([
        '640x480', '800x600', '1024x768', '1280x1024', '1600x1200'
      ]);
      const incoming = new URLSearchParams(window.location.search);
      let resolution = incoming.get('logical') || '';
      let fit = incoming.get('fit') || '';
      let displayMode = incoming.get('displayMode') || incoming.get('display') || '';
      try {
        if (!validResolutions.has(resolution)) {
          resolution = window.localStorage.getItem(resolutionKey) || '';
        }
        if (fit !== 'actual' && fit !== 'contain') {
          fit = window.localStorage.getItem(fitKey) || '';
        }
        if (displayMode !== 'optimized' && displayMode !== 'legacy') {
          displayMode = window.localStorage.getItem(displayModeKey) || '';
        }
      } catch {
        // Storage is optional; the official 800x600 default remains available.
      }
      if (!validResolutions.has(resolution)) resolution = '800x600';
      if (fit !== 'actual' && fit !== 'contain') fit = 'actual';
      if (displayMode !== 'optimized' && displayMode !== 'legacy') displayMode = 'optimized';

      const target = new URL('./startup_harness.html', window.location.href);
      const params = target.searchParams;
      params.set('v', '4');
      params.set('mode', 'play');
      params.set('demo', '1');
      params.set('state', '7');
      params.set('logical', resolution);
      params.set('fit', fit);
      params.set('displayMode', displayMode);
      params.set('fieldMode', 'real');
      params.set('autoboot', '1');
      params.set('autostart', '1');
      window.location.replace(target.toString());
    })();
  </script>
  <noscript>
    <meta http-equiv="refresh" content="0; url=./startup_harness.html?v=4&amp;mode=play&amp;demo=1&amp;state=7&amp;displayMode=optimized&amp;logical=800x600&amp;fit=actual&amp;fieldMode=real&amp;autoboot=1&amp;autostart=1" />
  </noscript>
</head>
<body>
  <p>Opening OpenWyd...</p>
</body>
</html>
"""

def _read_runtime_name(state_path: Path, key: str) -> str:
    state = json.loads(state_path.read_text(encoding="utf-8"))
    name = state.get(key)
    if not isinstance(name, str) or not name or Path(name).name != name:
        raise SystemExit(f"{state_path.name} has invalid {key!r}")
    return name


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--link-dir", type=Path, default=Path("webclient/client-wasm/build/link"))
    parser.add_argument("--out-dir", type=Path, default=Path("webclient/client-wasm/build/pages"))
    parser.add_argument("--max-bytes", type=int, default=900 * 1024 * 1024)
    parser.add_argument(
        "--loading-art",
        type=Path,
        help="Prebuilt official loading art; defaults to UI/newtitle.wyt",
    )
    args = parser.parse_args()

    link_dir = args.link_dir.resolve()
    out_dir = args.out_dir.resolve()
    startup_state = link_dir / "tmproject_startup.state.json"
    asset_state = link_dir / "openwyd_assets.state.json"
    runtime_files = {
        *STATIC_RUNTIME_FILES,
        _read_runtime_name(startup_state, "javascript"),
        _read_runtime_name(startup_state, "wasm"),
        _read_runtime_name(asset_state, "loader"),
        _read_runtime_name(asset_state, "data"),
    }
    missing = [
        name for name in sorted(runtime_files)
        if not (link_dir / name).is_file()
    ]
    if missing:
        raise SystemExit(f"missing linked runtime files: {', '.join(missing)}")

    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)

    for name in sorted(runtime_files):
        destination = out_dir / name
        shutil.copyfile(link_dir / name, destination)
        destination.chmod(0o644)
        for suffix in (".gz", ".br"):
            compressed = link_dir / f"{name}{suffix}"
            if compressed.is_file():
                compressed_destination = out_dir / compressed.name
                shutil.copyfile(compressed, compressed_destination)
                compressed_destination.chmod(0o644)
    if args.loading_art:
        loading_art = args.loading_art.resolve()
        if not loading_art.is_file():
            raise SystemExit(f"missing loading art: {loading_art}")
        shutil.copyfile(loading_art, out_dir / LOADING_ART_NAME)
    else:
        repo_root = link_dir.parents[3]
        loading_source = repo_root / "v769ClientRelease/UI/newtitle.wyt"
        if not loading_source.is_file():
            raise SystemExit(f"missing official loading art: {loading_source}")
        convert_wyt_to_png(loading_source, out_dir / LOADING_ART_NAME)
    (out_dir / "index.html").write_text(INDEX_HTML, encoding="utf-8")
    (out_dir / ".nojekyll").write_text("", encoding="utf-8")

    size = sum(path.stat().st_size for path in out_dir.rglob("*") if path.is_file())
    if size > args.max_bytes:
        raise SystemExit(f"Pages payload is {size} bytes, above the {args.max_bytes} byte safety limit")

    print(f"[pages] payload={size} bytes output={out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
