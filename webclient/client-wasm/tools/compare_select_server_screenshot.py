#!/usr/bin/env python3
"""Compare a select-server screenshot against the official visual reference.

This helper intentionally reports simple image-distance metrics and optional
diff output. It is a guardrail for screenshot review, not a replacement for
human visual comparison against the original client.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

from PIL import Image, ImageChops, ImageStat


EXPECTED_SIZE = (1280, 720)


@dataclass(frozen=True)
class ChannelStats:
    red: float
    green: float
    blue: float

    @property
    def max_channel(self) -> float:
        return max(self.red, self.green, self.blue)


@dataclass(frozen=True)
class CompareResult:
    reference: str
    actual: str
    size: list[int]
    mean_absolute_error: ChannelStats
    root_mean_square_error: ChannelStats
    normalized_mean_error: float
    normalized_rms_error: float
    changed_pixels: int
    changed_pixel_ratio: float
    pass_thresholds: bool


def _load_rgb(path: Path) -> Image.Image:
    if not path.is_file():
        raise FileNotFoundError(f"image not found: {path}")
    with Image.open(path) as image:
        return image.convert("RGB")


def _channel_stats(values: list[float]) -> ChannelStats:
    red, green, blue = values[:3]
    return ChannelStats(red=red, green=green, blue=blue)


def _changed_pixels(diff: Image.Image) -> int:
    mask = diff.convert("L").point(lambda value: 255 if value else 0)
    return int(ImageStat.Stat(mask).sum[0] / 255)


def compare_images(
    reference_path: Path,
    actual_path: Path,
    *,
    max_mean_error: float,
    max_rms_error: float,
    require_size: tuple[int, int],
    diff_path: Path | None,
) -> CompareResult:
    reference = _load_rgb(reference_path)
    actual = _load_rgb(actual_path)

    if reference.size != require_size:
        raise ValueError(
            f"reference must be {require_size[0]}x{require_size[1]}, got "
            f"{reference.size[0]}x{reference.size[1]}"
        )
    if actual.size != reference.size:
        raise ValueError(
            f"actual screenshot size {actual.size[0]}x{actual.size[1]} does "
            f"not match reference {reference.size[0]}x{reference.size[1]}"
        )

    diff = ImageChops.difference(reference, actual)
    stat = ImageStat.Stat(diff)
    mean_error = _channel_stats(stat.mean)
    rms_error = _channel_stats(stat.rms)
    changed_pixels = _changed_pixels(diff)
    pixel_count = reference.size[0] * reference.size[1]

    if diff_path:
        diff_path.parent.mkdir(parents=True, exist_ok=True)
        diff.save(diff_path)

    normalized_mean = mean_error.max_channel / 255.0
    normalized_rms = rms_error.max_channel / 255.0
    return CompareResult(
        reference=str(reference_path),
        actual=str(actual_path),
        size=[reference.size[0], reference.size[1]],
        mean_absolute_error=mean_error,
        root_mean_square_error=rms_error,
        normalized_mean_error=normalized_mean,
        normalized_rms_error=normalized_rms,
        changed_pixels=changed_pixels,
        changed_pixel_ratio=changed_pixels / pixel_count,
        pass_thresholds=(
            normalized_mean <= max_mean_error
            and normalized_rms <= max_rms_error
        ),
    )


def _print_text(result: CompareResult) -> None:
    print(f"reference: {result.reference}")
    print(f"actual: {result.actual}")
    print(f"size: {result.size[0]}x{result.size[1]}")
    print(
        "mean_absolute_error_rgb: "
        f"{result.mean_absolute_error.red:.3f}, "
        f"{result.mean_absolute_error.green:.3f}, "
        f"{result.mean_absolute_error.blue:.3f}"
    )
    print(
        "root_mean_square_error_rgb: "
        f"{result.root_mean_square_error.red:.3f}, "
        f"{result.root_mean_square_error.green:.3f}, "
        f"{result.root_mean_square_error.blue:.3f}"
    )
    print(f"normalized_mean_error: {result.normalized_mean_error:.6f}")
    print(f"normalized_rms_error: {result.normalized_rms_error:.6f}")
    print(
        "changed_pixels: "
        f"{result.changed_pixels} ({result.changed_pixel_ratio:.6%})"
    )
    print(f"pass_thresholds: {str(result.pass_thresholds).lower()}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare a 1280x720 select-server screenshot against the official "
            "reference image. Metrics are for smoke gating only; still inspect "
            "the screenshot visually."
        )
    )
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--actual", required=True, type=Path)
    parser.add_argument(
        "--diff-out",
        type=Path,
        help="Optional PNG path for the raw RGB difference image.",
    )
    parser.add_argument(
        "--max-mean-error",
        type=float,
        default=0.030,
        help="Maximum normalized per-channel mean error before failure.",
    )
    parser.add_argument(
        "--max-rms-error",
        type=float,
        default=0.090,
        help="Maximum normalized per-channel RMS error before failure.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Print machine-readable JSON instead of text.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        result = compare_images(
            args.reference,
            args.actual,
            max_mean_error=args.max_mean_error,
            max_rms_error=args.max_rms_error,
            require_size=EXPECTED_SIZE,
            diff_path=args.diff_out,
        )
    except (FileNotFoundError, OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(asdict(result), indent=2, sort_keys=True))
    else:
        _print_text(result)
    return 0 if result.pass_thresholds else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
