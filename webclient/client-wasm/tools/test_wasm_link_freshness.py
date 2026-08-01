#!/usr/bin/env python3
"""Regression tests for the content-addressed TMProject WASM object contract."""

from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).with_name("link_tmproject_wasm_startup.py")
SPEC = importlib.util.spec_from_file_location("link_tmproject_wasm_startup", SCRIPT)
assert SPEC and SPEC.loader
LINKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LINKER)
CONTRACT = LINKER.sys.modules.get("tmproject_wasm_object_contract")
if CONTRACT is None:
    import tmproject_wasm_object_contract as CONTRACT

BUILDER_SCRIPT = Path(__file__).with_name("build_tmproject_wasm_objects.py")
BUILDER_SPEC = importlib.util.spec_from_file_location(
    "build_tmproject_wasm_objects_under_test",
    BUILDER_SCRIPT,
)
assert BUILDER_SPEC and BUILDER_SPEC.loader
BUILDER = importlib.util.module_from_spec(BUILDER_SPEC)
sys.modules[BUILDER_SPEC.name] = BUILDER
BUILDER_SPEC.loader.exec_module(BUILDER)


class WasmLinkFreshnessTests(unittest.TestCase):
    @staticmethod
    def make_repo(root: Path) -> tuple[Path, Path, Path]:
        project = root / "Projects/TMProject"
        compat = root / "webclient/client-wasm/compat/include"
        directx = root / "Dependencies/Directx/Include"
        project.mkdir(parents=True)
        compat.mkdir(parents=True)
        directx.mkdir(parents=True)
        source = project / "CPSock.cpp"
        header = project / "CPSock.h"
        source.write_text('#include "CPSock.h"\n', encoding="utf-8")
        header.write_text("#define VALUE 1\n", encoding="utf-8")
        (compat / "tm_emscripten_prelude.h").write_text(
            "#pragma once\n",
            encoding="utf-8",
        )
        (directx / "d3d9.h").write_text("#pragma once\n", encoding="utf-8")
        vcxproj = project / "TMProject.vcxproj"
        vcxproj.write_text(
            (
                '<Project xmlns="http://schemas.microsoft.com/developer/'
                'msbuild/2003"><ItemGroup>'
                '<ClCompile Include="CPSock.cpp" />'
                "</ItemGroup></Project>"
            ),
            encoding="utf-8",
        )
        return vcxproj, source, header

    @classmethod
    def make_startup_link_contract_inputs(cls, root: Path) -> dict:
        vcxproj, source, _ = cls.make_repo(root)
        obj_root = root / "webclient/client-wasm/build/obj"
        tm_object = CONTRACT.object_for_source(root, obj_root, source)
        tm_object.parent.mkdir(parents=True)
        tm_object.write_bytes(b"tmproject object")
        CONTRACT.compiler_identity.cache_clear()
        CONTRACT.write_stamp(
            root,
            obj_root,
            vcxproj,
            "-O2",
            [source],
        )

        compat_src = root / "webclient/client-wasm/compat/src"
        compat_src.mkdir(parents=True)
        entry_src = compat_src / "wyd_client_entry.cpp"
        stubs_src = compat_src / "win32_emscripten_stubs.cpp"
        entry_obj = obj_root / "webclient/client-wasm/compat/src/wyd_client_entry.o"
        stubs_obj = (
            obj_root
            / "webclient/client-wasm/compat/src/win32_emscripten_stubs.o"
        )
        entry_src.write_text("int entry_source = 1;\n", encoding="utf-8")
        stubs_src.write_text("int stubs_source = 1;\n", encoding="utf-8")
        entry_obj.parent.mkdir(parents=True)
        entry_obj.write_bytes(b"entry object")
        stubs_obj.write_bytes(b"stubs object")

        asset = root / "assets/payload.bin"
        asset.parent.mkdir()
        asset.write_bytes(b"preload payload")
        preload_manifest = root / "preload.txt"
        preload_manifest.write_text(
            "assets/payload.bin@/payload.bin\n",
            encoding="utf-8",
        )
        preload_entries = LINKER.read_preload_entries(root, preload_manifest)

        rsp_path = root / "link/startup-objects.rsp"
        link_rsp_path = root / "link/startup-link.rsp.utf-8"
        rsp_path.parent.mkdir()
        rsp_path.write_text("tmproject.o\n", encoding="utf-8")
        link_rsp_path.write_text("-O2 output.js\n", encoding="utf-8")
        link_cmd = [
            "em++",
            "-O2",
            "@link/startup-objects.rsp",
            "--preload-file=assets/payload.bin@/payload.bin",
            "-o",
            "link/output.js",
        ]
        return {
            "repo_root": root,
            "obj_root": obj_root,
            "optimization_flag": "-O2",
            "tm_objects": [tm_object],
            "entry_src": entry_src,
            "stubs_src": stubs_src,
            "entry_obj": entry_obj,
            "stubs_obj": stubs_obj,
            "preload_manifest": preload_manifest,
            "preload_entries": preload_entries,
            "link_cmd": link_cmd,
            "response_files": (rsp_path, link_rsp_path),
            "asset": asset,
        }

    def test_header_content_changes_invalidate_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vcxproj, source, header = self.make_repo(root)
            obj_root = root / "webclient/client-wasm/build/obj"
            first = CONTRACT.make_stamp(
                root,
                obj_root,
                vcxproj,
                "-O2",
                [source],
            )
            header.write_text("#define VALUE 2\n", encoding="utf-8")
            second = CONTRACT.make_stamp(
                root,
                obj_root,
                vcxproj,
                "-O2",
                [source],
            )
            self.assertNotEqual(first["fingerprint"], second["fingerprint"])

    def test_optimization_and_compile_contract_change_fingerprint(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vcxproj, source, _ = self.make_repo(root)
            obj_root = root / "webclient/client-wasm/build/obj"
            optimized = CONTRACT.make_stamp(
                root, obj_root, vcxproj, "-O2", [source]
            )
            debug = CONTRACT.make_stamp(
                root, obj_root, vcxproj, "-O0", [source]
            )
            self.assertNotEqual(
                optimized["fingerprint"],
                debug["fingerprint"],
            )

    def test_compiler_identity_changes_fingerprint(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vcxproj, source, _ = self.make_repo(root)
            obj_root = root / "webclient/client-wasm/build/obj"
            original_identity = CONTRACT.compiler_identity
            try:
                CONTRACT.compiler_identity = lambda: {"version": "A"}
                first = CONTRACT.make_stamp(
                    root, obj_root, vcxproj, "-O2", [source]
                )
                CONTRACT.compiler_identity = lambda: {"version": "B"}
                second = CONTRACT.make_stamp(
                    root, obj_root, vcxproj, "-O2", [source]
                )
            finally:
                CONTRACT.compiler_identity = original_identity
            self.assertNotEqual(first["fingerprint"], second["fingerprint"])

    def test_stamp_requires_exact_sources_objects_and_fingerprint(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vcxproj, source, _ = self.make_repo(root)
            obj_root = root / "webclient/client-wasm/build/obj"
            obj = CONTRACT.object_for_source(root, obj_root, source)
            obj.parent.mkdir(parents=True)
            obj.write_bytes(b"object")
            written = CONTRACT.write_stamp(
                root, obj_root, vcxproj, "-O2", [source]
            )
            self.assertTrue(
                CONTRACT.stamp_matches(
                    CONTRACT.read_stamp(obj_root),
                    written,
                    root,
                )
            )
            source.write_text("// changed\n", encoding="utf-8")
            changed = CONTRACT.make_stamp(
                root, obj_root, vcxproj, "-O2", [source]
            )
            self.assertFalse(
                CONTRACT.stamp_matches(
                    CONTRACT.read_stamp(obj_root),
                    changed,
                    root,
                )
            )

    def test_object_content_change_invalidates_matching_input_stamp(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vcxproj, source, _ = self.make_repo(root)
            obj_root = root / "webclient/client-wasm/build/obj"
            obj = CONTRACT.object_for_source(root, obj_root, source)
            obj.parent.mkdir(parents=True)
            obj.write_bytes(b"original object")
            expected = CONTRACT.write_stamp(
                root, obj_root, vcxproj, "-O2", [source]
            )
            obj.write_bytes(b"corrupted object")
            self.assertFalse(
                CONTRACT.stamp_matches(
                    CONTRACT.read_stamp(obj_root),
                    expected,
                    root,
                )
            )

    def test_orphaned_object_is_rejected_before_link(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            _, source, _ = self.make_repo(root)
            obj_root = root / "webclient/client-wasm/build/obj"
            expected = CONTRACT.object_for_source(root, obj_root, source)
            expected.parent.mkdir(parents=True)
            expected.write_bytes(b"expected")
            (expected.parent / "RemovedSource.o").write_bytes(b"orphan")

            with self.assertRaisesRegex(
                RuntimeError,
                "orphaned TMProject objects are forbidden",
            ):
                LINKER.ensure_tmproject_object_contract(root, obj_root, "-O2")

    def test_builder_does_not_certify_contract_changed_during_compile(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vcxproj, source, _ = self.make_repo(root)
            obj_root = root / "webclient/client-wasm/build/obj"
            report_json = root / "reports/objects.json"
            report_md = root / "reports/objects.md"
            old_stamp = CONTRACT.stamp_path(obj_root)
            old_stamp.parent.mkdir(parents=True)
            old_stamp.write_text('{"stale": true}\n', encoding="utf-8")

            before = CONTRACT.make_stamp(
                root,
                obj_root,
                vcxproj,
                "-O2",
                [source],
            )
            after = dict(before)
            after["fingerprint"] = "f" * 64
            compile_result = BUILDER.CompileResult(
                source_rel="Projects/TMProject/CPSock.cpp",
                source_abs=str(source),
                object_rel=(
                    "webclient/client-wasm/build/obj/"
                    "Projects/TMProject/CPSock.o"
                ),
                ok=True,
                returncode=0,
                elapsed_ms=1,
                first_error=None,
                stderr_log="reports/CPSock.stderr.txt",
            )

            argv = [
                str(BUILDER_SCRIPT),
                "--repo-root",
                str(root),
                "--vcxproj",
                str(vcxproj),
                "--obj-root",
                str(obj_root),
                "--report-json",
                str(report_json),
                "--report-md",
                str(report_md),
                "--jobs",
                "1",
            ]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    BUILDER,
                    "capture_contract",
                    side_effect=[before, after],
                ),
                mock.patch.object(
                    BUILDER,
                    "run_compile",
                    return_value=compile_result,
                ),
            ):
                result = BUILDER.main()

            self.assertEqual(result, 2)
            self.assertFalse(old_stamp.exists())
            report = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertFalse(report["contract"]["unchanged"])
            self.assertFalse(report["contract"]["certified"])
            self.assertIn(
                "changed while compilation",
                report["contract"]["error"],
            )

    def test_builder_rechecks_contract_after_hashing_objects(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vcxproj, source, _ = self.make_repo(root)
            obj_root = root / "webclient/client-wasm/build/obj"
            report_json = root / "reports/objects.json"
            report_md = root / "reports/objects.md"
            object_path = CONTRACT.object_for_source(
                root,
                obj_root,
                source,
            )
            object_path.parent.mkdir(parents=True)
            object_path.write_bytes(b"compiled object")

            before = CONTRACT.make_stamp(
                root,
                obj_root,
                vcxproj,
                "-O2",
                [source],
            )
            changed = dict(before)
            changed["fingerprint"] = "e" * 64
            compile_result = BUILDER.CompileResult(
                source_rel="Projects/TMProject/CPSock.cpp",
                source_abs=str(source),
                object_rel=object_path.relative_to(root).as_posix(),
                ok=True,
                returncode=0,
                elapsed_ms=1,
                first_error=None,
                stderr_log="reports/CPSock.stderr.txt",
            )
            argv = [
                str(BUILDER_SCRIPT),
                "--repo-root",
                str(root),
                "--vcxproj",
                str(vcxproj),
                "--obj-root",
                str(obj_root),
                "--report-json",
                str(report_json),
                "--report-md",
                str(report_md),
                "--jobs",
                "1",
            ]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    BUILDER,
                    "capture_contract",
                    side_effect=[before, before, changed],
                ),
                mock.patch.object(
                    BUILDER,
                    "run_compile",
                    return_value=compile_result,
                ),
            ):
                result = BUILDER.main()

            self.assertEqual(result, 2)
            self.assertFalse(CONTRACT.stamp_path(obj_root).exists())
            report = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertFalse(report["contract"]["certified"])
            self.assertIn(
                "while object hashes",
                report["contract"]["error"],
            )

    def test_fresh_capture_detects_em_config_toolchain_change(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vcxproj, source, _ = self.make_repo(root)
            obj_root = root / "webclient/client-wasm/build/obj"
            em_config = root / ".emscripten"
            em_config.write_text("CACHE = 'first'\n", encoding="utf-8")

            with mock.patch.dict(
                os.environ,
                {"EM_CONFIG": str(em_config)},
                clear=False,
            ):
                first = BUILDER.capture_contract(
                    root,
                    obj_root,
                    vcxproj,
                    "-O2",
                    [source],
                )
                em_config.write_text(
                    "CACHE = 'second'\n",
                    encoding="utf-8",
                )
                second = BUILDER.capture_contract(
                    root,
                    obj_root,
                    vcxproj,
                    "-O2",
                    [source],
                )

            self.assertNotEqual(
                first["fingerprint"],
                second["fingerprint"],
            )

    def test_startup_link_contract_covers_compat_preloads_objects_and_args(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            inputs = self.make_startup_link_contract_inputs(root)
            asset = inputs.pop("asset")
            with self.subTest("captured inputs"):
                baseline = LINKER.capture_startup_link_contract(**inputs)

                inputs["entry_src"].write_text(
                    "int entry_source = 2;\n",
                    encoding="utf-8",
                )
                changed_entry = LINKER.capture_startup_link_contract(**inputs)
                self.assertNotEqual(
                    baseline["fingerprint"],
                    changed_entry["fingerprint"],
                )
                inputs["entry_src"].write_text(
                    "int entry_source = 1;\n",
                    encoding="utf-8",
                )

                asset.write_bytes(b"changed preload payload")
                changed_asset = LINKER.capture_startup_link_contract(**inputs)
                self.assertNotEqual(
                    baseline["fingerprint"],
                    changed_asset["fingerprint"],
                )
                asset.write_bytes(b"preload payload")

                inputs["entry_obj"].write_bytes(b"changed entry object")
                changed_object = LINKER.capture_startup_link_contract(**inputs)
                self.assertNotEqual(
                    baseline["fingerprint"],
                    changed_object["fingerprint"],
                )
                inputs["entry_obj"].write_bytes(b"entry object")

                changed_arguments = dict(inputs)
                changed_arguments["link_cmd"] = [
                    *inputs["link_cmd"],
                    "-sASSERTIONS=1",
                ]
                changed_command = LINKER.capture_startup_link_contract(
                    **changed_arguments
                )
                self.assertNotEqual(
                    baseline["fingerprint"],
                    changed_command["fingerprint"],
                )

                inputs["tm_objects"][0].write_bytes(b"changed TM object")
                with self.assertRaisesRegex(
                    RuntimeError,
                    "TMProject source/toolchain/object contract changed",
                ):
                    LINKER.capture_startup_link_contract(**inputs)

    def test_startup_invalidation_removes_only_known_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            link_dir = Path(temp)
            for output in LINKER.startup_output_paths(link_dir):
                output.write_bytes(b"stale")
            sentinel = link_dir / "tmproject_startup.js.map"
            sentinel.write_bytes(b"keep")
            unrelated = link_dir / "other.wasm"
            unrelated.write_bytes(b"keep")

            LINKER.invalidate_startup_outputs(link_dir)

            self.assertTrue(
                all(
                    not output.exists()
                    for output in LINKER.startup_output_paths(link_dir)
                )
            )
            self.assertEqual(sentinel.read_bytes(), b"keep")
            self.assertEqual(unrelated.read_bytes(), b"keep")

    def test_object_contract_failure_invalidates_outputs_and_reports(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_repo(root)
            link_dir = root / "link"
            link_dir.mkdir()
            for output in LINKER.startup_output_paths(link_dir):
                output.write_bytes(b"stale")
            sentinel = link_dir / "keep.txt"
            sentinel.write_text("keep", encoding="utf-8")
            report_json = root / "reports/link.json"
            report_md = root / "reports/link.md"
            argv = [
                str(SCRIPT),
                "--repo-root",
                str(root),
                "--obj-root",
                "obj",
                "--link-dir",
                str(link_dir),
                "--report-json",
                str(report_json),
                "--report-md",
                str(report_md),
            ]

            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    LINKER,
                    "ensure_tmproject_object_contract",
                    side_effect=RuntimeError("contract failed"),
                ),
            ):
                result = LINKER.main()

            self.assertEqual(result, 2)
            self.assertTrue(sentinel.is_file())
            self.assertTrue(
                all(
                    not output.exists()
                    for output in LINKER.startup_output_paths(link_dir)
                )
            )
            report = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertFalse(report["ok"])
            self.assertEqual(
                report["phase"],
                "tmproject-object-contract",
            )
            self.assertIn("contract failed", report["error"])

    def test_compile_failure_invalidates_outputs_and_reports(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_repo(root)
            link_dir = root / "link"
            link_dir.mkdir()
            for output in LINKER.startup_output_paths(link_dir):
                output.write_bytes(b"stale")
            report_json = root / "reports/link.json"
            report_md = root / "reports/link.md"
            argv = [
                str(SCRIPT),
                "--repo-root",
                str(root),
                "--obj-root",
                "obj",
                "--link-dir",
                str(link_dir),
                "--report-json",
                str(report_json),
                "--report-md",
                str(report_md),
            ]
            compile_error = subprocess.CalledProcessError(
                7,
                ["em++", "entry.cpp"],
            )

            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    LINKER,
                    "ensure_tmproject_object_contract",
                    return_value=([root / "obj/TM.o"], False),
                ),
                mock.patch.object(
                    LINKER,
                    "compile_source",
                    side_effect=compile_error,
                ),
            ):
                result = LINKER.main()

            self.assertEqual(result, 7)
            self.assertTrue(
                all(
                    not output.exists()
                    for output in LINKER.startup_output_paths(link_dir)
                )
            )
            report = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertFalse(report["ok"])
            self.assertEqual(report["returncode"], 7)
            self.assertEqual(report["phase"], "compile-entry")

    def test_link_failure_removes_partial_outputs_and_reports(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_repo(root)
            link_dir = root / "link"
            link_dir.mkdir()
            sentinel = link_dir / "keep.txt"
            sentinel.write_text("keep", encoding="utf-8")
            report_json = root / "reports/link.json"
            report_md = root / "reports/link.md"
            argv = [
                str(SCRIPT),
                "--repo-root",
                str(root),
                "--obj-root",
                "obj",
                "--link-dir",
                str(link_dir),
                "--report-json",
                str(report_json),
                "--report-md",
                str(report_md),
            ]

            def failed_link(*args, **kwargs):
                for output in LINKER.startup_output_paths(link_dir):
                    output.write_bytes(b"partial")
                return subprocess.CompletedProcess(
                    args=args[0],
                    returncode=1,
                    stdout="",
                    stderr="wasm-ld: error: undefined symbol: missing_one",
                )

            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    LINKER,
                    "ensure_tmproject_object_contract",
                    return_value=([root / "obj/TM.o"], False),
                ),
                mock.patch.object(LINKER, "compile_source"),
                mock.patch.object(
                    LINKER,
                    "read_preload_entries",
                    return_value=[],
                ),
                mock.patch.object(
                    LINKER,
                    "build_gdi_font_atlas_preload",
                    return_value=[],
                ),
                mock.patch.object(
                    LINKER,
                    "capture_startup_link_contract",
                    return_value={"fingerprint": "stable"},
                ),
                mock.patch.object(
                    LINKER.subprocess,
                    "run",
                    side_effect=failed_link,
                ),
            ):
                result = LINKER.main()

            self.assertEqual(result, 1)
            self.assertTrue(sentinel.is_file())
            self.assertTrue(
                all(
                    not output.exists()
                    for output in LINKER.startup_output_paths(link_dir)
                )
            )
            report = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertFalse(report["ok"])
            self.assertEqual(report["phase"], "link")
            self.assertEqual(report["undefined_total"], 1)

    def test_concurrent_link_input_change_invalidates_successful_outputs(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_repo(root)
            link_dir = root / "link"
            link_dir.mkdir()
            sentinel = link_dir / "keep.txt"
            sentinel.write_text("keep", encoding="utf-8")
            report_json = root / "reports/link.json"
            report_md = root / "reports/link.md"
            argv = [
                str(SCRIPT),
                "--repo-root",
                str(root),
                "--obj-root",
                "obj",
                "--link-dir",
                str(link_dir),
                "--report-json",
                str(report_json),
                "--report-md",
                str(report_md),
            ]

            def successful_link(*args, **kwargs):
                for output in LINKER.startup_output_paths(link_dir):
                    output.write_bytes(b"new output")
                return subprocess.CompletedProcess(
                    args=args[0],
                    returncode=0,
                    stdout="",
                    stderr="",
                )

            before = {"fingerprint": "a" * 64}
            after = {"fingerprint": "b" * 64}
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    LINKER,
                    "ensure_tmproject_object_contract",
                    return_value=([root / "obj/TM.o"], False),
                ),
                mock.patch.object(LINKER, "compile_source"),
                mock.patch.object(
                    LINKER,
                    "read_preload_entries",
                    return_value=[],
                ),
                mock.patch.object(
                    LINKER,
                    "build_gdi_font_atlas_preload",
                    return_value=[],
                ),
                mock.patch.object(
                    LINKER,
                    "capture_startup_link_contract",
                    side_effect=[before, after],
                ),
                mock.patch.object(
                    LINKER.subprocess,
                    "run",
                    side_effect=successful_link,
                ),
            ):
                result = LINKER.main()

            self.assertEqual(result, 2)
            self.assertTrue(sentinel.is_file())
            self.assertTrue(
                all(
                    not output.exists()
                    for output in LINKER.startup_output_paths(link_dir)
                )
            )
            report = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertFalse(report["ok"])
            self.assertEqual(
                report["phase"],
                "link-input-contract-changed",
            )
            self.assertEqual(
                report["input_contract"]["before_fingerprint"],
                before["fingerprint"],
            )
            self.assertEqual(
                report["input_contract"]["after_fingerprint"],
                after["fingerprint"],
            )
            self.assertFalse(report["input_contract"]["unchanged"])

    def test_concurrent_tmproject_change_rejected_during_post_link_check(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.make_repo(root)
            link_dir = root / "link"
            link_dir.mkdir()
            sentinel = link_dir / "keep.txt"
            sentinel.write_text("keep", encoding="utf-8")
            report_json = root / "reports/link.json"
            report_md = root / "reports/link.md"
            argv = [
                str(SCRIPT),
                "--repo-root",
                str(root),
                "--obj-root",
                "obj",
                "--link-dir",
                str(link_dir),
                "--report-json",
                str(report_json),
                "--report-md",
                str(report_md),
            ]

            def successful_link(*args, **kwargs):
                for output in LINKER.startup_output_paths(link_dir):
                    output.write_bytes(b"new output")
                return subprocess.CompletedProcess(
                    args=args[0],
                    returncode=0,
                    stdout="",
                    stderr="",
                )

            before = {"fingerprint": "c" * 64}
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(
                    LINKER,
                    "ensure_tmproject_object_contract",
                    return_value=([root / "obj/TM.o"], False),
                ),
                mock.patch.object(LINKER, "compile_source"),
                mock.patch.object(
                    LINKER,
                    "read_preload_entries",
                    return_value=[],
                ),
                mock.patch.object(
                    LINKER,
                    "build_gdi_font_atlas_preload",
                    return_value=[],
                ),
                mock.patch.object(
                    LINKER,
                    "capture_startup_link_contract",
                    side_effect=[
                        before,
                        RuntimeError("TMProject object changed during link"),
                    ],
                ),
                mock.patch.object(
                    LINKER.subprocess,
                    "run",
                    side_effect=successful_link,
                ),
            ):
                result = LINKER.main()

            self.assertEqual(result, 2)
            self.assertTrue(sentinel.is_file())
            self.assertTrue(
                all(
                    not output.exists()
                    for output in LINKER.startup_output_paths(link_dir)
                )
            )
            report = json.loads(report_json.read_text(encoding="utf-8"))
            self.assertFalse(report["ok"])
            self.assertEqual(
                report["phase"],
                "link-input-contract-changed",
            )
            self.assertIsNone(
                report["input_contract"]["after_fingerprint"]
            )
            self.assertIn(
                "post-link input revalidation failed",
                report["error"],
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
