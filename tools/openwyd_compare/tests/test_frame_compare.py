from __future__ import annotations

import json
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

from tools.openwyd_compare.frame_compare import compare_frame_pair


class FrameCompareTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_directory.name)

    def tearDown(self) -> None:
        self.temp_directory.cleanup()

    def save_rgba(
        self,
        name: str,
        size: tuple[int, int],
        pixels: list[tuple[int, int, int, int]],
    ) -> Path:
        path = self.root / name
        image = Image.new("RGBA", size)
        image.putdata(pixels)
        image.save(path)
        return path

    def test_identical_frames_have_exact_zero_metrics_and_ssim_one(self) -> None:
        pixels = [
            (0, 20, 40, 255),
            (80, 100, 120, 200),
            (130, 140, 150, 100),
            (255, 240, 220, 0),
        ]
        reference = self.save_rgba("reference.png", (2, 2), pixels)
        candidate = self.save_rgba("candidate.png", (2, 2), pixels)

        report = compare_frame_pair(
            reference,
            candidate,
            self.root / "output",
            frame_id=17,
        )

        metrics = report["metrics"]
        self.assertEqual(metrics["changed_pixels"], 0)
        self.assertEqual(metrics["changed_pixel_percentage"], 0.0)
        self.assertEqual(metrics["rms_rgb"], 0.0)
        self.assertEqual(metrics["rms_rgba"], 0.0)
        self.assertEqual(metrics["max_absolute_channel_delta"], 0)
        self.assertEqual(metrics["ssim"]["rgb"], 1.0)

        with Image.open(self.root / "output" / "diff.absolute.png") as diff:
            self.assertEqual(set(diff.getdata()), {(0, 0, 0, 255)})
        with Image.open(self.root / "output" / "diff.heatmap.png") as heatmap:
            self.assertEqual(set(heatmap.getdata()), {(0, 0, 0, 255)})

    def test_exact_metrics_and_threshold_include_alpha(self) -> None:
        reference = self.save_rgba(
            "reference.png",
            (2, 1),
            [(0, 0, 0, 255), (10, 20, 30, 255)],
        )
        candidate = self.save_rgba(
            "candidate.png",
            (2, 1),
            [(0, 0, 0, 255), (13, 24, 30, 250)],
        )

        report = compare_frame_pair(
            reference,
            candidate,
            self.root / "exact",
            ssim_window=1,
        )
        metrics = report["metrics"]
        self.assertEqual(metrics["changed_pixels"], 1)
        self.assertEqual(metrics["changed_pixel_percentage"], 50.0)
        self.assertAlmostEqual(
            metrics["rms_rgb"],
            math.sqrt(25 / 6),
            places=9,
        )
        self.assertEqual(metrics["rms_rgba"], 2.5)
        self.assertEqual(metrics["mean_absolute_rgb"], round(7 / 6, 10))
        self.assertEqual(metrics["mean_absolute_rgba"], 1.5)
        self.assertEqual(metrics["max_absolute_channel_delta"], 5)
        self.assertEqual(metrics["channels"]["a"]["max_absolute"], 5)

        threshold_report = compare_frame_pair(
            reference,
            candidate,
            self.root / "threshold",
            threshold=5,
            ssim_window=None,
        )
        self.assertEqual(threshold_report["metrics"]["changed_pixels"], 0)
        self.assertIsNone(threshold_report["metrics"]["ssim"])

    def test_alpha_can_be_explicitly_normalized_to_opaque(self) -> None:
        reference = self.save_rgba(
            "reference.png",
            (1, 1),
            [(20, 40, 60, 0)],
        )
        candidate = self.save_rgba(
            "candidate.png",
            (1, 1),
            [(20, 40, 60, 255)],
        )

        exact = compare_frame_pair(
            reference,
            candidate,
            self.root / "alpha-exact",
        )
        opaque = compare_frame_pair(
            reference,
            candidate,
            self.root / "alpha-opaque",
            alpha_mode="opaque",
        )

        self.assertEqual(exact["metrics"]["changed_pixels"], 1)
        self.assertEqual(exact["metrics"]["max_absolute_channel_delta"], 255)
        self.assertEqual(opaque["metrics"]["changed_pixels"], 0)
        self.assertEqual(opaque["metrics"]["rms_rgba"], 0.0)
        self.assertEqual(opaque["metrics"]["compared_channels"], "rgb")

    def test_candidate_vertical_flip_normalizes_orientation(self) -> None:
        reference = self.save_rgba(
            "reference.png",
            (1, 2),
            [(255, 0, 0, 255), (0, 0, 255, 255)],
        )
        candidate = self.save_rgba(
            "candidate.png",
            (1, 2),
            [(0, 0, 255, 255), (255, 0, 0, 255)],
        )

        report = compare_frame_pair(
            reference,
            candidate,
            self.root / "flipped",
            candidate_orientation="flip-y",
        )

        self.assertEqual(report["metrics"]["changed_pixels"], 0)
        self.assertEqual(
            report["inputs"]["candidate"]["orientation"],
            "flip-y",
        )

    def test_dimension_mismatch_is_strict_unless_policy_is_explicit(self) -> None:
        reference = self.save_rgba(
            "reference.png",
            (2, 2),
            [(255, 0, 0, 255)] * 4,
        )
        candidate = self.save_rgba(
            "candidate.png",
            (1, 1),
            [(255, 0, 0, 255)],
        )

        with self.assertRaisesRegex(ValueError, "dimensions differ"):
            compare_frame_pair(reference, candidate, self.root / "strict")

        report = compare_frame_pair(
            reference,
            candidate,
            self.root / "resized",
            size_policy="reference",
        )
        self.assertTrue(report["normalization"]["source_dimension_mismatch"])
        self.assertTrue(report["normalization"]["pre_resize_dimension_mismatch"])
        self.assertTrue(report["inputs"]["candidate"]["resized"])
        self.assertFalse(report["inputs"]["reference"]["resized"])
        self.assertEqual(report["metrics"]["changed_pixels"], 0)

    def test_report_is_deterministic_and_cli_writes_all_artifacts(self) -> None:
        reference = self.save_rgba(
            "reference.png",
            (2, 2),
            [(10, 20, 30, 255)] * 4,
        )
        candidate = self.save_rgba(
            "candidate.png",
            (2, 2),
            [(10, 20, 31, 255)] * 4,
        )
        first_output = self.root / "first"
        second_output = self.root / "second"

        first = compare_frame_pair(
            reference,
            candidate,
            first_output,
            frame_id="0000042",
        )
        second = compare_frame_pair(
            reference,
            candidate,
            second_output,
            frame_id="0000042",
        )

        self.assertEqual(first, second)
        self.assertEqual(
            (first_output / "report.json").read_bytes(),
            (second_output / "report.json").read_bytes(),
        )

        cli_output = self.root / "cli"
        result = subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.openwyd_compare",
                str(reference),
                str(candidate),
                "--frame-id",
                "42",
                "--output-dir",
                str(cli_output),
            ],
            cwd=Path(__file__).resolve().parents[3],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        stdout_report = json.loads(result.stdout)
        file_report = json.loads((cli_output / "report.json").read_text("utf-8"))
        self.assertEqual(stdout_report, file_report)
        self.assertEqual(
            set(path.name for path in cli_output.iterdir()),
            {
                "candidate.normalized.png",
                "diff.absolute.png",
                "diff.heatmap.png",
                "reference.normalized.png",
                "report.json",
            },
        )


if __name__ == "__main__":
    unittest.main()
