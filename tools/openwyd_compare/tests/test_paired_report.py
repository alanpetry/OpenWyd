from __future__ import annotations

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from PIL import Image

from tools.openwyd_compare.controller import main
from tools.openwyd_compare.frame_schema import new_frame_record
from tools.openwyd_compare.paired_report import (
    PairedReportError,
    report_paired_run,
)


class PairedReportTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_json(self, path: Path, value: object) -> None:
        path.write_text(
            json.dumps(value, indent=2) + "\n",
            encoding="utf-8",
        )

    def make_run(self) -> Path:
        frames: list[dict[str, object]] = []
        for frame_id in (1, 2):
            native_png = self.root / f"native-{frame_id}.png"
            wasm_png = self.root / f"wasm-{frame_id}.png"
            native_snapshot = self.root / f"native-{frame_id}.json"
            wasm_snapshot = self.root / f"wasm-{frame_id}.json"

            native = Image.new("RGBA", (4, 4), (10, 20, 30, 255))
            wasm = native.copy()
            if frame_id == 2:
                wasm.putpixel((2, 1), (14, 20, 30, 255))
            native.save(native_png)
            wasm.save(wasm_png)

            state = {"game": 7, "scene": 30004}
            random = {
                "armed": True,
                "configured_seed": 12345,
                "state": 67890 + frame_id,
                "rand_calls": frame_id,
                "srand_calls": 1,
                "last_requested_seed": 7,
            }
            self.write_json(
                native_snapshot,
                new_frame_record(
                    frame_id,
                    state=state,
                    ticks={"compare_frame": frame_id},
                    clock={"controlled_time_ms": frame_id * 16},
                    render={"capture_point": "before_Present"},
                    extensions={"native": {"random": random}},
                ),
            )
            self.write_json(
                wasm_snapshot,
                new_frame_record(
                    frame_id,
                    state=state,
                    ticks={"compare_frame": frame_id},
                    clock={"controlled_time_ms": frame_id * 16},
                    render={
                        "capture_point": "after_tick",
                        "gl_error_total": 0,
                    },
                    extensions={
                        "wasm": {
                            "random": {
                                **random,
                                "armed": 1,
                            }
                        }
                    },
                ),
            )
            frames.append(
                {
                    "frame_id": frame_id,
                    "time_ms": frame_id * 16,
                    "native_png": native_png.name,
                    "native_snapshot": native_snapshot.name,
                    "wasm_png": wasm_png.name,
                    "wasm_snapshot": wasm_snapshot.name,
                }
            )

        manifest = self.root / "paired-run.json"
        self.write_json(
            manifest,
            {
                "schema": "openwyd.paired-tick-run",
                "schema_version": 1,
                "width": 4,
                "height": 4,
                "random_seed": 12345,
                "frames": frames,
            },
        )
        return manifest

    def test_complete_report_copies_inputs_and_aggregates_every_metric(self) -> None:
        manifest = self.make_run()
        output = self.root / "report"

        report = report_paired_run(manifest, output)

        self.assertEqual(report["schema"], "openwyd.paired-comparison-report")
        self.assertEqual(report["summary"]["frame_count"], 2)
        self.assertEqual(report["summary"]["exact_frame_count"], 1)
        self.assertEqual(report["summary"]["divergent_frame_count"], 1)
        self.assertEqual(report["summary"]["first_divergent_frame_id"], 2)
        self.assertEqual(report["summary"]["total_changed_pixels"], 1)
        self.assertEqual(
            report["summary"]["total_changed_pixel_percentage"],
            3.125,
        )
        self.assertEqual(report["summary"]["wasm_gl_error_total_max"], 0)
        self.assertEqual(report["summary"]["internal_mismatch_frame_count"], 0)
        self.assertIsNone(report["summary"]["first_internal_mismatch_frame_id"])
        self.assertEqual(report["frames"][0]["metrics"]["rms_rgb"], 0.0)
        self.assertGreater(report["frames"][1]["metrics"]["rms_rgb"], 0)
        self.assertEqual(report["frames"][0]["metrics"]["ssim"]["rgb"], 1.0)
        self.assertTrue(report["frames"][0]["snapshots"]["states_equal"])
        self.assertTrue(report["frames"][0]["snapshots"]["random_equal"])
        self.assertTrue(report["frames"][0]["snapshots"]["internal_equal"])

        persisted = json.loads((output / "report.json").read_text("utf-8"))
        self.assertEqual(report, persisted)
        for frame in report["frames"]:
            for artifact in frame["artifacts"].values():
                self.assertTrue((output / artifact).is_file(), artifact)

        first = output / "frames" / "frame-00000000000000000001"
        self.assertEqual(
            (first / "directx.png").read_bytes(),
            (self.root / "native-1.png").read_bytes(),
        )
        self.assertEqual(
            (first / "webgl.snapshot.json").read_bytes(),
            (self.root / "wasm-1.json").read_bytes(),
        )

    def test_cli_report_paired_routes_through_package_entrypoint(self) -> None:
        manifest = self.make_run()
        output = self.root / "cli-report"
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            return_code = main(
                [
                    "report-paired",
                    str(manifest),
                    "--output-dir",
                    str(output),
                ]
            )
        self.assertEqual(return_code, 0)
        self.assertEqual(
            json.loads(stdout.getvalue()),
            json.loads((output / "report.json").read_text("utf-8")),
        )

    def test_snapshot_frame_mismatch_is_rejected(self) -> None:
        manifest = self.make_run()
        bad_snapshot = self.root / "native-2.json"
        record = json.loads(bad_snapshot.read_text("utf-8"))
        record["frame_id"] = 99
        self.write_json(bad_snapshot, record)

        with self.assertRaisesRegex(PairedReportError, "does not match"):
            report_paired_run(manifest, self.root / "bad-report")

    def test_snapshot_logical_positions_must_match_each_other(self) -> None:
        manifest = self.make_run()
        bad_snapshot = self.root / "wasm-1.json"
        record = json.loads(bad_snapshot.read_text("utf-8"))
        record["clock"]["controlled_time_ms"] = 17
        self.write_json(bad_snapshot, record)

        with self.assertRaisesRegex(
            PairedReportError,
            "do not identify the same logical frame",
        ):
            report_paired_run(manifest, self.root / "logical-mismatch")

    def test_snapshot_controlled_time_must_match_manifest(self) -> None:
        manifest = self.make_run()
        for name in ("native-1.json", "wasm-1.json"):
            snapshot = self.root / name
            record = json.loads(snapshot.read_text("utf-8"))
            record["clock"]["controlled_time_ms"] = 17
            self.write_json(snapshot, record)

        with self.assertRaisesRegex(
            PairedReportError,
            r"clock\.controlled_time_ms 17 does not match "
            r"paired frame time_ms 16",
        ):
            report_paired_run(manifest, self.root / "wrong-time")

    def test_snapshot_compare_frame_must_match_manifest(self) -> None:
        manifest = self.make_run()
        for name in ("native-2.json", "wasm-2.json"):
            snapshot = self.root / name
            record = json.loads(snapshot.read_text("utf-8"))
            record["ticks"]["compare_frame"] = 1
            self.write_json(snapshot, record)

        with self.assertRaisesRegex(
            PairedReportError,
            r"ticks\.compare_frame 1 does not match paired frame 2",
        ):
            report_paired_run(manifest, self.root / "wrong-logical-frame")

    def test_snapshot_logical_fields_are_required_and_typed(self) -> None:
        manifest = self.make_run()
        bad_snapshot = self.root / "native-1.json"
        record = json.loads(bad_snapshot.read_text("utf-8"))
        del record["ticks"]["compare_frame"]
        self.write_json(bad_snapshot, record)

        with self.assertRaisesRegex(
            PairedReportError,
            r"valid ticks\.compare_frame",
        ):
            report_paired_run(manifest, self.root / "missing-logical-frame")

    def test_random_divergence_is_reported_as_first_internal_mismatch(self) -> None:
        manifest = self.make_run()
        wasm_snapshot = self.root / "wasm-2.json"
        record = json.loads(wasm_snapshot.read_text("utf-8"))
        record["extensions"]["wasm"]["random"]["rand_calls"] = 99
        self.write_json(wasm_snapshot, record)

        report = report_paired_run(manifest, self.root / "random-mismatch")

        self.assertEqual(report["summary"]["internal_mismatch_frame_count"], 1)
        self.assertEqual(report["summary"]["first_internal_mismatch_frame_id"], 2)
        self.assertFalse(report["frames"][1]["snapshots"]["random_equal"])
        self.assertEqual(
            report["frames"][1]["snapshots"]["internal_mismatches"],
            ["random"],
        )

    def test_seeded_run_requires_valid_random_telemetry(self) -> None:
        manifest = self.make_run()
        native_snapshot = self.root / "native-1.json"
        record = json.loads(native_snapshot.read_text("utf-8"))
        del record["extensions"]["native"]["random"]
        self.write_json(native_snapshot, record)

        with self.assertRaisesRegex(
            PairedReportError,
            r"extensions\.native\.random",
        ):
            report_paired_run(manifest, self.root / "missing-random")

    def test_seeded_run_rejects_disarmed_random_telemetry(self) -> None:
        manifest = self.make_run()
        wasm_snapshot = self.root / "wasm-1.json"
        record = json.loads(wasm_snapshot.read_text("utf-8"))
        record["extensions"]["wasm"]["random"]["armed"] = 0
        self.write_json(wasm_snapshot, record)

        with self.assertRaisesRegex(
            PairedReportError,
            r"random generator is not armed",
        ):
            report_paired_run(manifest, self.root / "disarmed-random")

    def test_seeded_run_rejects_a_different_configured_seed(self) -> None:
        manifest = self.make_run()
        native_snapshot = self.root / "native-1.json"
        record = json.loads(native_snapshot.read_text("utf-8"))
        record["extensions"]["native"]["random"]["configured_seed"] = 54321
        self.write_json(native_snapshot, record)

        with self.assertRaisesRegex(
            PairedReportError,
            r"configured_seed 54321 does not match paired-run random_seed 12345",
        ):
            report_paired_run(manifest, self.root / "wrong-random-seed")

    def test_stale_output_and_non_monotonic_frames_are_rejected(self) -> None:
        manifest = self.make_run()
        stale = self.root / "stale"
        stale.mkdir()
        (stale / "old.txt").write_text("stale", encoding="utf-8")
        with self.assertRaisesRegex(PairedReportError, "new or empty"):
            report_paired_run(manifest, stale)

        value = json.loads(manifest.read_text("utf-8"))
        value["frames"].reverse()
        self.write_json(manifest, value)
        with self.assertRaisesRegex(PairedReportError, "strictly increasing"):
            report_paired_run(manifest, self.root / "unordered")

    def test_declared_dimensions_must_match_real_frames(self) -> None:
        manifest = self.make_run()
        value = json.loads(manifest.read_text("utf-8"))
        value["width"] = 800
        value["height"] = 600
        self.write_json(manifest, value)
        with self.assertRaisesRegex(PairedReportError, "declares 800x600"):
            report_paired_run(
                manifest,
                self.root / "wrong-size",
                target_size=(4, 4),
            )


if __name__ == "__main__":
    unittest.main()
