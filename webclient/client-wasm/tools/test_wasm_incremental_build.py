#!/usr/bin/env python3
"""Focused tests for the fast WASM developer build."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import build_tmproject_wasm_incremental as incremental
import build_wasm_asset_bundle as assets
import link_tmproject_wasm_startup as linker


class IncrementalWasmBuildTests(unittest.TestCase):
    def test_depfile_parser_accepts_windows_absolute_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            depfile = Path(temporary) / "sample.d"
            depfile.write_text(
                "C:\\repo\\sample.o: \\\n"
                " C:\\repo\\sample.cpp \\\n"
                " C:\\repo\\sample.h\n",
                encoding="utf-8",
            )
            self.assertEqual(
                incremental.parse_depfile(depfile),
                [
                    Path("C:\\repo\\sample.cpp"),
                    Path("C:\\repo\\sample.h"),
                ],
            )

    def test_failed_compile_preserves_previous_object(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source.cpp"
            source.write_text("int value = 1;\n", encoding="utf-8")
            object_path = root / "build/source.o"
            object_path.parent.mkdir()
            object_path.write_bytes(b"last-good")
            with mock.patch.object(
                incremental.subprocess,
                "run",
                return_value=subprocess.CompletedProcess(
                    args=[],
                    returncode=1,
                    stdout="",
                    stderr="compile failed",
                ),
            ):
                result = incremental.compile_source_incremental(
                    source=source,
                    repo_root=root,
                    object_path=object_path,
                    logs_dir=root / "logs",
                    optimization_flag="-O2",
                )
            self.assertFalse(result.ok)
            self.assertEqual(object_path.read_bytes(), b"last-good")

    def test_successful_compile_publishes_object_and_depfile(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source.cpp"
            source.write_text("int value = 1;\n", encoding="utf-8")
            object_path = root / "build/source.o"

            def compile_fixture(command, **_kwargs):
                output = Path(command[command.index("-o") + 1])
                depfile = Path(command[command.index("-MF") + 1])
                output.write_bytes(b"new-object")
                depfile.write_text(
                    f"{object_path}: {source}\n",
                    encoding="utf-8",
                )
                return subprocess.CompletedProcess(
                    args=command,
                    returncode=0,
                    stdout="",
                    stderr="",
                )

            with mock.patch.object(
                incremental.subprocess,
                "run",
                side_effect=compile_fixture,
            ):
                result = incremental.compile_source_incremental(
                    source=source,
                    repo_root=root,
                    object_path=object_path,
                    logs_dir=root / "logs",
                    optimization_flag="-O2",
                )
            self.assertTrue(result.ok)
            self.assertEqual(object_path.read_bytes(), b"new-object")
            self.assertTrue(incremental.dependency_path(object_path).is_file())
            self.assertTrue(incremental.command_path(object_path).is_file())

    def test_versioned_code_pair_switches_through_bootstrap(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            staging = root / "staging"
            output = root / "output"
            staging.mkdir()
            output.mkdir()
            name = "tmproject_startup.123"
            (staging / f"{name}.js").write_bytes(b"runtime")
            (staging / f"{name}.wasm").write_bytes(b"wasm")
            linker.publish_incremental_code(
                staging,
                output,
                name,
                {"fixture": True},
            )
            bootstrap = (output / "tmproject_startup.js").read_text(
                encoding="utf-8"
            )
            self.assertIn(f"./{name}.js", bootstrap)
            self.assertTrue((output / f"{name}.wasm").is_file())

    def test_asset_availability_requires_declared_pair(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            link_dir = Path(temporary)
            (link_dir / assets.BOOTSTRAP_NAME).write_text(
                "loader",
                encoding="utf-8",
            )
            (link_dir / "bundle.js").write_text("loader", encoding="utf-8")
            (link_dir / "bundle.data").write_bytes(b"data")
            (link_dir / assets.STATE_NAME).write_text(
                json.dumps(
                    {
                        "schema_version": assets.ASSET_STATE_SCHEMA_VERSION,
                        "loader": "bundle.js",
                        "data": "bundle.data",
                    }
                ),
                encoding="utf-8",
            )
            self.assertTrue(assets.bundle_is_available(link_dir))
            (link_dir / "bundle.data").unlink()
            self.assertFalse(assets.bundle_is_available(link_dir))

    def test_indexeddb_cache_is_rewritten_without_retained_chunk_arrays(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            loader = Path(temporary) / "bundle.js"
            loader.write_text(
                """        async function cacheRemotePackage(db, packageName, packageData, packageMeta) {
          var finishedChunks = 0;
          return new Promise((resolve) => resolve(packageData));
        }

        async function fetchCachedPackage(db, packageName, metadata) {
          var chunks = new Array(metadata['chunkCount']);
          return new Promise((resolve) => resolve(chunks));
        }

""",
                encoding="utf-8",
            )
            assets._use_bounded_memory_indexeddb_cache(loader)
            source = loader.read_text(encoding="utf-8")
            self.assertIn(
                "var packageData = new Uint8Array(REMOTE_PACKAGE_SIZE);",
                source,
            )
            self.assertIn("await new Promise", source)
            self.assertIn("packageData.slice(chunkStart, chunkEnd)", source)
            self.assertNotIn("finishedChunks", source)
            self.assertNotIn("var chunks = new Array", source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
