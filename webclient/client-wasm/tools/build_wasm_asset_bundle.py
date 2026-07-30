#!/usr/bin/env python3
"""Build the large OpenWyd asset package independently from the WASM link."""

from __future__ import annotations

import argparse
import json
import os
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Sequence

TOOLS_DIRECTORY = Path(__file__).resolve().parent
if str(TOOLS_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIRECTORY))

from build_gdi_font_atlas import (  # noqa: E402
    ATLAS_NAME,
    MANIFEST_NAME,
    build_gdi_font_atlas,
)
from link_tmproject_wasm_startup import read_preload_entries  # noqa: E402


ASSET_STATE_SCHEMA_VERSION = 1
BOOTSTRAP_NAME = "openwyd_assets.js"
STATE_NAME = "openwyd_assets.state.json"


def _atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.",
        suffix=".tmp",
        dir=path.parent,
        text=True,
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(
            descriptor,
            "w",
            encoding="utf-8",
            newline="\n",
        ) as stream:
            stream.write(text)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _atomic_write_json(path: Path, value: object) -> None:
    _atomic_write_text(
        path,
        json.dumps(value, indent=2, sort_keys=True) + "\n",
    )


def _file_packager(repo_root: Path) -> Path:
    override = os.environ.get("OPENWYD_FILE_PACKAGER")
    candidates = [
        Path(override).expanduser() if override else None,
        (
            Path(os.environ["EMSDK"]).expanduser()
            / "upstream/emscripten/tools/file_packager.py"
            if os.environ.get("EMSDK")
            else None
        ),
        (
            repo_root.parent
            / ".tools/emsdk/upstream/emscripten/tools/file_packager.py"
        ),
    ]
    for candidate in candidates:
        if candidate is not None and candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(
        "file_packager.py not found; activate emsdk or set "
        "OPENWYD_FILE_PACKAGER"
    )


def _gdi_entries(repo_root: Path) -> list[str]:
    output_dir = (
        repo_root / "webclient/client-wasm/build/generated/gdi-font"
    )
    atlas = output_dir / ATLAS_NAME
    manifest = output_dir / MANIFEST_NAME
    generator_inputs = (
        repo_root
        / "webclient/client-wasm/tools/generate_gdi_font_atlas.cpp",
        repo_root
        / "webclient/client-wasm/tools/build_gdi_font_atlas.py",
    )
    newest_input = max(path.stat().st_mtime_ns for path in generator_inputs)
    if (
        not atlas.is_file()
        or not manifest.is_file()
        or atlas.stat().st_mtime_ns < newest_input
        or manifest.stat().st_mtime_ns < newest_input
    ):
        atlas, manifest = build_gdi_font_atlas(repo_root, output_dir)
    return [
        f"{atlas.relative_to(repo_root).as_posix()}@/OpenWydGdiFontAtlas.bin",
        (
            f"{manifest.relative_to(repo_root).as_posix()}"
            "@/OpenWydGdiFontAtlas.json"
        ),
    ]


def _absolute_packager_entry(repo_root: Path, entry: str) -> str:
    source, separator, destination = entry.partition("@")
    source_path = Path(source)
    if not source_path.is_absolute():
        source_path = repo_root / source_path
    escaped_source = str(source_path.resolve()).replace("@", "@@")
    if not separator:
        return escaped_source
    return f"{escaped_source}@{destination}"


def bundle_is_available(link_dir: Path) -> bool:
    bootstrap = link_dir / BOOTSTRAP_NAME
    state_path = link_dir / STATE_NAME
    try:
        state = json.loads(state_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    loader = state.get("loader")
    data = state.get("data")
    return (
        bootstrap.is_file()
        and isinstance(loader, str)
        and isinstance(data, str)
        and (link_dir / loader).is_file()
        and (link_dir / data).is_file()
    )


def build_asset_bundle(
    *,
    repo_root: Path,
    manifest_path: Path,
    link_dir: Path,
    force: bool,
) -> dict[str, object]:
    repo_root = repo_root.resolve()
    manifest_path = manifest_path.resolve()
    link_dir = link_dir.resolve()
    link_dir.mkdir(parents=True, exist_ok=True)
    if bundle_is_available(link_dir) and not force:
        state = json.loads(
            (link_dir / STATE_NAME).read_text(encoding="utf-8")
        )
        print(
            f"[wasm-assets] reused data={state['data']} "
            f"files={state.get('file_count', 'unknown')}"
        )
        return state

    entries = [
        *read_preload_entries(repo_root, manifest_path),
        *_gdi_entries(repo_root),
    ]
    build_id = str(time.time_ns())
    data_name = f"openwyd_assets.{build_id}.data"
    loader_name = f"openwyd_assets.{build_id}.js"
    packager = _file_packager(repo_root)
    started = time.perf_counter()
    with tempfile.TemporaryDirectory(
        prefix=".openwyd-assets-",
        dir=link_dir,
    ) as temporary_name:
        staging = Path(temporary_name)
        response = staging / "file-packager.rsp.utf-8"
        response_arguments = [
            data_name,
            "--preload",
            *[
                _absolute_packager_entry(repo_root, entry)
                for entry in entries
            ],
            f"--js-output={loader_name}",
            "--use-preload-cache",
            "--indexedDB-name=OPENWYD_PRELOAD_CACHE",
            "--no-node",
            "--quiet",
        ]
        response.write_text(
            shlex.join(response_arguments) + "\n",
            encoding="utf-8",
        )
        subprocess.run(
            [sys.executable, str(packager), f"@{response}"],
            cwd=staging,
            check=True,
        )
        staged_data = staging / data_name
        staged_loader = staging / loader_name
        if not staged_data.is_file() or not staged_loader.is_file():
            raise RuntimeError(
                "file_packager completed without producing data and loader"
            )
        os.replace(staged_data, link_dir / data_name)
        os.replace(staged_loader, link_dir / loader_name)

    bootstrap = (
        "/* Generated atomically by build_wasm_asset_bundle.py. */\n"
        "document.write("
        f"'<script src=\"./{loader_name}\"><\\/script>'"
        ");\n"
    )
    _atomic_write_text(link_dir / BOOTSTRAP_NAME, bootstrap)
    elapsed_ms = int((time.perf_counter() - started) * 1000)
    data_path = link_dir / data_name
    state: dict[str, object] = {
        "schema_version": ASSET_STATE_SCHEMA_VERSION,
        "manifest": manifest_path.relative_to(repo_root).as_posix(),
        "loader": loader_name,
        "data": data_name,
        "file_count": len(entries),
        "data_bytes": data_path.stat().st_size,
        "elapsed_ms": elapsed_ms,
    }
    _atomic_write_json(link_dir / STATE_NAME, state)
    print(
        f"[wasm-assets] built files={len(entries)} "
        f"bytes={state['data_bytes']} elapsed_ms={elapsed_ms}"
    )
    return state


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(
            "webclient/client-wasm/config/startup-preload-manifest.txt"
        ),
    )
    parser.add_argument(
        "--link-dir",
        type=Path,
        default=Path("webclient/client-wasm/build/link"),
    )
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    build_asset_bundle(
        repo_root=repo_root,
        manifest_path=(repo_root / args.manifest).resolve(),
        link_dir=(repo_root / args.link_dir).resolve(),
        force=args.force,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
