#!/usr/bin/env python3
"""Validate select-server visual capture dimensions before image comparison.

The official select-server reference is a 16:9 capture.  The WASM harness should
be captured at a logical 1280x720 viewport so camera framing, text placement, and
screen-space UI are compared without browser-window zoom drift.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


EXPECTED_WIDTH = 1280
EXPECTED_HEIGHT = 720
EXPECTED_ASPECT = EXPECTED_WIDTH / EXPECTED_HEIGHT


class ImageSizeError(RuntimeError):
    pass


def png_size(data: bytes) -> tuple[int, int] | None:
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        return None
    if len(data) < 24:
        raise ImageSizeError("PNG header is truncated")
    return struct.unpack(">II", data[16:24])


def jpeg_size(data: bytes) -> tuple[int, int] | None:
    if not data.startswith(b"\xff\xd8"):
        return None

    offset = 2
    while offset < len(data):
        if data[offset] != 0xFF:
            offset += 1
            continue

        while offset < len(data) and data[offset] == 0xFF:
            offset += 1
        if offset >= len(data):
            break

        marker = data[offset]
        offset += 1

        if marker in (0xD8, 0xD9):
            continue
        if offset + 2 > len(data):
            raise ImageSizeError("JPEG segment length is truncated")

        segment_len = struct.unpack(">H", data[offset : offset + 2])[0]
        if segment_len < 2:
            raise ImageSizeError("JPEG segment length is invalid")

        if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
            if offset + 7 > len(data):
                raise ImageSizeError("JPEG frame header is truncated")
            height = struct.unpack(">H", data[offset + 3 : offset + 5])[0]
            width = struct.unpack(">H", data[offset + 5 : offset + 7])[0]
            return width, height

        offset += segment_len

    raise ImageSizeError("JPEG frame header was not found")


def read_image_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    for reader in (png_size, jpeg_size):
        size = reader(data)
        if size is not None:
            return size
    raise ImageSizeError(f"{path} is not a supported PNG or JPEG image")


def describe(label: str, width: int, height: int) -> str:
    aspect = width / height if height else 0.0
    return f"{label}: {width}x{height} aspect={aspect:.6f}"


def validate_size(label: str, width: int, height: int, strict_size: bool, aspect_tolerance: float) -> list[str]:
    errors: list[str] = []
    if strict_size and (width != EXPECTED_WIDTH or height != EXPECTED_HEIGHT):
        errors.append(f"{label} must be {EXPECTED_WIDTH}x{EXPECTED_HEIGHT}, got {width}x{height}")

    actual_aspect = width / height if height else 0.0
    if abs(actual_aspect - EXPECTED_ASPECT) > aspect_tolerance:
        errors.append(
            f"{label} aspect must be {EXPECTED_ASPECT:.6f} +/- {aspect_tolerance:.6f}, got {actual_aspect:.6f}"
        )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True, help="Official select-server reference PNG/JPEG.")
    parser.add_argument("--actual", type=Path, help="Current WASM select-server screenshot PNG/JPEG.")
    parser.add_argument(
        "--allow-scaled",
        action="store_true",
        help="Allow non-1280x720 images when their aspect ratio still matches the reference contract.",
    )
    parser.add_argument(
        "--aspect-tolerance",
        type=float,
        default=0.0005,
        help="Allowed absolute aspect-ratio delta from 16:9.",
    )
    args = parser.parse_args()

    strict_size = not args.allow_scaled
    checks: list[tuple[str, Path]] = [("reference", args.reference)]
    if args.actual:
        checks.append(("actual", args.actual))

    errors: list[str] = []
    sizes: dict[str, tuple[int, int]] = {}
    for label, path in checks:
        try:
            width, height = read_image_size(path)
        except OSError as exc:
            errors.append(f"{label}: could not read {path}: {exc}")
            continue
        except ImageSizeError as exc:
            errors.append(f"{label}: {exc}")
            continue

        sizes[label] = (width, height)
        print(describe(label, width, height))
        errors.extend(validate_size(label, width, height, strict_size, args.aspect_tolerance))

    if "reference" in sizes and "actual" in sizes and sizes["reference"] != sizes["actual"]:
        ref_width, ref_height = sizes["reference"]
        actual_width, actual_height = sizes["actual"]
        errors.append(
            "actual capture must match reference dimensions for pixel comparison, "
            f"got actual {actual_width}x{actual_height} vs reference {ref_width}x{ref_height}"
        )

    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1

    print("select-server capture dimensions match the 1280x720 visual validation contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
