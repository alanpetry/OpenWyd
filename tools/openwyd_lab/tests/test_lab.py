import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

LAB_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LAB_DIR))

import lab


class PixelComparisonTests(unittest.TestCase):
    def test_reports_and_writes_amplified_difference(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native = Image.new("RGB", (2, 1), (0, 0, 0))
            wasm = Image.new("RGB", (2, 1), (0, 0, 0))
            wasm.putpixel((1, 0), (40, 20, 0))
            native.save(root / "native.png")
            wasm.save(root / "wasm.png")

            result = lab._compare_pixels(
                root / "native.png",
                root / "wasm.png",
                root / "diff.png",
            )

            self.assertTrue((root / "diff.png").is_file())
            self.assertEqual(result["max_absolute_error"], 40)
            self.assertEqual(result["pixels_over_32_percent"], 50.0)
            self.assertEqual(
                result["strong_difference_bounds"],
                {"left": 1, "top": 0, "right": 1, "bottom": 0},
            )


class ScenarioCompilerTests(unittest.TestCase):
    def test_action_route_is_ascii_and_fields_are_unambiguous(self) -> None:
        event = lab._event_bytes(
            {
                "frame": 1,
                "kind": "action",
                "actor": 0,
                "x": 2102,
                "y": 2092,
                "speed": 6,
                "route": "666666",
            }
        )
        values = lab.EVENT.unpack(event)

        self.assertEqual(values[0:7], (1, 2, 0, 0, 2102, 2092, 6))
        self.assertEqual(bytes(values[7:13]), b"666666")


if __name__ == "__main__":
    unittest.main()
