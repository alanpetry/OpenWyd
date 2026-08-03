#!/usr/bin/env python3
"""Build the large OpenWyd asset package independently from the WASM link."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
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
from preload_manifest import read_preload_entries  # noqa: E402


ASSET_STATE_SCHEMA_VERSION = 4
BOOTSTRAP_NAME = "openwyd_assets.js"
STATE_NAME = "openwyd_assets.state.json"
HASH_CACHE_SCHEMA_VERSION = 1


def _use_single_buffer_streaming_loader(loader_path: Path) -> None:
    """Avoid file_packager's two-copy peak for very large monolithic data.

    Emscripten 6 collects every response chunk and then allocates a second
    contiguous buffer. The OpenWyd package is over 500 MB, so that transiently
    requires more than 1 GB before the WASM heap and WebGL allocations. Fill
    the final, exactly-sized package buffer as chunks arrive instead. The
    package is still fetched and mounted in full before the client boots.
    """

    source = loader_path.read_text(encoding="utf-8")
    original = """        const chunks = [];
        const headers = response.headers;
        const total = Number(headers.get('Content-Length') || packageSize);
        let loaded = 0;

        Module['setStatus'] && Module['setStatus']('Downloading data...');
        const reader = response.body.getReader();

        while (1) {
          var {done, value} = await reader.read();
          if (done) break;
          chunks.push(value);
          loaded += value.length;
          Module['dataFileDownloads'][packageName] = {loaded, total};

          let totalLoaded = 0;
          let totalSize = 0;

          for (const download of Object.values(Module['dataFileDownloads'])) {
            totalLoaded += download.loaded;
            totalSize += download.total;
          }

          Module['setStatus'] && Module['setStatus'](`Downloading data... (${totalLoaded}/${totalSize})`);
        }

        const packageData = new Uint8Array(chunks.map((c) => c.length).reduce((a, b) => a + b, 0));
        let offset = 0;
        for (const chunk of chunks) {
          packageData.set(chunk, offset);
          offset += chunk.length;
        }
        return packageData.buffer;
"""
    replacement = """        const headers = response.headers;
        const total = Number(headers.get('Content-Length') || packageSize);
        let loaded = 0;

        Module['setStatus'] && Module['setStatus']('Downloading data...');
        if (!response.body) return response.arrayBuffer();
        const reader = response.body.getReader();
        const packageData = new Uint8Array(packageSize);

        while (1) {
          var {done, value} = await reader.read();
          if (done) break;
          if (loaded + value.length > packageData.length) {
            throw new Error(`Asset package exceeded declared size: ${packageName}`);
          }
          packageData.set(value, loaded);
          loaded += value.length;
          Module['dataFileDownloads'][packageName] = {loaded, total};

          let totalLoaded = 0;
          let totalSize = 0;

          for (const download of Object.values(Module['dataFileDownloads'])) {
            totalLoaded += download.loaded;
            totalSize += download.total;
          }

          Module['setStatus'] && Module['setStatus'](`Downloading data... (${totalLoaded}/${totalSize})`);
        }

        if (loaded !== packageData.length) {
          throw new Error(`Asset package size mismatch: ${loaded}/${packageData.length}`);
        }
        return packageData.buffer;
