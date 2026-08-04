#!/usr/bin/env python3
"""Build approved, versioned HD texture derivatives for Native WebGL2.

Original OpenWyd assets are never modified.  Each ``.owhd`` file contains a
complete offline RGBA mip chain and the FNV-1a hash of the exact wrapped WYS or
WYT source.  The WASM runtime rejects a derivative when that identity no
longer matches and falls back to the original texture.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

from audit_visual_quality import decode_texture


MAGIC = b"OWHDv1\0\0"
FORMAT_VERSION = 1
HEADER_SIZE = 40


def fnv1a64(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resize_unassociated_rgba(
    image: Image.Image, size: tuple[int, int]
) -> Image.Image:
    """Resize RGB and alpha independently, matching the legacy material model.

    Several official DXT3 character materials intentionally keep useful RGB
    underneath zero alpha.  The CMesh ``C`` path consumes that RGB while
    applying alpha through separate texture-stage state.  Premultiplying while
    generating mipmaps therefore turns official red armour black at distance.
    Keep the DDS channels unassociated just as the original compressed mip
    chain does.
    """

    rgba = image.convert("RGBA")
    channels = [
        channel.resize(size, Image.Resampling.LANCZOS)
        for channel in rgba.split()
    ]
    return Image.merge("RGBA", channels)


def build_mip_chain(image: Image.Image) -> list[Image.Image]:
    levels = [image.convert("RGBA")]
    while levels[-1].width > 1 or levels[-1].height > 1:
        previous = levels[-1]
        levels.append(
            resize_unassociated_rgba(
                previous,
                (max(1, previous.width // 2), max(1, previous.height // 2)),
            )
        )
    return levels


def make_realesrgan(model_path: Path):
    try:
        from basicsr.archs.rrdbnet_arch import RRDBNet
        from realesrgan import RealESRGANer
    except ImportError as error:
        raise RuntimeError(
            "Real-ESRGAN is required; install realesrgan, basicsr, torch and torchvision"
        ) from error

    network = RRDBNet(
        num_in_ch=3,
        num_out_ch=3,
        num_feat=64,
        num_block=23,
        num_grow_ch=32,
        scale=4,
    )
    return RealESRGANer(
        scale=4,
        model_path=str(model_path),
        model=network,
        tile=0,
        tile_pad=10,
        pre_pad=0,
        half=False,
        gpu_id=None,
    )


def enhance_rgb(image: Image.Image, upsampler, scale: int) -> Image.Image:
    rgb = np.asarray(image.convert("RGB"), dtype=np.uint8)
    bgr = rgb[:, :, ::-1]
    enhanced_bgr, _ = upsampler.enhance(bgr, outscale=scale)
    enhanced_rgb = enhanced_bgr[:, :, ::-1]
    result = Image.fromarray(enhanced_rgb.astype(np.uint8), "RGB")
    expected = (image.width * scale, image.height * scale)
    if result.size != expected:
        result = result.resize(expected, Image.Resampling.LANCZOS)
    return result


def preserve_official_local_color(
    original: Image.Image,
    enhanced: Image.Image,
    scale: int,
) -> Image.Image:
    """Retain AI detail while anchoring local color to the official asset."""

    from PIL import ImageFilter

    base = original.convert("RGB").resize(enhanced.size, Image.Resampling.LANCZOS)
    radius = max(2.0, float(scale) * 2.0)
    base_low = np.asarray(
        base.filter(ImageFilter.GaussianBlur(radius=radius)), dtype=np.float32
    )
    enhanced_low = np.asarray(
        enhanced.convert("RGB").filter(ImageFilter.GaussianBlur(radius=radius)),
        dtype=np.float32,
    )
    enhanced_pixels = np.asarray(enhanced.convert("RGB"), dtype=np.float32)
    corrected = enhanced_pixels + base_low - enhanced_low
    return Image.fromarray(
        np.clip(np.rint(corrected), 0, 255).astype(np.uint8), "RGB"
    )


def source_color_delta(original: Image.Image, derivative: Image.Image) -> float:
    restored = derivative.convert("RGB").resize(original.size, Image.Resampling.LANCZOS)
    source = np.asarray(original.convert("RGB"), dtype=np.float32)
    candidate = np.asarray(restored, dtype=np.float32)
    return float(np.mean(np.abs(candidate - source)))


def source_alpha_delta(original: Image.Image, derivative: Image.Image) -> float:
    restored = derivative.getchannel("A").resize(
        original.size, Image.Resampling.LANCZOS
    )
    source = np.asarray(original.getchannel("A"), dtype=np.float32)
    candidate = np.asarray(restored, dtype=np.float32)
    return float(np.mean(np.abs(candidate - source)))


def read_texture_alpha_modes(catalog: Path) -> dict[str, str]:
    """Read cAlpha from an ABI-locked 528-byte texture catalog."""

    record_size = 528
    data = catalog.read_bytes()
    if len(data) % record_size != 0:
        raise ValueError(f"invalid model texture catalog size: {catalog}")
    modes: dict[str, str] = {}
    for offset in range(0, len(data), record_size):
        record = data[offset : offset + record_size]
        raw_name = record[:255].split(b"\0", 1)[0]
        if not raw_name:
            continue
        name = raw_name.decode("cp1252").replace("\\", "/").lower()
        modes[name] = chr(record[510])
    return modes


def combine_preserved_alpha(original: Image.Image, enhanced_rgb: Image.Image) -> Image.Image:
    alpha = original.getchannel("A").resize(
        enhanced_rgb.size, Image.Resampling.LANCZOS
    )
    result = enhanced_rgb.convert("RGBA")
    result.putalpha(alpha)
    return result


def encode_owhd(source_bytes: bytes, source_size: tuple[int, int], mips: list[Image.Image]) -> bytes:
    payload = bytearray(
        struct.pack(
            "<8sIIQIIII",
            MAGIC,
            FORMAT_VERSION,
            HEADER_SIZE,
            fnv1a64(source_bytes),
            source_size[0],
            source_size[1],
            len(mips),
            0,
        )
    )
    if len(payload) != HEADER_SIZE:
        raise AssertionError(f"invalid OWHD header size: {len(payload)}")
    for mip in mips:
        rgba = mip.convert("RGBA").tobytes()
        payload.extend(struct.pack("<III", mip.width, mip.height, len(rgba)))
        payload.extend(rgba)
    return bytes(payload)


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("webclient/client-wasm/config/optimized-hd-assets.json"),
    )
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--entry", action="append", default=[])
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    manifest_path = (repo_root / args.manifest).resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schemaVersion") != 1:
        raise ValueError("unsupported optimized HD manifest schema")
    model_path = args.model.expanduser().resolve()
    if not model_path.is_file():
        raise FileNotFoundError(model_path)
    output_root = repo_root / "webclient/client-wasm/assets/optimized-hd"
    release_root = repo_root / "v769ClientRelease"
    catalog_alpha_modes: dict[str, str] = {}
    for catalog in (
        release_root / "Mesh/MeshTextureList.bin",
        release_root / "Env/EnvTextureList3.bin",
    ):
        catalog_alpha_modes.update(read_texture_alpha_modes(catalog))
    selected = {value.replace("\\", "/").lower() for value in args.entry}
    entries = [
        entry
        for entry in manifest.get("entries", [])
        if entry.get("approved") is True
        and (not selected or entry["source"].replace("\\", "/").lower() in selected)
    ]
    if not entries:
        raise ValueError("manifest has no matching approved entries")

    upsampler = make_realesrgan(model_path)
    generated: list[dict[str, object]] = []
    for entry in entries:
        source_relative = Path(entry["source"])
        source = release_root / source_relative
        if not source.is_file():
            raise FileNotFoundError(source)
        scale = int(entry.get("scale", 4))
        if scale != 4:
            raise ValueError(f"only 4x derivatives are currently supported: {source_relative}")
        destination = output_root / Path(f"{source_relative.as_posix()}.owhd")
        if destination.is_file() and not args.force:
            print(f"[optimized-hd] exists, use --force to rebuild: {destination}")
            continue

        original = decode_texture(source)
        catalog_key = source_relative.as_posix().lower()
        alpha_mode = catalog_alpha_modes.get(catalog_key)
        if alpha_mode not in {"N", "A", "a", "C"}:
            raise ValueError(
                f"missing/invalid cAlpha for {source_relative}: {alpha_mode!r}"
            )
        enhanced_rgb = enhance_rgb(original, upsampler, scale)
        enhanced_rgb = preserve_official_local_color(original, enhanced_rgb, scale)
        color_delta = source_color_delta(original, enhanced_rgb)
        maximum_color_delta = float(entry.get("maximumSourceColorDelta", 12.0))
        if color_delta > maximum_color_delta:
            raise ValueError(
                f"source color delta {color_delta:.3f} exceeds "
                f"{maximum_color_delta:.3f}: {source_relative}"
            )
        enhanced = combine_preserved_alpha(original, enhanced_rgb)
        alpha_delta = source_alpha_delta(original, enhanced)
        maximum_alpha_delta = float(entry.get("maximumSourceAlphaDelta", 3.0))
        if alpha_delta > maximum_alpha_delta:
            raise ValueError(
                f"source alpha delta {alpha_delta:.3f} exceeds "
                f"{maximum_alpha_delta:.3f}: {source_relative}"
            )
        mips = build_mip_chain(enhanced)
        source_bytes = source.read_bytes()
        atomic_write(destination, encode_owhd(source_bytes, original.size, mips))
        generated.append(
            {
                "source": source_relative.as_posix(),
                "sourceFnv1a64": f"{fnv1a64(source_bytes):016x}",
                "sourceSha256": hashlib.sha256(source_bytes).hexdigest(),
                "sourceSize": list(original.size),
                "output": destination.relative_to(repo_root).as_posix(),
                "outputSha256": sha256_file(destination),
                "outputSize": list(enhanced.size),
                "mipCount": len(mips),
                "model": entry["model"],
                "modelSha256": sha256_file(model_path),
                "sourceColorDelta": round(color_delta, 6),
                "sourceAlphaDelta": round(alpha_delta, 6),
                "catalogAlphaMode": alpha_mode,
                "colorPolicy": "official-local-color-v1",
                "mipPolicy": "unassociated-rgba-lanczos-v1",
            }
        )
        print(
            f"[optimized-hd] {source_relative.as_posix()} "
            f"{original.width}x{original.height}->{enhanced.width}x{enhanced.height} "
            f"mips={len(mips)} bytes={destination.stat().st_size}"
        )

    generated_manifest = {
        "schemaVersion": 1,
        "format": "OWHDv1",
        "generated": generated,
    }
    manifest_output = output_root / "generated-manifest.json"
    atomic_write(
        manifest_output,
        (json.dumps(generated_manifest, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
