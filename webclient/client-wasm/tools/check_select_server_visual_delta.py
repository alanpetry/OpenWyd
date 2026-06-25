#!/usr/bin/env python3
"""Compare select-server screenshots against the official visual reference.

This tool is intentionally small and dependency-light. It does not decide
whether parity is correct by itself; it gives each visual smoke run a stable
set of numbers and optional diff image so humans can compare the current
WASM capture against the original client reference at logical 1280x720.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Sequence

from PIL import Image, ImageChops, ImageStat


EXPECTED_WIDTH = 1280
EXPECTED_HEIGHT = 720


@dataclass(frozen=True)
class ImageShape:
    path: str
    width: int
    height: int
    aspect: float


@dataclass(frozen=True)
class VisualDelta:
    reference: ImageShape
    actual: ImageShape
    mean_absolute_error: float
    root_mean_square_error: float
    max_channel_delta: int
    changed_pixel_ratio: float
    threshold_pixel_ratio: float
    threshold: int


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare a WASM select-server screenshot with the official "
            "1280x720 reference and report reproducible image-delta metrics."
        )
    )
    parser.add_argument("--reference", required=True, help="Official select-server JPG/PNG reference.")
    parser.add_argument("--actual", required=True, help="Current WASM select-server screenshot.")
    parser.add_argument(
        "--expected-width",
        type=int,
        default=EXPECTED_WIDTH,
        help=f"Required logical capture width. Defaults to {EXPECTED_WIDTH}.",
    )
    parser.add_argument(
        "--expected-height",
        type=int,
        default=EXPECTED_HEIGHT,
        help=f"Required logical capture height. Defaults to {EXPECTED_HEIGHT}.",
    )
    parser.add_argument(
        "--threshold",
        type=int,
        default=24,
        help="Per-channel delta threshold used for changed-pixel counting.",
    )
    parser.add_argument("--json-out", help="Optional path for a JSON metrics report.")
    parser.add_argument("--diff-out", help="Optional path for an amplified RGB diff image.")
    parser.add_argument(
        "--allow-size-mismatch",
        action="store_true",
        help="Resize the actual capture to the reference size before comparing.",
    )
    return parser.parse_args(argv)


def load_rgb(path: Path) -> Image.Image:
    try:
        return Image.open(path).convert("RGB")
    except FileNotFoundError as exc:
        raise SystemExit(f"missing image: {path}") from exc
    except OSError as exc:
        raise SystemExit(f"cannot open image {path}: {exc}") from exc


def shape_for(path: Path, image: Image.Image) -> ImageShape:
    width, height = image.size
    return ImageShape(
        path=str(path),
        width=width,
        height=height,
        aspect=width / height if height else 0.0,
    )


def require_expected_shape(name: str, shape: ImageShape, expected_width: int, expected_height: int) -> None:
    if shape.width != expected_width or shape.height != expected_height:
        raise SystemExit(
            f"{name} is {shape.width}x{shape.height}; expected "
            f"{expected_width}x{expected_height} logical select-server capture"
        )


def count_threshold_pixels(diff: Image.Image, threshold: int) -> int:
    pixels = diff.load()
    width, height = diff.size
    changed = 0
    for y in range(height):
        for x in range(width):
            if max(pixels[x, y]) > threshold:
                changed += 1
    return changed


def count_changed_pixels(diff: Image.Image) -> int:
    pixels = diff.load()
    width, height = diff.size
    changed = 0
    for y in range(height):
        for x in range(width):
            if pixels[x, y] != (0, 0, 0):
                changed += 1
    return changed


def build_delta(
    reference_path: Path,
    actual_path: Path,
    expected_width: int,
    expected_height: int,
    threshold: int,
    allow_size_mismatch: bool,
) -> tuple[VisualDelta, Image.Image]:
    reference = load_rgb(reference_path)
    actual = load_rgb(actual_path)
    reference_shape = shape_for(reference_path, reference)
    actual_shape = shape_for(actual_path, actual)

    require_expected_shape("reference", reference_shape, expected_width, expected_height)
    if actual.size != reference.size:
        if not allow_size_mismatch:
            require_expected_shape("actual", actual_shape, expected_width, expected_height)
        actual = actual.resize(reference.size, Image.Resampling.BICUBIC)

    diff = ImageChops.difference(reference, actual)
    stat = ImageStat.Stat(diff)
    mean_absolute_error = sum(stat.mean) / len(stat.mean)
    square_sum = sum((value * value) for value in stat.rms)
    root_mean_square_error = math.sqrt(square_sum / len(stat.rms))
    max_channel_delta = max(channel_max[1] for channel_max in stat.extrema)
    pixel_count = reference.width * reference.height
    changed_pixels = count_changed_pixels(diff)
    threshold_pixels = count_threshold_pixels(diff, threshold)

    delta = VisualDelta(
        reference=reference_shape,
        actual=actual_shape,
        mean_absolute_error=mean_absolute_error,
        root_mean_square_error=root_mean_square_error,
        max_channel_delta=max_channel_delta,
        changed_pixel_ratio=changed_pixels / pixel_count if pixel_count else 0.0,
        threshold_pixel_ratio=threshold_pixels / pixel_count if pixel_count else 0.0,
        threshold=threshold,
    )
    return delta, diff


def save_amplified_diff(diff: Image.Image, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    diff.point(lambda value: min(255, value * 4)).save(output_path)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    delta, diff = build_delta(
        reference_path=Path(args.reference),
        actual_path=Path(args.actual),
        expected_width=args.expected_width,
        expected_height=args.expected_height,
        threshold=args.threshold,
        allow_size_mismatch=args.allow_size_mismatch,
    )

    payload = asdict(delta)
    print(json.dumps(payload, indent=2, sort_keys=True))

    if args.json_out:
        json_path = Path(args.json_out)
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.diff_out:
        save_amplified_diff(diff, Path(args.diff_out))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
