#!/usr/bin/env python3
"""Compare a select-server screenshot against the official visual reference.

The tool is intentionally small and dependency-light so scheduled migration
runs can attach a concrete visual-diff artifact before investigating deeper
D3D/WebGL behavior.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageStat


EXPECTED_WIDTH = 1280
EXPECTED_HEIGHT = 720


def load_rgb(path: Path) -> Image.Image:
    if not path.exists():
        raise FileNotFoundError(path)
    return Image.open(path).convert("RGB")


def root_mean_square(diff: Image.Image) -> float:
    stat = ImageStat.Stat(diff)
    squared = sum(value * value for value in stat.rms) / len(stat.rms)
    return math.sqrt(squared)


def mean_absolute_error(diff: Image.Image) -> float:
    stat = ImageStat.Stat(diff)
    return sum(stat.mean) / len(stat.mean)


def changed_pixel_ratio(diff: Image.Image, threshold: int) -> float:
    gray = diff.convert("L")
    changed = 0
    total = gray.width * gray.height
    pixel_values = gray.get_flattened_data() if hasattr(gray, "get_flattened_data") else gray.getdata()
    for value in pixel_values:
        if value > threshold:
            changed += 1
    return changed / total if total else 0.0


def write_diff_image(diff: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    diff.save(path)


def compare(reference_path: Path, current_path: Path, diff_path: Path | None, threshold: int) -> dict[str, object]:
    reference = load_rgb(reference_path)
    current = load_rgb(current_path)

    same_size = reference.size == current.size
    current_expected_size = current.size == (EXPECTED_WIDTH, EXPECTED_HEIGHT)
    reference_expected_size = reference.size == (EXPECTED_WIDTH, EXPECTED_HEIGHT)

    metrics: dict[str, object] = {
        "reference": str(reference_path),
        "current": str(current_path),
        "reference_size": {"width": reference.width, "height": reference.height},
        "current_size": {"width": current.width, "height": current.height},
        "expected_size": {"width": EXPECTED_WIDTH, "height": EXPECTED_HEIGHT},
        "same_size": same_size,
        "reference_expected_size": reference_expected_size,
        "current_expected_size": current_expected_size,
    }

    if not same_size:
        metrics.update(
            {
                "ok": False,
                "reason": "image sizes differ; capture the harness at logical 1280x720 before comparing pixels",
            }
        )
        return metrics

    diff = ImageChops.difference(reference, current)
    bbox = diff.getbbox()
    metrics.update(
        {
            "ok": current_expected_size and reference_expected_size,
            "rmse": root_mean_square(diff),
            "mae": mean_absolute_error(diff),
            "changed_pixel_ratio": changed_pixel_ratio(diff, threshold),
            "diff_bbox": None
            if bbox is None
            else {"left": bbox[0], "top": bbox[1], "right": bbox[2], "bottom": bbox[3]},
            "threshold": threshold,
        }
    )

    if diff_path:
        write_diff_image(diff, diff_path)
        metrics["diff_image"] = str(diff_path)

    return metrics


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True, help="Official select-server reference screenshot.")
    parser.add_argument("--current", type=Path, required=True, help="Current WASM select-server screenshot.")
    parser.add_argument(
        "--diff",
        type=Path,
        default=None,
        help="Optional output path for an absolute pixel-difference PNG.",
    )
    parser.add_argument(
        "--json",
        type=Path,
        default=None,
        help="Optional output path for machine-readable metrics.",
    )
    parser.add_argument(
        "--threshold",
        type=int,
        default=8,
        help="Per-pixel luma threshold used for changed_pixel_ratio.",
    )
    args = parser.parse_args()

    metrics = compare(args.reference, args.current, args.diff, args.threshold)
    text = json.dumps(metrics, indent=2, sort_keys=True)

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(text + "\n", encoding="utf-8")
    print(text)

    return 0 if metrics.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
