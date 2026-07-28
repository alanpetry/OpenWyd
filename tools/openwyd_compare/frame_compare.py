"""Deterministic comparison of paired DirectX and WebGL frame captures."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from pathlib import Path
from typing import Any, Sequence

from PIL import Image, UnidentifiedImageError


SCHEMA = "openwyd.frame-comparison"
SCHEMA_VERSION = 1
REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ARTIFACT_ROOT = REPO_ROOT / "artifacts" / "openwyd_compare"

ORIENTATIONS = {
    "identity": None,
    "flip-x": Image.Transpose.FLIP_LEFT_RIGHT,
    "flip-y": Image.Transpose.FLIP_TOP_BOTTOM,
    "rotate-90-cw": Image.Transpose.ROTATE_270,
    "rotate-90-ccw": Image.Transpose.ROTATE_90,
    "rotate-180": Image.Transpose.ROTATE_180,
}

RESIZE_FILTERS = {
    "nearest": Image.Resampling.NEAREST,
    "bilinear": Image.Resampling.BILINEAR,
    "bicubic": Image.Resampling.BICUBIC,
    "lanczos": Image.Resampling.LANCZOS,
}

CHANNEL_NAMES = ("r", "g", "b", "a")


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _normalized_pixel_sha256(image: Image.Image) -> str:
    digest = hashlib.sha256()
    digest.update(image.width.to_bytes(8, "big"))
    digest.update(image.height.to_bytes(8, "big"))
    digest.update(b"RGBA")
    digest.update(image.tobytes())
    return digest.hexdigest()


def _round_metric(value: float) -> float:
    rounded = round(float(value), 10)
    return 0.0 if rounded == 0 else rounded


def _parse_size(value: str) -> tuple[int, int]:
    match = re.fullmatch(r"\s*(\d+)[xX](\d+)\s*", value)
    if not match:
        raise argparse.ArgumentTypeError("size must use WIDTHxHEIGHT, for example 800x600")
    width, height = (int(part) for part in match.groups())
    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("size dimensions must be positive")
    return width, height


def _safe_frame_directory_name(frame_id: str) -> str:
    if re.fullmatch(r"\d+", frame_id):
        return f"frame-{int(frame_id):08d}"
    safe = re.sub(r"[^A-Za-z0-9._-]+", "-", frame_id).strip("-")
    if not safe:
        raise ValueError("frame id must contain at least one letter or digit")
    return f"frame-{safe[:96]}"


def _load_rgba(
    path: Path,
    orientation: str,
    alpha_mode: str,
) -> tuple[Image.Image, dict[str, Any]]:
    with Image.open(path) as source:
        source.load()
        original_mode = source.mode
        original_size = source.size
        image = source.convert("RGBA")

    transpose = ORIENTATIONS[orientation]
    if transpose is not None:
        image = image.transpose(transpose)

    if alpha_mode == "opaque":
        image.putalpha(255)

    metadata = {
        "file": path.name,
        "sha256": _sha256_file(path),
        "original_mode": original_mode,
        "original_size": {
            "width": original_size[0],
            "height": original_size[1],
        },
        "orientation": orientation,
        "oriented_size": {
            "width": image.width,
            "height": image.height,
        },
    }
    return image, metadata


def _normalize_dimensions(
    reference: Image.Image,
    candidate: Image.Image,
    size_policy: str,
    target_size: tuple[int, int] | None,
    resize_filter: str,
) -> tuple[Image.Image, Image.Image, tuple[int, int], bool, bool]:
    if target_size is not None:
        normalized_size = target_size
    elif size_policy == "strict":
        if reference.size != candidate.size:
            raise ValueError(
                "frame dimensions differ under strict policy: "
                f"reference={reference.width}x{reference.height}, "
                f"candidate={candidate.width}x{candidate.height}"
            )
        normalized_size = reference.size
    elif size_policy == "reference":
        normalized_size = reference.size
    elif size_policy == "candidate":
        normalized_size = candidate.size
    else:
        raise ValueError(f"unsupported size policy: {size_policy}")

    resampling = RESIZE_FILTERS[resize_filter]
    reference_resized = reference.size != normalized_size
    candidate_resized = candidate.size != normalized_size
    if reference_resized:
        reference = reference.resize(normalized_size, resample=resampling)
    if candidate_resized:
        candidate = candidate.resize(normalized_size, resample=resampling)

    return (
        reference,
        candidate,
        normalized_size,
        reference_resized,
        candidate_resized,
    )


def _windowed_ssim_rgb(
    reference_bytes: bytes,
    candidate_bytes: bytes,
    width: int,
    height: int,
    window_size: int,
) -> dict[str, Any]:
    block_columns = math.ceil(width / window_size)
    block_rows = math.ceil(height / window_size)
    block_count = block_columns * block_rows
    accumulator_size = block_count * 3

    counts = [0] * block_count
    sum_reference = [0] * accumulator_size
    sum_candidate = [0] * accumulator_size
    square_reference = [0] * accumulator_size
    square_candidate = [0] * accumulator_size
    products = [0] * accumulator_size

    for y in range(height):
        block_row = y // window_size
        for x in range(width):
            block_index = block_row * block_columns + (x // window_size)
            counts[block_index] += 1
            pixel_index = (y * width + x) * 4
            channel_base = block_index * 3
            for channel in range(3):
                reference_value = reference_bytes[pixel_index + channel]
                candidate_value = candidate_bytes[pixel_index + channel]
                accumulator_index = channel_base + channel
                sum_reference[accumulator_index] += reference_value
                sum_candidate[accumulator_index] += candidate_value
                square_reference[accumulator_index] += reference_value * reference_value
                square_candidate[accumulator_index] += candidate_value * candidate_value
                products[accumulator_index] += reference_value * candidate_value

    c1 = (0.01 * 255.0) ** 2
    c2 = (0.03 * 255.0) ** 2
    channel_totals = [0.0, 0.0, 0.0]

    for block_index, count in enumerate(counts):
        channel_base = block_index * 3
        for channel in range(3):
            index = channel_base + channel
            mean_reference = sum_reference[index] / count
            mean_candidate = sum_candidate[index] / count
            variance_reference = max(
                0.0,
                square_reference[index] / count - mean_reference * mean_reference,
            )
            variance_candidate = max(
                0.0,
                square_candidate[index] / count - mean_candidate * mean_candidate,
            )
            covariance = (
                products[index] / count - mean_reference * mean_candidate
            )

            numerator = (
                (2.0 * mean_reference * mean_candidate + c1)
                * (2.0 * covariance + c2)
            )
            denominator = (
                (mean_reference * mean_reference + mean_candidate * mean_candidate + c1)
                * (variance_reference + variance_candidate + c2)
            )
            value = numerator / denominator
            channel_totals[channel] += max(-1.0, min(1.0, value))

    channel_scores = [
        channel_total / block_count for channel_total in channel_totals
    ]
    return {
        "rgb": _round_metric(sum(channel_scores) / 3.0),
        "channels": {
            "r": _round_metric(channel_scores[0]),
            "g": _round_metric(channel_scores[1]),
            "b": _round_metric(channel_scores[2]),
        },
        "window_size": window_size,
        "window_count": block_count,
        "method": "mean_nonoverlapping_window_ssim",
        "dynamic_range": 255,
        "alpha_included": False,
    }


def _heatmap_color(delta: int, gain: float) -> tuple[int, int, int, int]:
    if delta <= 0:
        return 0, 0, 0, 255

    scaled = min(255.0, delta * gain)
    position = scaled / 255.0
    anchors = (
        (0.0, (0, 0, 0)),
        (0.25, (0, 0, 255)),
        (0.5, (0, 255, 255)),
        (0.75, (255, 255, 0)),
        (1.0, (255, 0, 0)),
    )
    for index in range(1, len(anchors)):
        right_position, right_color = anchors[index]
        if position <= right_position:
            left_position, left_color = anchors[index - 1]
            amount = (position - left_position) / (right_position - left_position)
            color = tuple(
                int(round(left + (right - left) * amount))
                for left, right in zip(left_color, right_color)
            )
            return color[0], color[1], color[2], 255
    return 255, 0, 0, 255


def _calculate_metrics_and_images(
    reference: Image.Image,
    candidate: Image.Image,
    threshold: int,
    alpha_mode: str,
    heatmap_gain: float,
    ssim_window: int | None,
) -> tuple[dict[str, Any], Image.Image, Image.Image]:
    reference_bytes = reference.tobytes()
    candidate_bytes = candidate.tobytes()
    pixel_count = reference.width * reference.height
    compared_channel_count = 4 if alpha_mode == "compare" else 3

    channel_sum_abs = [0, 0, 0, 0]
    channel_sum_square = [0, 0, 0, 0]
    channel_max = [0, 0, 0, 0]
    changed_pixels = 0
    absolute_pixels = bytearray(pixel_count * 4)
    heatmap_pixels = bytearray(pixel_count * 4)

    for pixel_index in range(pixel_count):
        byte_index = pixel_index * 4
        deltas = [
            abs(
                reference_bytes[byte_index + channel]
                - candidate_bytes[byte_index + channel]
            )
            for channel in range(4)
        ]

        for channel, delta in enumerate(deltas):
            channel_sum_abs[channel] += delta
            channel_sum_square[channel] += delta * delta
            channel_max[channel] = max(channel_max[channel], delta)

        compared_delta = max(deltas[:compared_channel_count])
        if compared_delta > threshold:
            changed_pixels += 1

        absolute_pixels[byte_index] = deltas[0]
        absolute_pixels[byte_index + 1] = deltas[1]
        absolute_pixels[byte_index + 2] = deltas[2]
        absolute_pixels[byte_index + 3] = 255
        heatmap_pixels[byte_index : byte_index + 4] = bytes(
            _heatmap_color(compared_delta, heatmap_gain)
        )

    rgb_sum_abs = sum(channel_sum_abs[:3])
    rgba_sum_abs = sum(channel_sum_abs)
    rgb_sum_square = sum(channel_sum_square[:3])
    rgba_sum_square = sum(channel_sum_square)
    metrics: dict[str, Any] = {
        "pixel_count": pixel_count,
        "changed_pixels": changed_pixels,
        "changed_pixel_percentage": _round_metric(
            changed_pixels * 100.0 / pixel_count
        ),
        "threshold": threshold,
        "threshold_rule": "pixel_changed_when_max_compared_channel_delta_gt_threshold",
        "compared_channels": "rgba" if compared_channel_count == 4 else "rgb",
        "mean_absolute_rgb": _round_metric(rgb_sum_abs / (pixel_count * 3)),
        "mean_absolute_rgba": _round_metric(rgba_sum_abs / (pixel_count * 4)),
        "rms_rgb": _round_metric(math.sqrt(rgb_sum_square / (pixel_count * 3))),
        "rms_rgba": _round_metric(math.sqrt(rgba_sum_square / (pixel_count * 4))),
        "max_absolute_channel_delta": max(
            channel_max[:compared_channel_count]
        ),
        "channels": {
            name: {
                "mean_absolute": _round_metric(
                    channel_sum_abs[index] / pixel_count
                ),
                "rms": _round_metric(
                    math.sqrt(channel_sum_square[index] / pixel_count)
                ),
                "max_absolute": channel_max[index],
            }
            for index, name in enumerate(CHANNEL_NAMES)
        },
    }

    if ssim_window is not None:
        metrics["ssim"] = _windowed_ssim_rgb(
            reference_bytes,
            candidate_bytes,
            reference.width,
            reference.height,
            ssim_window,
        )
    else:
        metrics["ssim"] = None

    absolute_image = Image.frombytes(
        "RGBA",
        reference.size,
        bytes(absolute_pixels),
    )
    heatmap_image = Image.frombytes(
        "RGBA",
        reference.size,
        bytes(heatmap_pixels),
    )
    return metrics, absolute_image, heatmap_image


def _save_png(image: Image.Image, path: Path) -> None:
    image.save(path, format="PNG", optimize=False, compress_level=9)


def _write_report(report: dict[str, Any], path: Path) -> str:
    encoded = json.dumps(
        report,
        ensure_ascii=False,
        indent=2,
        sort_keys=True,
        allow_nan=False,
    )
    encoded += "\n"
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(encoded)
    return encoded


def compare_frame_pair(
    reference_path: str | Path,
    candidate_path: str | Path,
    output_dir: str | Path,
    *,
    frame_id: str | int = "0",
    reference_orientation: str = "identity",
    candidate_orientation: str = "identity",
    size_policy: str = "strict",
    target_size: tuple[int, int] | None = None,
    resize_filter: str = "nearest",
    alpha_mode: str = "compare",
    threshold: int = 0,
    heatmap_gain: float = 4.0,
    ssim_window: int | None = 8,
) -> dict[str, Any]:
    """Compare two PNG frames and write deterministic image/JSON artifacts."""

    reference_path = Path(reference_path)
    candidate_path = Path(candidate_path)
    output_dir = Path(output_dir)
    frame_id = str(frame_id)

    if reference_orientation not in ORIENTATIONS:
        raise ValueError(f"unsupported reference orientation: {reference_orientation}")
    if candidate_orientation not in ORIENTATIONS:
        raise ValueError(f"unsupported candidate orientation: {candidate_orientation}")
    if size_policy not in {"strict", "reference", "candidate"}:
        raise ValueError(f"unsupported size policy: {size_policy}")
    if resize_filter not in RESIZE_FILTERS:
        raise ValueError(f"unsupported resize filter: {resize_filter}")
    if alpha_mode not in {"compare", "opaque"}:
        raise ValueError(f"unsupported alpha mode: {alpha_mode}")
    if not 0 <= threshold <= 255:
        raise ValueError("threshold must be between 0 and 255")
    if not math.isfinite(heatmap_gain) or heatmap_gain <= 0:
        raise ValueError("heatmap gain must be a finite number greater than zero")
    if ssim_window is not None and ssim_window <= 0:
        raise ValueError("SSIM window must be greater than zero")
    if target_size is not None and (
        len(target_size) != 2 or target_size[0] <= 0 or target_size[1] <= 0
    ):
        raise ValueError("target size dimensions must be positive")

    reference, reference_metadata = _load_rgba(
        reference_path,
        reference_orientation,
        alpha_mode,
    )
    candidate, candidate_metadata = _load_rgba(
        candidate_path,
        candidate_orientation,
        alpha_mode,
    )
    source_dimension_mismatch = (
        reference_metadata["original_size"] != candidate_metadata["original_size"]
    )
    pre_resize_dimension_mismatch = reference.size != candidate.size

    (
        reference,
        candidate,
        normalized_size,
        reference_resized,
        candidate_resized,
    ) = _normalize_dimensions(
        reference,
        candidate,
        size_policy,
        target_size,
        resize_filter,
    )

    metrics, absolute_image, heatmap_image = _calculate_metrics_and_images(
        reference,
        candidate,
        threshold,
        alpha_mode,
        heatmap_gain,
        ssim_window,
    )

    output_dir.mkdir(parents=True, exist_ok=True)
    artifact_names = {
        "reference_normalized": "reference.normalized.png",
        "candidate_normalized": "candidate.normalized.png",
        "absolute_diff": "diff.absolute.png",
        "heatmap": "diff.heatmap.png",
        "report": "report.json",
    }
    _save_png(reference, output_dir / artifact_names["reference_normalized"])
    _save_png(candidate, output_dir / artifact_names["candidate_normalized"])
    _save_png(absolute_image, output_dir / artifact_names["absolute_diff"])
    _save_png(heatmap_image, output_dir / artifact_names["heatmap"])

    reference_metadata["normalized_pixel_sha256"] = _normalized_pixel_sha256(
        reference
    )
    reference_metadata["resized"] = reference_resized
    candidate_metadata["normalized_pixel_sha256"] = _normalized_pixel_sha256(
        candidate
    )
    candidate_metadata["resized"] = candidate_resized

    report: dict[str, Any] = {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "frame_id": frame_id,
        "inputs": {
            "reference": reference_metadata,
            "candidate": candidate_metadata,
        },
        "normalization": {
            "pixel_format": "RGBA8",
            "alpha_mode": alpha_mode,
            "size_policy": size_policy,
            "target_size_requested": (
                {
                    "width": target_size[0],
                    "height": target_size[1],
                }
                if target_size is not None
                else None
            ),
            "normalized_size": {
                "width": normalized_size[0],
                "height": normalized_size[1],
            },
            "effective_size_policy": (
                "target" if target_size is not None else size_policy
            ),
            "resize_filter": resize_filter,
            "source_dimension_mismatch": source_dimension_mismatch,
            "pre_resize_dimension_mismatch": pre_resize_dimension_mismatch,
        },
        "metrics": metrics,
        "visualization": {
            "absolute_diff": "absolute RGB channel delta with opaque output alpha",
            "heatmap_source": "maximum compared-channel absolute delta",
            "heatmap_gain": _round_metric(heatmap_gain),
        },
        "artifacts": artifact_names,
    }
    _write_report(report, output_dir / artifact_names["report"])
    return report


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Normalize and compare a reference DirectX PNG with a candidate "
            "WebGL PNG, producing deterministic per-frame artifacts."
        )
    )
    parser.add_argument("reference_png", type=Path)
    parser.add_argument("candidate_png", type=Path)
    parser.add_argument(
        "--frame-id",
        default="0",
        help="common monotonic frame identifier (default: 0)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        help=(
            "artifact directory (default: "
            "artifacts/openwyd_compare/frame-<frame-id>)"
        ),
    )
    parser.add_argument(
        "--reference-orientation",
        choices=ORIENTATIONS,
        default="identity",
    )
    parser.add_argument(
        "--candidate-orientation",
        choices=ORIENTATIONS,
        default="identity",
    )
    parser.add_argument(
        "--size-policy",
        choices=("strict", "reference", "candidate"),
        default="strict",
        help="strict fails on mismatch; other modes resize to the selected frame",
    )
    parser.add_argument(
        "--target-size",
        type=_parse_size,
        metavar="WIDTHxHEIGHT",
        help="resize both frames to an explicit size; overrides --size-policy",
    )
    parser.add_argument(
        "--resize-filter",
        choices=RESIZE_FILTERS,
        default="nearest",
    )
    parser.add_argument(
        "--alpha-mode",
        choices=("compare", "opaque"),
        default="compare",
        help="compare alpha exactly, or normalize both frames to opaque alpha",
    )
    parser.add_argument(
        "--threshold",
        type=int,
        default=0,
        help="changed-pixel threshold, inclusive values remain unchanged (default: 0)",
    )
    parser.add_argument(
        "--heatmap-gain",
        type=float,
        default=4.0,
        help="visual-only multiplier for heatmap intensity (default: 4)",
    )
    parser.add_argument(
        "--ssim-window",
        type=int,
        default=8,
        help="non-overlapping RGB SSIM window size (default: 8)",
    )
    parser.add_argument(
        "--no-ssim",
        action="store_true",
        help="skip the supplemental SSIM metric",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)
    try:
        output_dir = args.output_dir
        if output_dir is None:
            output_dir = (
                DEFAULT_ARTIFACT_ROOT
                / _safe_frame_directory_name(str(args.frame_id))
            )

        report = compare_frame_pair(
            args.reference_png,
            args.candidate_png,
            output_dir,
            frame_id=args.frame_id,
            reference_orientation=args.reference_orientation,
            candidate_orientation=args.candidate_orientation,
            size_policy=args.size_policy,
            target_size=args.target_size,
            resize_filter=args.resize_filter,
            alpha_mode=args.alpha_mode,
            threshold=args.threshold,
            heatmap_gain=args.heatmap_gain,
            ssim_window=None if args.no_ssim else args.ssim_window,
        )
    except (OSError, ValueError, UnidentifiedImageError) as error:
        parser.exit(2, f"error: {error}\n")

    json.dump(
        report,
        sys.stdout,
        ensure_ascii=False,
        indent=2,
        sort_keys=True,
        allow_nan=False,
    )
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