"""
    if source.count(original) != 1:
        raise RuntimeError(
            "unsupported Emscripten file_packager streaming loader; "
            "expected exactly one chunk-concatenation block"
        )
    loader_path.write_text(
        source.replace(original, replacement),
        encoding="utf-8",
        newline="\n",
    )


def _use_bounded_memory_indexeddb_cache(loader_path: Path) -> None:
    """Keep the monolithic package in IndexedDB without full-size copies.

    Emscripten's default cache starts every 64 MB write/read concurrently.  On
    a 500+ MB package that retains all chunk copies and, on reads, allocates a
    second complete buffer for concatenation.  Process one chunk at a time and
    copy it directly into the final buffer.  The package remains monolithic:
    it is still completely restored and mounted before the client starts.
    """

    source = loader_path.read_text(encoding="utf-8")
    cache_pattern = re.compile(
        r"^        async function cacheRemotePackage\([\s\S]*?"
        r"^        \}\r?\n\r?\n",
        re.MULTILINE,
    )
    fetch_pattern = re.compile(
        r"^        async function fetchCachedPackage\([\s\S]*?"
        r"^        \}\r?\n\r?\n",
        re.MULTILINE,
    )
    fallback_pattern = re.compile(
        r"^        async function preloadFallback\(error\) \{[\s\S]*?"
        r"^        \}\r?\n\r?\n",
        re.MULTILINE,
    )
    cache_replacement = """        async function cacheRemotePackage(db, packageName, packageData, packageMeta) {
          var chunkCount = Math.ceil(packageData.byteLength / CHUNK_SIZE);

          for (var chunkId = 0; chunkId < chunkCount; chunkId++) {
            var chunkStart = chunkId * CHUNK_SIZE;
            var chunkEnd = Math.min(packageData.byteLength, chunkStart + CHUNK_SIZE);
            var chunk = packageData.slice(chunkStart, chunkEnd);
            await new Promise((resolve, reject) => {
              var transaction = db.transaction([PACKAGE_STORE_NAME], IDB_RW);
              var packages = transaction.objectStore(PACKAGE_STORE_NAME);
              var request = packages.put(chunk, `package/${packageName}/${chunkId}`);
              request.onerror = reject;
              transaction.oncomplete = resolve;
              transaction.onerror = reject;
              transaction.onabort = reject;
            });
            chunk = undefined;
          }

          await new Promise((resolve, reject) => {
            var transaction = db.transaction([METADATA_STORE_NAME], IDB_RW);
            var metadata = transaction.objectStore(METADATA_STORE_NAME);
            var request = metadata.put(
              {'uuid': packageMeta.uuid, 'chunkCount': chunkCount},
              `metadata/${packageName}`
            );
            request.onerror = reject;
            transaction.oncomplete = resolve;
            transaction.onerror = reject;
            transaction.onabort = reject;
          });
          return packageData;
        }

        async function deleteCachedPackage(db, packageName, packageMeta) {
          await new Promise((resolve, reject) => {
            var transaction = db.transaction(
              [PACKAGE_STORE_NAME, METADATA_STORE_NAME],
              IDB_RW
            );
            var packages = transaction.objectStore(PACKAGE_STORE_NAME);
            var metadata = transaction.objectStore(METADATA_STORE_NAME);
            var chunkCount = Number(packageMeta && packageMeta['chunkCount']) || 0;
            for (var chunkId = 0; chunkId < chunkCount; chunkId++) {
              packages.delete(`package/${packageName}/${chunkId}`);
            }
            metadata.delete(`metadata/${packageName}`);
            transaction.oncomplete = resolve;
            transaction.onerror = reject;
            transaction.onabort = reject;
          });
        }

"""
    fetch_replacement = """        async function fetchCachedPackage(db, packageName, metadata) {
          var chunkCount = metadata['chunkCount'];
          var packageData = new Uint8Array(REMOTE_PACKAGE_SIZE);
          var byteOffset = 0;

          for (var chunkId = 0; chunkId < chunkCount; chunkId++) {
            var buffer = await new Promise((resolve, reject) => {
              var transaction = db.transaction([PACKAGE_STORE_NAME], IDB_RO);
              var packages = transaction.objectStore(PACKAGE_STORE_NAME);
              var request = packages.get(`package/${packageName}/${chunkId}`);
              request.onsuccess = (event) => {
                if (!event.target.result) {
                  reject(`CachedPackageNotFound for: ${packageName}`);
                } else {
                  resolve(event.target.result);
                }
              };
              request.onerror = reject;
              transaction.onabort = reject;
            });
            var chunk = new Uint8Array(buffer);
            if (byteOffset + chunk.byteLength > packageData.byteLength) {
              throw new Error(`Cached package exceeded declared size: ${packageName}`);
            }
            packageData.set(chunk, byteOffset);
            byteOffset += chunk.byteLength;
            chunk = undefined;
            buffer = undefined;
          }

          if (byteOffset != packageData.byteLength) {
            throw new Error(
              `Cached package size mismatch: ${byteOffset}/${packageData.byteLength}`
            );
          }
          return packageData.buffer;
        }

"""
    fallback_replacement = """        async function preloadFallback(error) {
          console.warn('Asset cache miss or incomplete entry; repairing it.', error);
          var packageData = await fetchRemotePackage(REMOTE_PACKAGE_NAME, REMOTE_PACKAGE_SIZE);
          if (db) {
            try {
              await deleteCachedPackage(db, PACKAGE_PATH + PACKAGE_NAME, pkgMetadata);
              processPackageData(await cacheRemotePackage(
                db,
                PACKAGE_PATH + PACKAGE_NAME,
                packageData,
                {uuid:PACKAGE_UUID}
              ));
              return;
            } catch (cacheError) {
              console.warn('Unable to repair the asset cache; continuing in memory.', cacheError);
            }
          }
          processPackageData(packageData);
        }

