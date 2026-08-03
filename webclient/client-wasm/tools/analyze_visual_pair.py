#!/usr/bin/env python3
"""Measure and compose deterministic before/after rendering evidence."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageStat


def laplacian_variance(image: Image.Image) -> float:
    laplacian = image.convert("L").filter(
        ImageFilter.Kernel(
            (3, 3),
            (-1, -1, -1, -1, 8, -1, -1, -1, -1),
            scale=1,
            offset=128,
        )
    )
    return float(ImageStat.Stat(laplacian).var[0])


def gradient_energy(image: Image.Image) -> float:
    gray = image.convert("L")
    horizontal = gray.filter(
        ImageFilter.Kernel(
            (3, 3), (-1, 0, 1, -2, 0, 2, -1, 0, 1), scale=1, offset=128
        )
    )
    vertical = gray.filter(
        ImageFilter.Kernel(
            (3, 3), (-1, -2, -1, 0, 0, 0, 1, 2, 1), scale=1, offset=128
        )
    )
    h_histogram = horizontal.histogram()
    v_histogram = vertical.histogram()
    samples = max(1, image.width * image.height)
    energy = sum((value - 128) ** 2 * count for value, count in enumerate(h_histogram))
    energy += sum((value - 128) ** 2 * count for value, count in enumerate(v_histogram))
    return math.sqrt(energy / (samples * 2))


def local_range_violations(reference: Image.Image, candidate: Image.Image) -> dict[str, float | int]:
    reference_rgb = reference.convert("RGB")
    candidate_rgb = candidate.convert("RGB")
    minimum = reference_rgb.filter(ImageFilter.MinFilter(3))
    maximum = reference_rgb.filter(ImageFilter.MaxFilter(3))
    violations = 0
    maximum_overshoot = 0
    for source_min, source_max, value in zip(minimum.getdata(), maximum.getdata(), candidate_rgb.getdata()):
        overshoot = max(
            max(0, source_min[channel] - value[channel], value[channel] - source_max[channel])
            for channel in range(3)
        )
        if overshoot > 1:
            violations += 1
            maximum_overshoot = max(maximum_overshoot, overshoot)
    pixels = max(1, reference.width * reference.height)
    return {
        "pixels": violations,
        "percentage": round(100.0 * violations / pixels, 6),
        "maximumChannelOvershoot": maximum_overshoot,
    }


def make_evidence(
    reference: Image.Image,
    candidate: Image.Image,
    output: Path,
    labels: tuple[str, str],
) -> None:
    reference = reference.convert("RGB")
    candidate = candidate.convert("RGB")
    width, height = reference.size
    header = 34
    canvas = Image.new("RGB", (width * 2, height + header), "#111111")
    canvas.paste(reference, (0, header))
    canvas.paste(candidate, (width, header))
    draw = ImageDraw.Draw(canvas)
    draw.text((12, 10), labels[0], fill="white")
    draw.text((width + 12, 10), labels[1], fill="white")

    # Add two 3x inspection crops: character/equipment and ground texture.
    crops = [
        (width // 2 - 90, height // 2 - 100, width // 2 + 90, height // 2 + 120),
        (width // 2 - 210, height - 300, width // 2 + 210, height - 120),
    ]
    crop_width = 420
    crop_height = 250
    detail = Image.new("RGB", (crop_width * 2, crop_height * len(crops)), "#111111")
    for index, box in enumerate(crops):
        left = reference.crop(box)
        right = candidate.crop(box)
        left.thumbnail((crop_width, crop_height), Image.Resampling.NEAREST)
        right.thumbnail((crop_width, crop_height), Image.Resampling.NEAREST)
        detail.paste(left, (0, index * crop_height))
        detail.paste(right, (crop_width, index * crop_height))

    combined = Image.new("RGB", (canvas.width, canvas.height + detail.height), "#111111")
    combined.paste(canvas, (0, 0))
    combined.paste(detail, ((canvas.width - detail.width) // 2, canvas.height))
    output.parent.mkdir(parents=True, exist_ok=True)
    combined.save(output, optimize=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("reference")
    parser.add_argument("candidate")
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--reference-label", default="Referência")
    parser.add_argument("--candidate-label", default="Candidato")
    args = parser.parse_args()

    reference_path = Path(args.reference)
    candidate_path = Path(args.candidate)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    reference = Image.open(reference_path).convert("RGB")
    candidate = Image.open(candidate_path).convert("RGB")
    if reference.size != candidate.size:
        raise ValueError(f"size mismatch: {reference.size} != {candidate.size}")

    difference = ImageChops.difference(reference, candidate)
    difference_histogram = difference.histogram()
    samples = max(1, reference.width * reference.height * 3)
    rms = math.sqrt(
        sum((value % 256) ** 2 * count for value, count in enumerate(difference_histogram))
        / samples
    )
    reference_laplacian = laplacian_variance(reference)
    candidate_laplacian = laplacian_variance(candidate)
    reference_gradient = gradient_energy(reference)
    candidate_gradient = gradient_energy(candidate)
    metrics = {
        "resolution": {"width": reference.width, "height": reference.height},
        "rmsRgb": round(rms, 6),
        "laplacianVariance": {
            "reference": round(reference_laplacian, 6),
            "candidate": round(candidate_laplacian, 6),
            "changePercent": round(
                100.0 * (candidate_laplacian - reference_laplacian) / max(1e-9, reference_laplacian),
                4,
            ),
        },
        "gradientEnergy": {
            "reference": round(reference_gradient, 6),
            "candidate": round(candidate_gradient, 6),
            "changePercent": round(
                100.0 * (candidate_gradient - reference_gradient) / max(1e-9, reference_gradient),
                4,
            ),
        },
        "localRangeViolations": local_range_violations(reference, candidate),
    }
    evidence = output_dir / "side-by-side.png"
    make_evidence(
        reference,
        candidate,
        evidence,
        (args.reference_label, args.candidate_label),
    )
    (output_dir / "quality-metrics.json").write_text(
        json.dumps(metrics, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps({"ok": True, "metrics": metrics, "evidence": str(evidence)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
