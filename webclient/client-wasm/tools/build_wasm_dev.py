#!/usr/bin/env python3
"""One-command OpenWyd WASM developer build.

``dev`` updates stale code and reuses the external asset bundle.
``verify`` forces a clean code rebuild while retaining the independent assets.
``assets`` explicitly rebuilds the large asset package.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Sequence

TOOLS_DIRECTORY = Path(__file__).resolve().parent
if str(TOOLS_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIRECTORY))

from build_wasm_asset_bundle import (  # noqa: E402
    build_asset_bundle,
)
from convert_wyt_to_png import convert_wyt_to_png  # noqa: E402


def _activate_local_emsdk(repo_root: Path) -> None:
    if not os.environ.get("EMSDK"):
        candidate = repo_root.parent / ".tools/emsdk"
        if candidate.is_dir():
            os.environ["EMSDK"] = str(candidate.resolve())


def _prepare_local_runtime_extras(repo_root: Path, link_dir: Path) -> None:
    """Keep the raw linked harness self-contained without copying MP3 files."""

    loading_source = repo_root / "v769ClientRelease/UI/newtitle.wyt"
    loading_target = link_dir / "openwyd_loading.png"
    if (
        loading_source.is_file()
        and (
            not loading_target.is_file()
            or loading_target.stat().st_mtime_ns < loading_source.stat().st_mtime_ns
        )
    ):
        convert_wyt_to_png(loading_source, loading_target)
        print(f"[wasm-dev] generated={loading_target.relative_to(repo_root)}")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "action",
        nargs="?",
        choices=("dev", "verify", "assets"),
        default="dev",
    )
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument(
        "--jobs",
        type=int,
        default=max(1, min(8, (os.cpu_count() or 4) // 2)),
    )
    parser.add_argument(
        "--force-assets",
        action="store_true",
        help="Rebuild the 500+ MiB asset package during dev/verify.",
    )
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    _activate_local_emsdk(repo_root)
    link_dir = repo_root / "webclient/client-wasm/build/link"
    manifest = (
        repo_root
        / "webclient/client-wasm/config/startup-preload-manifest.txt"
    )

    if args.action == "assets":
        build_asset_bundle(
            repo_root=repo_root,
            manifest_path=manifest,
            link_dir=link_dir,
            force=True,
        )
        return 0

    started = time.perf_counter()
    build_asset_bundle(
        repo_root=repo_root,
        manifest_path=manifest,
        link_dir=link_dir,
        force=args.force_assets,
    )

    linker = TOOLS_DIRECTORY / "link_tmproject_wasm_startup.py"
    command = [
        sys.executable,
        str(linker),
        "--repo-root",
        str(repo_root),
        "--dev",
        "--jobs",
        str(max(1, args.jobs)),
    ]
    if args.action == "verify":
        command.extend(("--rebuild", "--link-opt-level", "O2"))
    subprocess.run(command, cwd=repo_root, check=True)
    _prepare_local_runtime_extras(repo_root, link_dir)
    elapsed_ms = int((time.perf_counter() - started) * 1000)
    print(
        f"[wasm-dev] action={args.action} complete elapsed_ms={elapsed_ms}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