"""
    source, cache_count = cache_pattern.subn(cache_replacement, source)
    source, fetch_count = fetch_pattern.subn(fetch_replacement, source)
    source, fallback_count = fallback_pattern.subn(
        fallback_replacement,
        source,
    )
    if cache_count != 1 or fetch_count != 1 or fallback_count != 1:
        raise RuntimeError(
            "unsupported Emscripten IndexedDB cache loader; expected one "
            "cache, fetch, and fallback function, got "
            f"{cache_count}/{fetch_count}/{fallback_count}"
        )
    loader_path.write_text(source, encoding="utf-8", newline="\n")


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


def _asset_content_sha256(
    repo_root: Path,
    entries: Sequence[str],
    packager: Path,
    *,
    cache_path: Path | None = None,
    trust_cache: bool = True,
) -> str:
    """Hash the exact virtual filesystem payload and its destination names."""

    digest = hashlib.sha256()
    digest.update(b"openwyd-wasm-assets-v2\0")
    for tool in (Path(__file__).resolve(), packager.resolve()):
        digest.update(tool.name.encode("utf-8"))
        with tool.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    cached_files: dict[str, object] = {}
    if trust_cache and cache_path is not None:
        try:
            cached = json.loads(cache_path.read_text(encoding="utf-8"))
            if cached.get("schema_version") == HASH_CACHE_SCHEMA_VERSION:
                cached_files = cached.get("files", {})
        except (OSError, json.JSONDecodeError, AttributeError):
            pass
    updated_files: dict[str, object] = {}
    file_digests: dict[Path, str] = {}
    for entry in entries:
        source, separator, destination = entry.partition("@")
        source_path = Path(source)
        if not source_path.is_absolute():
            source_path = repo_root / source_path
        source_path = source_path.resolve()
        try:
            cache_key = source_path.relative_to(repo_root).as_posix()
        except ValueError:
            cache_key = source_path.as_posix()
        stat = source_path.stat()
        cached_file = cached_files.get(cache_key)
        file_sha256 = None
        if isinstance(cached_file, dict):
            candidate = cached_file.get("sha256")
            if (
                cached_file.get("size") == stat.st_size
                and cached_file.get("mtime_ns") == stat.st_mtime_ns
                and isinstance(candidate, str)
                and len(candidate) == 64
            ):
                file_sha256 = candidate
        if file_sha256 is None:
            file_sha256 = file_digests.get(source_path)
        if file_sha256 is None:
            file_digest = hashlib.sha256()
            with source_path.open("rb") as stream:
                for chunk in iter(
                    lambda: stream.read(4 * 1024 * 1024),
                    b"",
                ):
                    file_digest.update(chunk)
            file_sha256 = file_digest.hexdigest()
        file_digests[source_path] = file_sha256
        updated_files[cache_key] = {
            "size": stat.st_size,
            "mtime_ns": stat.st_mtime_ns,
            "sha256": file_sha256,
        }
        virtual_path = destination if separator else source_path.name
        encoded_path = virtual_path.replace("\\", "/").encode("utf-8")
        digest.update(len(encoded_path).to_bytes(4, "little"))
        digest.update(encoded_path)
        digest.update(stat.st_size.to_bytes(8, "little"))
        digest.update(bytes.fromhex(file_sha256))
    if cache_path is not None:
        _atomic_write_json(
            cache_path,
            {
                "schema_version": HASH_CACHE_SCHEMA_VERSION,
                "files": updated_files,
            },
        )
    return digest.hexdigest()


def bundle_is_available(
    link_dir: Path,
    *,
    content_sha256: str | None = None,
) -> bool:
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
        and state.get("schema_version") == ASSET_STATE_SCHEMA_VERSION
        and (
            content_sha256 is None
            or state.get("content_sha256") == content_sha256
        )
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
    entries = [
        *read_preload_entries(repo_root, manifest_path),
        *_gdi_entries(repo_root),
    ]
    packager = _file_packager(repo_root)
    content_sha256 = _asset_content_sha256(
        repo_root,
        entries,
        packager,
        cache_path=(
            repo_root
            / "webclient/client-wasm/build/cache/asset-content-hashes.json"
        ),
        trust_cache=not force,
    )
    if (
        bundle_is_available(
            link_dir,
            content_sha256=content_sha256,
        )
        and not force
    ):
        state = json.loads(
            (link_dir / STATE_NAME).read_text(encoding="utf-8")
        )
        print(
            f"[wasm-assets] reused data={state['data']} "
            f"files={state.get('file_count', 'unknown')}"
        )
        return state

    build_id = content_sha256
    data_name = f"openwyd_assets.{build_id}.data"
    loader_name = f"openwyd_assets.{build_id}.js"
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
        _use_single_buffer_streaming_loader(staged_loader)
        _use_bounded_memory_indexeddb_cache(staged_loader)
        os.replace(staged_data, link_dir / data_name)
        os.replace(staged_loader, link_dir / loader_name)

    data_path = link_dir / data_name
    bootstrap = (
        "/* Generated atomically by build_wasm_asset_bundle.py. */\n"
        f"window.__openwydAssetDataBytes = {data_path.stat().st_size};\n"
        "document.write("
        f"'<script src=\"./{loader_name}\"><\\/script>'"
        ");\n"
    )
    _atomic_write_text(link_dir / BOOTSTRAP_NAME, bootstrap)
    elapsed_ms = int((time.perf_counter() - started) * 1000)
    state: dict[str, object] = {
        "schema_version": ASSET_STATE_SCHEMA_VERSION,
        "content_sha256": content_sha256,
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
