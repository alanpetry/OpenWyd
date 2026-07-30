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
    bundle_is_available,
)


def _activate_local_emsdk(repo_root: Path) -> None:
    if not os.environ.get("EMSDK"):
        candidate = repo_root.parent / ".tools/emsdk"
        if candidate.is_dir():
            os.environ["EMSDK"] = str(candidate.resolve())


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
    if args.force_assets or not bundle_is_available(link_dir):
        build_asset_bundle(
            repo_root=repo_root,
            manifest_path=manifest,
            link_dir=link_dir,
            force=args.force_assets,
        )
    else:
        print("[wasm-dev] assets=reused")

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
    elapsed_ms = int((time.perf_counter() - started) * 1000)
    print(
        f"[wasm-dev] action={args.action} complete elapsed_ms={elapsed_ms}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
