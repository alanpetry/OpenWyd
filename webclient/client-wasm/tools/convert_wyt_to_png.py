#!/usr/bin/env python3
"""Convert an original OpenWyd WT10/TGA texture to a browser PNG."""

from __future__ import annotations

import argparse
import binascii
import struct
import zlib
from pathlib import Path


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _png_chunk(kind: bytes, payload: bytes) -> bytes:
    checksum = binascii.crc32(kind)
    checksum = binascii.crc32(payload, checksum) & 0xFFFFFFFF
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", checksum)
    )


def convert_wyt_to_png(source: Path, destination: Path) -> None:
    data = source.read_bytes()
    if data[:4] == b"WT10":
        data = data[4:]
    if len(data) < 18:
        raise ValueError(f"{source} is too short to contain a TGA image")

    (
        id_length,
        color_map_type,
        image_type,
        _color_map_first,
        _color_map_length,
        _color_map_depth,
        _x_origin,
        _y_origin,
        width,
        height,
        bits_per_pixel,
        descriptor,
    ) = struct.unpack_from("<BBBHHBHHHHBB", data)
    if color_map_type != 0 or image_type not in (2, 3):
        raise ValueError(
            f"{source} uses unsupported TGA type {image_type}"
        )
    if width <= 0 or height <= 0:
        raise ValueError(f"{source} has invalid dimensions {width}x{height}")

    grayscale = image_type == 3
    valid_depths = (8, 16) if grayscale else (24, 32)
    if bits_per_pixel not in valid_depths:
        raise ValueError(
            f"{source} uses unsupported {bits_per_pixel}-bit pixels"
        )
    bytes_per_pixel = bits_per_pixel // 8
    pixel_start = 18 + id_length
    pixel_bytes = width * height * bytes_per_pixel
    pixels = data[pixel_start : pixel_start + pixel_bytes]
    if len(pixels) != pixel_bytes:
        raise ValueError(f"{source} has a truncated pixel payload")

    top_origin = bool(descriptor & 0x20)
    rows: list[bytes] = []
    for output_y in range(height):
        source_y = output_y if top_origin else height - 1 - output_y
        row_start = source_y * width * bytes_per_pixel
        row = pixels[row_start : row_start + width * bytes_per_pixel]
        rgb = bytearray(width * 3)
        for x in range(width):
            src = x * bytes_per_pixel
            dst = x * 3
            if grayscale:
                rgb[dst : dst + 3] = row[src : src + 1] * 3
            else:
                rgb[dst] = row[src + 2]
                rgb[dst + 1] = row[src + 1]
                rgb[dst + 2] = row[src]
        rows.append(b"\x00" + bytes(rgb))

    png = bytearray(PNG_SIGNATURE)
    png.extend(
        _png_chunk(
            b"IHDR",
            struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0),
        )
    )
    png.extend(_png_chunk(b"IDAT", zlib.compress(b"".join(rows), 9)))
    png.extend(_png_chunk(b"IEND", b""))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(png)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    convert_wyt_to_png(args.source.resolve(), args.destination.resolve())
    print(f"[wyt-png] {args.source} -> {args.destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
