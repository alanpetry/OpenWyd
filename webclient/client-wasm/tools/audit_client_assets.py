#!/usr/bin/env python3
"""Exhaustively audit every file in the distributed OpenWyd client tree.

The audit intentionally follows the original C++ readers.  It expands the
same preload manifest as the linker, validates each payload's complete binary
envelope, checks catalog/list references, and writes both a machine-readable
report and an individual Markdown checklist.  A checked box means that the
file was actually opened, hashed and passed through its format validator.  It
also lists platform/launcher files intentionally excluded from the WASM VFS.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import sys
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Callable

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from preload_manifest import read_preload_entries  # noqa: E402


@dataclass
class AuditItem:
    source: str
    virtual_path: str
    size: int
    sha256: str
    format: str
    loader: str
    status: str
    detail: str


@dataclass
class Reference:
    owner: str
    target: str
    status: str
    detail: str


@dataclass
class LoaderSite:
    source: str
    line: int
    call: str
    role: str
    snippet: str


class InvalidAsset(ValueError):
    pass


def fail(message: str) -> None:
    raise InvalidAsset(message)


def u16(data: bytes, offset: int = 0) -> int:
    if offset + 2 > len(data):
        fail(f"truncated uint16 at {offset}")
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int = 0) -> int:
    if offset + 4 > len(data):
        fail(f"truncated uint32 at {offset}")
    return struct.unpack_from("<I", data, offset)[0]


def i32(data: bytes, offset: int = 0) -> int:
    if offset + 4 > len(data):
        fail(f"truncated int32 at {offset}")
    return struct.unpack_from("<i", data, offset)[0]


def cstring(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("cp1252", errors="replace").strip()


def norm_virtual(value: str) -> str:
    value = value.replace("\\", "/").strip()
    while value.startswith("./"):
        value = value[2:]
    return "/" + re.sub(r"/+", "/", value.lstrip("/"))


def ensure_finite(values: tuple[float, ...], context: str) -> None:
    if not all(math.isfinite(value) for value in values):
        fail(f"non-finite float in {context}")


def validate_wyt(data: bytes) -> str:
    if len(data) < 22 or data[:4] != b"WT10":
        fail("missing WT10 wrapper")
    tga = data[4:]
    id_len = tga[0]
    cmap_type = tga[1]
    image_type = tga[2]
    width, height = u16(tga, 12), u16(tga, 14)
    bpp = tga[16]
    if not width or not height or cmap_type != 0:
        fail(f"unsupported TGA geometry/cmap {width}x{height}/{cmap_type}")
    grayscale = image_type in (3, 11)
    supported_bpp = (8, 16) if grayscale else (16, 24, 32)
    if image_type not in (2, 3, 10, 11) or bpp not in supported_bpp:
        fail(f"unsupported TGA type/bpp {image_type}/{bpp}")
    channels = bpp // 8
    offset = 18 + id_len
    if offset > len(tga):
        fail("truncated TGA id")
    pixels = width * height
    if image_type in (2, 3):
        required = offset + pixels * channels
        if required > len(tga):
            fail(f"truncated TGA pixels: need {required}, have {len(tga)}")
    else:
        decoded = 0
        while decoded < pixels:
            if offset >= len(tga):
                fail("truncated TGA RLE header")
            packet = tga[offset]
            offset += 1
            count = (packet & 0x7F) + 1
            if decoded + count > pixels:
                fail("TGA RLE packet exceeds image")
            need = channels if packet & 0x80 else channels * count
            if offset + need > len(tga):
                fail("truncated TGA RLE payload")
            offset += need
            decoded += count
    return f"WT10/TGA {width}x{height} {bpp}bpp type={image_type}"


def validate_wys(data: bytes) -> str:
    if len(data) < 132 or data[:4] != b"WS10":
        fail("missing WS10 wrapper")
    dds_buffer = bytearray(data[1:])
    dds_buffer[:3] = b"DDS"
    dds_buffer[84:88] = b"DXT1" if dds_buffer[84] == ord("2") else b"DXT3"
    dds = bytes(dds_buffer)
    if dds[:4] != b"DDS " or u32(dds, 4) != 124:
        fail("invalid DDS header")
    height, width = u32(dds, 12), u32(dds, 16)
    mip_count = max(1, u32(dds, 28))
    if not width or not height or width > 16384 or height > 16384:
        fail(f"invalid DDS dimensions {width}x{height}")
    if u32(dds, 76) != 32:
        fail("invalid DDS pixel format size")
    fourcc = dds[84:88]
    if fourcc not in (b"DXT1", b"DXT3"):
        fail(f"bridge does not support DDS {fourcc!r}")
    block_bytes = 8 if fourcc == b"DXT1" else 16
    required = 128
    w, h = width, height
    for _ in range(mip_count):
        required += max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * block_bytes
        w, h = max(1, w // 2), max(1, h // 2)
    if required > len(dds):
        fail(f"truncated DDS blocks: need {required}, have {len(dds)}")
    return f"WS10/DDS {width}x{height} {fourcc.decode()} mips={mip_count}"


def validate_msh(data: bytes) -> str:
    if len(data) < 32:
        fail("mesh header shorter than 32 bytes")
    parent, mesh_id, fvf, stride, influences, palette, vertices, indices = (
        struct.unpack_from("<8I", data)
    )
    if stride == 0 or stride > 1024 or palette > 512:
        fail(f"invalid stride/palette {stride}/{palette}")
    if vertices > 10_000_000 or indices > 30_000_000:
        fail("implausible mesh counts")
    expected = 32 + palette * 64 + palette * 4 + stride * vertices + indices * 2
    if expected != len(data):
        fail(f"mesh envelope {len(data)} != expected {expected}")
    return (
        f"CMesh header id={mesh_id} parent={parent} fvf=0x{fvf:X} "
        f"stride={stride} vertices={vertices} indices={indices} palette={palette} "
        f"influences={influences}"
    )


def validate_msa(data: bytes) -> str:
    if len(data) < 12:
        fail("MSA header shorter than 12 bytes")
    fvf, stride, attributes = struct.unpack_from("<3I", data)
    if not 1 <= attributes <= 32 or not 1 <= stride <= 1024:
        fail(f"invalid MSA attributes/stride {attributes}/{stride}")
    offset = 12 + attributes * 20 + attributes * 11
    if offset + 8 > len(data):
        fail("truncated MSA attribute table")
    index_bytes = u32(data, offset)
    offset += 4
    if index_bytes % 2 or offset + index_bytes + 4 > len(data):
        fail(f"invalid MSA index buffer size {index_bytes}")
    offset += index_bytes
    vertex_bytes = u32(data, offset)
    offset += 4
    if vertex_bytes % stride or offset + vertex_bytes != len(data):
        fail(
            f"MSA vertex envelope/stride mismatch offset={offset} bytes={vertex_bytes} "
            f"stride={stride} file={len(data)}"
        )
    return (
        f"TMMesh fvf=0x{fvf:X} stride={stride} attributes={attributes} "
        f"vertices={vertex_bytes // stride} indices={index_bytes // 2}"
    )


def validate_bon(data: bytes) -> str:
    if not data or len(data) % 8:
        fail(f"bone table size {len(data)} is not a non-zero multiple of 8")
    return f"MeshManager bone table records={len(data) // 8}"


def validate_ani(data: bytes) -> str:
    if len(data) < 8:
        fail("animation header shorter than 8 bytes")
    ticks, bones = struct.unpack_from("<2I", data)
    if not ticks or not bones or ticks > 100_000 or bones > 1024:
        fail(f"invalid animation ticks/bones {ticks}/{bones}")
    expected = 8 + ticks * bones * 64
    if expected != len(data):
        fail(f"animation envelope {len(data)} != expected {expected}")
    # Check every matrix component; this also catches endian/layout mistakes.
    for values in struct.iter_unpack("<16f", memoryview(data)[8:]):
        ensure_finite(values, "animation matrix")
    return f"MeshManager animation ticks={ticks} bones={bones} matrices={ticks * bones}"


def validate_trn(data: bytes) -> str:
    tile_bytes = 64 * 64 * 12
    if len(data) < 3 + tile_bytes:
        fail("terrain tile shorter than minimum")
    name_len = data[0]
    if name_len > 128:
        fail(f"terrain texture name length {name_len} exceeds loader buffer")
    expected = 1 + name_len + 2 + tile_bytes
    if expected != len(data):
        fail(f"terrain envelope {len(data)} != expected {expected}")
    name = cstring(data[1 : 1 + name_len])
    return f"TMGround 64x64 records x 12 bytes texture={name!r} tile=({data[1+name_len]},{data[2+name_len]})"


def validate_env_objects(data: bytes) -> str:
    offset = 0
    records = 0
    scaled = 0
    while offset < len(data):
        if offset + 28 > len(data):
            fail(f"truncated object record at {offset}")
        obj_type = u32(data, offset)
        values = struct.unpack_from("<4f", data, offset + 4)
        ensure_finite(values, f"object {records}")
        offset += 28
        has_scale = (
            501 <= obj_type <= 506
            or 511 <= obj_type <= 518
            or 520 <= obj_type <= 530
            or obj_type in (531, 532)
            or 532 < obj_type < 600
        )
        if has_scale:
            if offset + 8 > len(data):
                fail(f"truncated object scale at record {records}")
            ensure_finite(struct.unpack_from("<2f", data, offset), f"object scale {records}")
            offset += 8
            scaled += 1
        records += 1
        if records > 4096:
            fail("object container exceeds 4096-record loader capacity")
    return f"TMObjectContainer records={records} scaled={scaled} exact EOF"


def validate_wav(data: bytes) -> str:
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        fail("invalid RIFF/WAVE header")
    if u32(data, 4) + 8 > len(data):
        fail("RIFF length exceeds file")
    offset = 12
    fmt = None
    pcm_bytes = 0
    while offset + 8 <= len(data):
        chunk = data[offset : offset + 4]
        size = u32(data, offset + 4)
        offset += 8
        if offset + size > len(data):
            fail(f"truncated WAV chunk {chunk!r}")
        if chunk == b"fmt " and size >= 16:
            fmt = struct.unpack_from("<HHIIHH", data, offset)
        elif chunk == b"data":
            pcm_bytes += size
        offset += size + (size & 1)
    if fmt is None or not pcm_bytes:
        fail("WAV missing fmt or data chunk")
    tag, channels, rate, byte_rate, align, bits = fmt
    if tag != 1 or channels not in (1, 2) or bits not in (8, 16):
        fail(f"WebAudio bridge supports PCM mono/stereo 8/16 only, got {fmt}")
    if align != channels * bits // 8 or byte_rate != rate * align or pcm_bytes % align:
        fail("inconsistent WAV rate/alignment")
    return f"DirectSound/WebAudio PCM {channels}ch {rate}Hz {bits}bit frames={pcm_bytes // align}"


def validate_ttf(data: bytes) -> str:
    if len(data) < 12 or data[:4] not in (b"\x00\x01\x00\x00", b"OTTO", b"true"):
        fail("invalid sfnt header")
    count = struct.unpack_from(">H", data, 4)[0]
    if not count or 12 + count * 16 > len(data):
        fail(f"invalid sfnt table count {count}")
    tags = []
    for index in range(count):
        pos = 12 + index * 16
        tag, _, offset, size = struct.unpack_from(">4sIII", data, pos)
        if offset + size > len(data):
            fail(f"sfnt table {tag!r} exceeds file")
        tags.append(tag)
    if b"cmap" not in tags:
        fail("sfnt has no cmap")
    return f"GDI/browser font sfnt tables={count}"


def validate_cur(data: bytes) -> str:
    if len(data) < 6 or u16(data, 0) != 0 or u16(data, 2) != 2:
        fail("invalid CUR directory")
    count = u16(data, 4)
    if not count or 6 + count * 16 > len(data):
        fail(f"invalid CUR image count {count}")
    for index in range(count):
        pos = 6 + index * 16
        size, offset = u32(data, pos + 8), u32(data, pos + 12)
        if not size or offset + size > len(data):
            fail(f"CUR image {index} exceeds file")
    return f"Win32/browser cursor images={count}"


def validate_mp3(data: bytes) -> str:
    if len(data) < 4:
        fail("MP3 is empty")
    offset = 0
    if data[:3] == b"ID3":
        if len(data) < 10:
            fail("truncated ID3 header")
        size_bytes = data[6:10]
        if any(value & 0x80 for value in size_bytes):
            fail("invalid ID3 syncsafe size")
        offset = 10 + sum(value << shift for value, shift in zip(size_bytes, (21, 14, 7, 0)))
    search_end = min(len(data) - 1, offset + 1_048_576)
    frame = -1
    for pos in range(offset, search_end):
        if data[pos] == 0xFF and data[pos + 1] & 0xE0 == 0xE0 and data[pos + 1] & 0x06:
            frame = pos
            break
    if frame < 0:
        fail("no MPEG audio frame found")
    return f"HTMLAudio streamed MP3 first-frame={frame} bytes={len(data)}"


def validate_shader(data: bytes) -> str:
    if not data or len(data) % 4:
        fail("D3D9 bytecode is not DWORD-aligned")
    token = u32(data)
    shader_type = token >> 16
    if shader_type == 0xFFFE:
        kind = "vertex"
    elif shader_type == 0xFFFF:
        kind = "pixel"
    else:
        fail(f"invalid D3D9 shader version token 0x{token:08X}")
    if u32(data, len(data) - 4) != 0x0000FFFF:
        fail("D3D9 shader lacks END token")
    return f"D3D9 bridge bytecode {kind} shader_{token & 0xFF}.{(token >> 8) & 0xFF} dwords={len(data)//4}"


def validate_text(data: bytes, suffix: str) -> str:
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        data.decode("utf-16")
        if suffix != ".guimat":
            return "UTF-16 text parsed; TMScene BOM-aware text adapter"
        return "legacy GUIMAT UTF-16 text (packaged; no compiled TMProject loader)"
    if b"\0" in data:
        fail("text file contains NUL bytes without a Unicode BOM")
    if suffix == ".guimat":
        fail("GUIMAT text lacks UTF-16 BOM")
    text = data.decode("utf-8-sig", errors="strict") if _is_utf8(data) else data.decode("cp1252")
    lines = text.count("\n") + (1 if text else 0)
    return f"C stdio text encoding={'UTF-8' if _is_utf8(data) else 'CP1252'} lines={lines}"


def _is_utf8(data: bytes) -> bool:
    try:
        data.decode("utf-8-sig")
        return True
    except UnicodeDecodeError:
        return False


RC_SIZES = {1: 40, 2: 40, 3: 32, 6: 52, 10: 48, 12: 52, 13: 184, 15: 28, 16: 40}
RC_LEGACY_SIZES = {**RC_SIZES, 1: 36, 2: 164, 12: 176}


def _walk_rc(data: bytes, sizes: dict[int, int]) -> tuple[int, set[int]] | None:
    offset = 0
    records = 0
    kinds: set[int] = set()
    while offset < len(data):
        if offset + 4 > len(data):
            return None
        kind = i32(data, offset)
        size = sizes.get(kind)
        if size is None or offset + 4 + size > len(data):
            return None
        offset += 4 + size
        records += 1
        kinds.add(kind)
    return records, kinds


def validate_rc(data: bytes) -> str:
    current = _walk_rc(data, RC_SIZES)
    legacy = _walk_rc(data, RC_LEGACY_SIZES)
    if current:
        records, kinds = current
        return f"TMScene::ReadRCBin current records={records} types={sorted(kinds)} exact EOF"
    if legacy:
        records, kinds = legacy
        return f"WASM legacy RC adapter records={records} types={sorted(kinds)} exact EOF"
    fail("does not match current or supported legacy RC records")


def validate_camera(data: bytes) -> str:
    if len(data) < 4:
        fail("camera action is truncated")
    count = i32(data)
    if count < 0 or count > 1000 or len(data) != 4 + count * 28:
        fail(f"camera action envelope count={count} size={len(data)}")
    for values in struct.iter_unpack("<Ih2x5f", memoryview(data)[4:]):
        ensure_finite(values[2:], "camera tick")
    return f"TMScene::ReadCameraPos ticks={count}"


def validate_texture_catalog(data: bytes, name: str) -> str:
    if len(data) % 528 == 0:
        stride = 528
    elif len(data) % 264 == 0:
        stride = 264
    elif len(data) % 260 == 0:
        stride = 260
    else:
        fail(f"texture catalog size {len(data)} is not a known record multiple")
    live = 0
    highest = -1
    for index in range(len(data) // stride):
        record = data[index * stride : (index + 1) * stride]
        filename = cstring(record[:255] if stride == 528 else record[:128])
        if filename:
            live += 1
            highest = index
    return f"TextureManager {name} stride={stride} slots={len(data)//stride} live={live} highest={highest}"


def validate_known_binary(virtual: str, data: bytes) -> tuple[str, str]:
    name = Path(virtual).name.lower()
    lower = virtual.lower()
    exact = {
        "itemlist.bin": (1_066_004, "BASE_ReadItemList 6500 x 164 encrypted + 4-byte trailer"),
        "skilldata.bin": (25_796, "BASE_ReadSkillBin 248 x 104 encrypted + 4-byte trailer"),
        "itemprice.bin": (800, "BASE_ReadItemPrice 100 x 2 int32"),
        "itemicon.bin": (26_000, "ReadItemicon 6500 int32"),
        "itemname.bin": (214_880, "ReadItemName 3160 x 68 encoded"),
        "inititem.bin": (800, "STRUCT_INITITEM 100 x 8 (currently packaged, reader disabled)"),
        "serverlist.bin": (7_040, "BASE_InitializeServerList 64 x 110 legacy records"),
        "object.bin": (524_292, "MeshManager object mask 524288 + 4-byte trailer"),
        "strdef.bin": (256_004, "BASE_ReadMessageBin 2000 x 128 encrypted + checksum"),
        "cdata.bin": (8_192, "TMGround checksum table 2048 int32"),
        "curse.bin": (8_192, "curse translation table 256 x 32"),
        "attributemap.dat": (1_048_576, "BASE_InitializeAttribute 1024 x 1024 bytes"),
        "heightmap.dat": (16_777_216, "legacy world height map 4096 x 4096 bytes"),
        "config.bin": (32, "NewApp reads 30-byte SaveUpdatAndConfig; official file has 2-byte legacy trailer"),
        "itemprice.bin": (800, "BASE_ReadItemPrice 100 x 2 int32"),
        "mountdata.bin": (27_204, "mount table payload 27200 + 4-byte trailer"),
        "rc.bin": (133_120, "ObjectManager::InitResourceList 2560 x 52"),
        "timetable.bin": (12_800, "TMDemoScene 50 x 16 stMobAni records"),
        "ending.bin": (64_000, "TMDemoScene 500 x 128 encrypted strings"),
        "mixlist.bin": (272_800, "CItemMix result[100] + requirement[100] tables"),
        "sn.bin": (143, "NewApp server names 11 x 9 + server counts 11 x int32"),
        "validindex.bin": (74_400, "MeshManager valid-animation indices 100 x 186 int32"),
    }
    if name == "skilldata.bin" and "/mesh/" in lower:
        return "legacy SkillData table", f"packaged legacy copy size={len(data)}; runtime opens /SkillData.bin"
    if name == "itemlist.bin" and "/mesh/" in lower:
        return "legacy ItemList table", f"packaged legacy copy size={len(data)}; runtime opens /ItemList.bin"
    if name == "itemname.bin" and len(data) == 68:
        return "localized ItemName stub", "one 68-byte record; active locale selection determines use"
    expected = exact.get(name)
    if expected:
        size, detail = expected
        if len(data) != size:
            fail(f"{detail}: size {len(data)} != {size}")
        if name == "skilldata.bin":
            decoded = bytes(value ^ 0x5A for value in data[: 248 * 104])
            for index in range(248):
                rec = struct.unpack_from("<12i8s8s10i", decoded, index * 104)
                if rec[-2] not in (0, index + 1):
                    fail(f"SkillData record {index} embedded index={rec[-2]}")
        if name == "itemlist.bin" and data[: 6500 * 164] == bytes(6500 * 164):
            fail("ItemList encrypted payload is all zero")
        return "fixed binary table", detail
    if name == "openwyd_gdi_tahoma12_a4.bin":
        if len(data) < 196 or data[:8] != b"OWGDA4\r\n":
            fail("invalid generated GDI A4 atlas header")
        if len(data) != 69_828:
            fail(f"generated GDI atlas size {len(data)} != 69828")
        return "generated GDI atlas", "OWGDA4 header + 512x256 A4 pixels; certified manifest accompanies payload"
    return "opaque/legacy binary", f"complete non-empty payload size={len(data)}; no exact active loader contract identified"


def loader_for(virtual: str, fmt: str) -> str:
    if virtual.lower().startswith("/__not_wasm__/"):
        return "legacy Windows/launcher component; intentionally absent from WASM VFS"
    lower = virtual.lower()
    mapping = {
        "WYT texture": "TextureManager WT10→TGA / D3D9 WebGL texture bridge",
        "WYS texture": "TextureManager WS10→DDS / D3D9 WebGL texture bridge",
        "CMesh": "CMesh::LoadMesh",
        "TMMesh": "TMMesh::LoadMsa",
        "bone": "MeshManager::InitBoneAnimation",
        "animation": "MeshManager::InitBoneAnimation",
        "terrain": "TMGround::LoadTileMap",
        "objects": "TMObjectContainer::Load",
        "WAV": "CSoundManager/CWaveFile and WebAudio bridge",
        "MP3": "NewApp music list and HTMLAudio bridge",
        "TTF": "GDI font path / browser font asset",
        "CUR": "Win32 cursor / browser cursor adapter",
        "shader": "D3DDevice shader loader / fixed-function WebGL bridge",
        "RC": "TMScene::LoadRC/ReadRCBin",
        "camera": "TMScene::ReadCameraPos",
        "catalog": "TextureManager catalog readers",
        "text": "original C stdio parser",
    }
    if fmt == "objects":
        return mapping[fmt]
    if lower.endswith(".pane") or lower.endswith(".wyp") or lower.endswith(".map"):
        return "packaged legacy client payload; no compiled TMProject reader"
    return mapping.get(fmt, "original fixed-file reader or packaged legacy payload")


def classify_and_validate(virtual: str, data: bytes) -> tuple[str, str, str]:
    lower = virtual.lower()
    name = Path(lower).name
    suffix = Path(lower).suffix
    status = "OK"
    if lower.startswith("/__not_wasm__/"):
        if not data:
            fail("empty excluded platform payload")
        if data[:4] == b"WS10":
            return "excluded legacy texture", validate_wys(data), "EXCLUDED"
        if suffix in (".exe", ".dll", ".sys", ".vxd") and data[:2] == b"MZ":
            if len(data) < 64:
                fail("truncated DOS executable header")
            image_offset = u32(data, 60)
            signature = data[image_offset : image_offset + 4]
            if signature not in (b"PE\0\0", b"LE\0\0"):
                fail(f"invalid Windows image signature {signature!r}")
            image_kind = "PE" if signature == b"PE\0\0" else "LE/VXD"
            return "excluded Windows binary", f"valid MZ + {image_kind} envelope", "EXCLUDED"
        if suffix == ".bmp":
            if len(data) < 54 or data[:2] != b"BM" or u32(data, 2) != len(data):
                fail("invalid launcher BMP envelope")
            return "excluded launcher asset", "BMP signature and declared size verified", "EXCLUDED"
        if suffix == ".swf":
            if len(data) < 8 or data[:3] not in (b"FWS", b"CWS", b"ZWS"):
                fail("invalid SWF envelope")
            return "excluded launcher asset", f"SWF signature={data[:3].decode('ascii')} version={data[3]}", "EXCLUDED"
        if suffix == ".xml":
            ET.fromstring(data)
            return "excluded launcher metadata", "XML parsed completely", "EXCLUDED"
        if suffix == ".vch":
            if data[0] != 0x30:
                fail("unexpected Adobe VCH envelope")
            return "excluded launcher metadata", "ASN.1 sequence envelope verified", "EXCLUDED"
        if name == "thumbs.db":
            if data[:8] != bytes.fromhex("d0cf11e0a1b11ae1"):
                fail("invalid OLE compound-file signature")
            return "excluded shell metadata", "OLE compound-file signature verified", "EXCLUDED"
        if suffix == ".log":
            detail = validate_text(data, suffix)
            return "excluded runtime log", detail, "EXCLUDED"
        if name == "hash":
            if len(data) != 32:
                fail("Adobe AIR hash payload is not 32 bytes")
            return "excluded launcher metadata", "32-byte Adobe AIR hash envelope", "EXCLUDED"
        if suffix in (".hat", ".nfo") or name == "mimetype":
            detail = validate_text(data, suffix)
            return "excluded launcher metadata", detail, "EXCLUDED"
        fail("unclassified file outside the WASM runtime manifest")
    if suffix == ".wyt":
        return "WYT texture", validate_wyt(data), status
    if suffix == ".wys":
        return "WYS texture", validate_wys(data), status
    if suffix in (".msh", ".mshb"):
        return "CMesh", validate_msh(data), status
    if suffix == ".msa":
        return "TMMesh", validate_msa(data), status
    if suffix == ".bon" or name == "kk01bon":
        if suffix == ".bon" and len(data) % 8:
            return "legacy/misnamed BON", "not an 8-byte bone table and not referenced by BoneAni4.txt", "WARNING"
        return "bone", validate_bon(data), status
    if suffix == ".ani":
        if name.startswith("mesh_bl"):
            return "legacy/misnamed ANI", "does not match active animation envelope and is not generated by BoneAni4.txt", "WARNING"
        return "animation", validate_ani(data), status
    if suffix == ".trn":
        return "terrain", validate_trn(data), status
    if suffix == ".wav":
        return "WAV", validate_wav(data), status
    if suffix == ".mp3":
        return "MP3", validate_mp3(data), status
    if suffix == ".ttf":
        return "TTF", validate_ttf(data), status
    if suffix == ".cur":
        return "CUR", validate_cur(data), status
    if suffix == ".json":
        json.loads(data.decode("utf-8-sig"))
        return "JSON", "JSON parsed completely", status
    if "texturelist" in name and suffix == ".bin":
        return "catalog", validate_texture_catalog(data, name), status
    if suffix == ".dat" and re.search(r"/env/(character|field\d+)\.dat$", lower):
        return "objects", validate_env_objects(data), status
    if suffix == ".dat" and re.search(r"/(ui|mesh)/field\d+\.dat$", lower):
        return "objects", validate_env_objects(data), status
    if name == "attributemap.dat":
        expected = 1_048_580 if lower == "/mesh/attributemap.dat" else 1_048_576
        if len(data) != expected:
            fail(f"AttributeMap size {len(data)} != {expected}")
        trailer = " + legacy trailer" if expected > 1_048_576 else ""
        return "attribute map", f"BASE_InitializeAttribute 1024x1024 bytes{trailer}", status
    if name == "heightmap.dat":
        if len(data) != 16_777_216:
            fail(f"HeightMap size {len(data)} != 16777216")
        return "height map", "legacy 4096x4096 byte height field; complete envelope", status
    if suffix == ".bin" and (name.startswith("shader") or "/shader/" in lower):
        return "shader", validate_shader(data), status
    if suffix == ".bin" and "camaction" in name or name == "testaction.bin":
        return "camera", validate_camera(data), status
    if suffix == ".bin" and re.fullmatch(r"demo[2-4]?\.bin", name):
        record = 60
        if len(data) % record:
            fail(f"demo human table is not a multiple of {record}")
        return "demo table", f"TMSelectServerScene stDemoHuman records={len(data)//record}", status
    if name == "enddemo.bin":
        if len(data) % 48:
            fail("EndDemo is not a multiple of stDemoHuman2 (48)")
        return "demo table", f"TMDemoScene stDemoHuman2 records={len(data)//48}", status
    if suffix == ".bin" and lower.startswith("/ui/") and "scene" in name:
        if not data:
            return "RC", "zero-byte alternate file; not selected by any LoadRC path", "WARNING"
        try:
            return "RC", validate_rc(data), status
        except InvalidAsset:
            if name in ("fieldscene.bin", "selcharscene.bin"):
                return "legacy RC", "mixed recovered-editor record layouts; alternate file is not selected by active LoadRC paths", "WARNING"
            raise
    if suffix == ".bin":
        fmt, detail = validate_known_binary(virtual, data)
        if not data:
            return fmt, "zero-byte payload", "WARNING"
        if fmt == "opaque/legacy binary":
            status = "WARNING"
        return fmt, detail, status
    if suffix in (".txt", ".ini", ".csv", ".guimat"):
        detail = validate_text(data, suffix)
        if suffix == ".guimat":
            status = "WARNING"
        return "text", detail, status
    if suffix == ".pane":
        if len(data) < 8 or data[:8] != bytes.fromhex("0d0c5e61c6cb3f46"):
            fail("unexpected PANE envelope signature")
        return "legacy PANE", "encrypted container signature and non-empty payload verified; no compiled loader", "WARNING"
    if suffix == ".wyp":
        if len(data) < 8 or data[:8] != bytes.fromhex("97e8388293a2c2d3"):
            fail("unexpected WYP envelope signature")
        return "legacy WYP", "encrypted image container signature and non-empty payload verified; no compiled loader", "WARNING"
    if suffix == ".map":
        if len(data) != 53_057:
            fail(f"legacy MAP size {len(data)} != 53057")
        return "legacy MAP", "53057-byte legacy map envelope verified; no compiled loader", "WARNING"
    if suffix == ".dat":
        if not data:
            fail("empty DAT payload")
        # Most remaining DATs are line-oriented localized tables.
        if b"\0" not in data:
            return "text", validate_text(data, suffix), status
        return "binary DAT", f"complete non-empty payload size={len(data)}", "WARNING"
    if not data:
        return "empty", "empty optional payload", "WARNING"
    return suffix.lstrip(".").upper() or "no extension", f"complete non-empty payload size={len(data)}", "WARNING"


def read_entries(repo: Path, manifest: Path) -> list[tuple[Path, str]]:
    result: list[tuple[Path, str]] = []
    for entry in read_preload_entries(repo, manifest):
        source, separator, destination = entry.partition("@")
        source_path = repo / source.strip()
        virtual = destination.strip() if separator else source.strip()
        result.append((source_path, norm_virtual(virtual)))
    # Music is deliberately streamed rather than preloaded, but remains part
    # of the files loaded by the client and therefore belongs in this audit.
    for source_path in sorted((repo / "v769ClientRelease/music").glob("*.mp3")):
        result.append((source_path, norm_virtual(f"music/{source_path.name}")))
    # The linker adds these two generated runtime files after expanding the
    # manifest.  Audit them when a local build has generated them.
    generated = repo / "webclient/client-wasm/build/generated/gdi-font"
    for name in ("openwyd_gdi_tahoma12_a4.bin", "openwyd_gdi_tahoma12_a4.json"):
        source_path = generated / name
        if source_path.exists():
            result.append((source_path, norm_virtual(name)))
    # Finally enumerate every remaining physical file from the distributed
    # client tree.  These are not placed in the virtual filesystem, but they
    # are still hashed and structurally checked so the report proves that no
    # shipped file was silently skipped.  The special virtual prefix keeps
    # them from satisfying active runtime references.
    known_sources = {source.resolve() for source, _ in result}
    release_root = repo / "v769ClientRelease"
    for source_path in sorted(path for path in release_root.rglob("*") if path.is_file()):
        if source_path.resolve() in known_sources:
            continue
        relative = source_path.relative_to(release_root).as_posix()
        result.append((source_path, norm_virtual(f"__not_wasm__/{relative}")))
    return result


def audit_catalog_references(
    items: list[AuditItem], repo: Path, virtual_paths: set[str]
) -> list[Reference]:
    active_catalogs = {
        "/ui/uitexturelistn.bin",
        "/effect/effecttexturelist.bin",
        "/mesh/meshtexturelist.bin",
        "/env/envtexturelist3.bin",
    }
    references: list[Reference] = []
    for item in items:
        name = Path(item.virtual_path).name.lower()
        if "texturelist" not in name or not item.virtual_path.lower().endswith(".bin"):
            continue
        data = (repo / item.source).read_bytes()
        stride = 528 if len(data) % 528 == 0 else (264 if len(data) % 264 == 0 else 260)
        field = 255 if stride == 528 else 128
        for index in range(len(data) // stride):
            record = data[index * stride : (index + 1) * stride]
            target = cstring(record[:field])
            if not target:
                continue
            normalized = norm_virtual(target).lower()
            present = normalized in virtual_paths
            active = item.virtual_path.lower() in active_catalogs
            references.append(
                Reference(
                    owner=f"{item.virtual_path}#{index}",
                    target=norm_virtual(target),
                    status="OK" if present else ("MISSING" if active else "LEGACY_MISSING"),
                    detail=(
                        "active catalog target packaged"
                        if present and active
                        else "legacy catalog target packaged"
                        if present
                        else "active catalog target absent from checkout/bundle"
                        if active
                        else "inactive legacy catalog target absent from checkout/bundle"
                    ),
                )
            )
    return references


def audit_text_list_references(
    items: list[AuditItem], repo: Path, virtual_paths: set[str]
) -> list[Reference]:
    references: list[Reference] = []
    candidates = {
        "/music.txt": (re.compile(r"\b(music[\\/][^\s]+\.mp3)\b", re.I), "music", True),
        "/sound/soundlist.txt": (re.compile(r"\b(sound[\\/][^\s]+\.wav)\b", re.I), "sound", True),
        "/mesh/meshlist.txt": (re.compile(r"\b((?:mesh|effect)[\\/][^\s]+\.msa)\b", re.I), "mesh list", True),
        "/mesh/commonmeshlist.txt": (re.compile(r"\b((?:mesh|effect)[\\/][^\s]+\.msa)\b", re.I), "common mesh list", False),
    }
    by_virtual = {item.virtual_path.lower(): item for item in items}
    for virtual, (pattern, label, active) in candidates.items():
        item = by_virtual.get(virtual)
        if not item:
            continue
        data = (repo / item.source).read_bytes()
        text = data.decode("cp1252", errors="replace")
        for line_no, line in enumerate(text.splitlines(), 1):
            match = pattern.search(line)
            if not match:
                continue
            target = norm_virtual(match.group(1))
            present = target.lower() in virtual_paths
            references.append(
                Reference(
                    owner=f"{virtual}:{line_no}",
                    target=target,
                    status="OK" if present else ("MISSING" if active else "LEGACY_MISSING"),
                    detail=(
                        f"{'active' if active else 'inactive legacy'} {label} target "
                        f"{'packaged' if present else 'absent from checkout/bundle'}"
                    ),
                )
            )
    return references


def audit_static_runtime_references(virtual_paths: set[str]) -> list[Reference]:
    """Check literal startup/scene inputs that do not come from a catalog.

    These paths were transcribed from TMPaths.h and the direct fopen/LoadRC
    calls in NewApp, Basedef, MeshManager, ObjectManager and the scene classes.
    Dynamic terrain/mesh/texture paths are covered by the manifest itself and
    by the catalog/list checks above.
    """

    required = {
        "/sn.bin": "NewApp::InitServerName",
        "/serverlist.bin": "NewApp::BASE_Initialize_NewServerList",
        "/Config.bin": "NewApp::Initialize",
        "/itemhelp.dat": "NewApp::Initialize",
        "/Mixhelp.dat": "NewApp::MixHelp",
        "/Music.txt": "NewApp::InitMusicList",
        "/ItemPrice.bin": "BASE_ReadItemPrice",
        "/itemicon.bin": "ReadItemicon",
        "/Itemname.bin": "ReadItemName",
        "/ItemList.bin": "BASE_ReadItemList",
        "/SkillData.bin": "BASE_ReadSkillBin",
        "/AttributeMap.dat": "BASE_InitializeAttribute",
        "/object.bin": "MeshManager::ReadObjectMask",
        "/cdata.bin": "TMGround::InitCheckSum",
        "/AniSound4.txt": "ObjectManager::InitAniSoundTable",
        "/minimap.dat": "TMFieldScene::InitializeScene",
        "/Mixlist.bin": "CItemMix::Load",
        "/Sound/soundlist.txt": "CSoundManager::LoadSoundData",
        "/Mesh/MeshList.txt": "MeshManager::InitMeshManager",
        "/Mesh/BoneAni4.txt": "MeshManager::InitBoneAnimation",
        "/Mesh/ValidIndex.bin": "MeshManager::InitBoneAnimation",
        "/UI/UITextureListN.bin": "TextureManager::InitUITextureList",
        "/UI/UITextureSetList.txt": "TextureManager::InitUITextureSetList",
        "/Effect/EffectTextureList.bin": "TextureManager::InitEffectTextureList",
        "/Mesh/MeshTextureList.bin": "TextureManager::InitModelTextureList",
        "/Env/EnvTextureList3.bin": "TextureManager::InitEnvTextureList",
        "/UI/strdef.bin": "BASE_ReadMessageBin",
        "/UI/UIString.txt": "ReadUIString",
        "/UI/EffectString.txt": "BASE_InitEffectString",
        "/UI/EffectSubString.txt": "BASE_InitEffectString",
        "/UI/RC.bin": "ObjectManager::InitResourceList",
        "/UI/selchar.txt": "ObjectManager/TMSelectCharScene",
        "/UI/SelServerScene2.bin": "TMSelectServerScene::LoadRC",
        "/UI/SelCharScene2.bin": "TMSelectCharScene::LoadRC",
        "/UI/FieldScene2.bin": "TMFieldScene::LoadRC",
        "/UI/LoginScene.bin": "WASM diagnostic scene LoadRC",
        "/UI/LoginScene2.bin": "WASM diagnostic scene LoadRC",
        "/UI/DemoScene.bin": "WASM diagnostic scene LoadRC",
        "/UI/EndDemo.bin": "TMDemoScene::InitializeScene",
        "/UI/TimeTable.bin": "TMDemoScene::InitializeScene",
        "/UI/Ending.bin": "TMDemoScene::InitializeScene",
        "/UI/demo.bin": "TMSelectServerScene::InitializeScene",
        "/UI/demo2.bin": "TMSelectServerScene::InitializeScene",
        "/UI/demo3.bin": "TMSelectServerScene::InitializeScene",
        "/UI/demo4.bin": "TMSelectServerScene::InitializeScene",
        "/UI/TOTOGame.csv": "TMFieldScene/BASE_ReadTOTOList",
        "/UI/mix4desc.txt": "TMFieldScene::LoadMsgText",
        "/UI/hellStoredesc.txt": "TMFieldScene::LoadMsgText2",
        "/UI/PotalPos.txt": "TMFieldScene::LoadMsgText2",
        "/UI/interface.txt": "TMFieldScene::LoadMsgText",
        "/UI/command.txt": "TMFieldScene::LoadMsgText",
        "/UI/etc.txt": "TMFieldScene::LoadMsgText",
        "/UI/interface1.txt": "TMFieldScene::LoadMsgText",
        "/UI/interface2.txt": "TMFieldScene::LoadMsgText",
        "/UI/interface3.txt": "TMFieldScene::LoadMsgText",
        "/UI/QuestSubjects.txt": "TMFieldScene quest readers",
        "/UI/QuestSubjects2.txt": "TMFieldScene quest readers",
        "/UI/QuestSubjects3.txt": "TMFieldScene quest readers",
        "/UI/QuestSubjects4.txt": "TMFieldScene quest readers",
        "/UI/QuestContents.txt": "TMFieldScene quest readers",
        "/UI/QuestContents2.txt": "TMFieldScene quest readers",
        "/UI/QuestContents3.txt": "TMFieldScene quest readers",
        "/UI/QuestContents4.txt": "TMFieldScene quest readers",
        "/UI/QuestMessage.txt": "TMFieldScene/TMHuman quest readers",
        "/UI/chardesctrans.txt": "TMSelectCharScene::LoadMsgText",
        "/UI/chardescfoema.txt": "TMSelectCharScene::LoadMsgText",
        "/UI/chardescbeast.txt": "TMSelectCharScene::LoadMsgText",
        "/UI/chardeschunter.txt": "TMSelectCharScene::LoadMsgText",
        "/notice.txt": "TMFieldScene::LoadMsgText",
    }
    optional = {
        "/sn2.bin": "NewApp::InitServerName2 tolerates an absent secondary name table",
        "/font.txt": "RenderDevice defaults to Tahoma/weight 500",
        "/WYD.avi": "NewApp discards the optional video player when the clip is absent",
        "/Mesh/tn.dat": "legacy encrypted BGM slot 14; not used by the WASM HTMLAudio path",
        "/Mesh/ed.dat": "legacy encrypted BGM slot 13; not used by the WASM HTMLAudio path",
        "/Mesh/hs010301.msh": "TMSkinMesh generates this part for bone profile 31; official package omits it and failed LoadMesh is explicitly discarded",
    }
    references: list[Reference] = []
    for target, owner in sorted(required.items(), key=lambda value: value[0].lower()):
        present = target.lower() in virtual_paths
        references.append(
            Reference(
                owner=owner,
                target=target,
                status="OK" if present else "MISSING",
                detail="required direct runtime input packaged" if present else "required direct runtime input absent",
            )
        )
    for target, owner in sorted(optional.items(), key=lambda value: value[0].lower()):
        present = target.lower() in virtual_paths
        references.append(
            Reference(
                owner=owner,
                target=target,
                status="OK" if present else "OPTIONAL_MISSING",
                detail="optional runtime input packaged" if present else "optional fallback verified in loading code",
            )
        )
    return references


def audit_loader_sites(repo: Path) -> list[LoaderSite]:
    """Inventory every compiled TMProject file-I/O call for review traceability."""

    expression = re.compile(
        r"\b(fopen_s|fopen|_open|CreateFileA|CreateFileW|CreateFile|ifstream|"
        r"fstream|D3DXCreateTextureFromFile|D3DXCreateTextureFromFileInMemory|"
        r"LoadFromFile|ReadFile)\s*\("
    )
    sites: list[LoaderSite] = []
    source_root = repo / "Projects/TMProject"
    for source_path in sorted((*source_root.glob("*.cpp"), *source_root.glob("*.h"))):
        relative = source_path.relative_to(repo).as_posix()
        text = source_path.read_text(encoding="utf-8", errors="replace")
        for line_number, line in enumerate(text.splitlines(), 1):
            match = expression.search(line)
            if not match:
                continue
            lower_source = source_path.name.lower()
            lower_line = line.lower()
            if lower_source in ("openwydcompare.cpp", "openwydlab.cpp"):
                role = "debug transport/artifact; not a game asset input"
            elif re.search(r'"w[bt]?"', lower_line) or "log_path" in lower_line:
                role = "write/output path; not a game asset input"
            else:
                role = "runtime input loader or delegated file reader"
            sites.append(
                LoaderSite(
                    source=relative,
                    line=line_number,
                    call=match.group(1),
                    role=role,
                    snippet=line.strip(),
                )
            )
    return sites


def audit_msa_texture_references(
    items: list[AuditItem], repo: Path, virtual_paths: set[str]
) -> list[Reference]:
    """Reproduce TMMesh::LoadMsa texture-name and own-name fallback lookup."""

    by_virtual = {item.virtual_path.lower(): item for item in items}
    active_meshes: set[str] = set()
    mesh_list = by_virtual.get("/mesh/meshlist.txt")
    if mesh_list:
        text = (repo / mesh_list.source).read_bytes().decode("cp1252", errors="replace")
        for match in re.finditer(r"\b((?:mesh|effect)[\\/][^\s]+\.msa)\b", text, re.I):
            active_meshes.add(norm_virtual(match.group(1)).lower())

    references: list[Reference] = []
    for item in items:
        if item.format != "TMMesh":
            continue
        data = (repo / item.source).read_bytes()
        attributes = u32(data, 8)
        offset = 12 + attributes * 20
        folder = "/Effect/" if item.virtual_path.lower().startswith("/effect/") else "/Mesh/"
        own_stem = Path(item.virtual_path).stem
        active = item.virtual_path.lower() in active_meshes
        for index in range(attributes):
            raw = data[offset + index * 11 : offset + (index + 1) * 11]
            raw = raw.split(b"\0", 1)[0].replace(b"\\", b"/").rsplit(b"/", 1)[-1]
            stem = raw.split(b".", 1)[0].decode("cp1252", errors="replace")
            primary = norm_virtual(f"{folder}{stem}.wys")
            fallback = norm_virtual(f"{folder}{own_stem}.wys")
            primary_present = primary.lower() in virtual_paths
            fallback_present = fallback.lower() in virtual_paths
            present = primary_present or fallback_present
            references.append(
                Reference(
                    owner=f"{item.virtual_path}@texture[{index}]",
                    target=primary,
                    status="OK" if present else ("MISSING" if active else "LEGACY_MISSING"),
                    detail=(
                        "TMMesh embedded texture packaged"
                        if primary_present
                        else f"TMMesh own-name fallback packaged as {fallback}"
                        if fallback_present
                        else "active TMMesh texture and own-name fallback both absent"
                        if active
                        else "inactive legacy TMMesh texture and own-name fallback both absent"
                    ),
                )
            )
    return references


def audit_bone_animation_references(
    items: list[AuditItem], repo: Path, virtual_paths: set[str]
) -> list[Reference]:
    """Reproduce MeshManager::InitBoneAnimation's BON/ANI filename generation."""

    by_virtual = {item.virtual_path.lower(): item for item in items}
    bone_list = by_virtual.get("/mesh/boneani4.txt")
    valid_index = by_virtual.get("/mesh/validindex.bin")
    if not bone_list or not valid_index:
        return []
    text = (repo / bone_list.source).read_bytes().decode("cp1252", errors="replace")
    valid_data = (repo / valid_index.source).read_bytes()
    valid_values = struct.unpack("<18600i", valid_data)
    references: list[Reference] = []
    for line_no, line in enumerate(text.splitlines(), 1):
        fields = line.split()
        if len(fields) < 4:
            continue
        index, animation_count = int(fields[0]), int(fields[1])
        prefix = fields[3].replace("\\", "/")
        bone = norm_virtual(f"{prefix}.bon")
        bone_present = bone.lower() in virtual_paths
        references.append(
            Reference(
                owner=f"/Mesh/BoneAni4.txt:{line_no}",
                target=bone,
                status="OK" if bone_present else "MISSING",
                detail="active bone table packaged" if bone_present else "active bone table absent",
            )
        )
        if not 0 <= index < 100 or not 0 <= animation_count <= 186:
            continue
        for animation_slot in range(animation_count):
            animation_id = valid_values[index * 186 + animation_slot] + 1
            animation = norm_virtual(f"{prefix}{animation_id:04d}.ani")
            present = animation.lower() in virtual_paths
            references.append(
                Reference(
                    owner=f"/Mesh/ValidIndex.bin[{index}][{animation_slot}]",
                    target=animation,
                    status="OK" if present else "MISSING",
                    detail="active animation packaged" if present else "active animation absent",
                )
            )
    return references


def write_reports(
    output_json: Path,
    output_markdown: Path,
    items: list[AuditItem],
    references: list[Reference],
    loader_sites: list[LoaderSite],
    manifest: Path,
) -> None:
    status_counts = Counter(item.status for item in items)
    format_counts = Counter(item.format for item in items)
    missing = [reference for reference in references if reference.status == "MISSING"]
    legacy_missing = [reference for reference in references if reference.status == "LEGACY_MISSING"]
    optional_missing = [reference for reference in references if reference.status == "OPTIONAL_MISSING"]
    physical_release_files = sum(
        item.source.lower().startswith("v769clientrelease/") for item in items
    )
    excluded_files = status_counts.get("EXCLUDED", 0)
    payload = {
        "schema": 3,
        "manifest": manifest.as_posix(),
        "summary": {
            "files": len(items),
            "physical_release_files": physical_release_files,
            "wasm_runtime_files": len(items) - excluded_files,
            "excluded_platform_files": excluded_files,
            "bytes": sum(item.size for item in items),
            "status": dict(sorted(status_counts.items())),
            "formats": dict(sorted(format_counts.items())),
            "references": len(references),
            "missing_references": len(missing),
            "legacy_missing_references": len(legacy_missing),
            "optional_missing_references": len(optional_missing),
            "loader_call_sites": len(loader_sites),
        },
        "files": [asdict(item) for item in items],
        "references": [asdict(reference) for reference in references],
        "loader_sites": [asdict(site) for site in loader_sites],
    }
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    grouped: dict[str, list[AuditItem]] = defaultdict(list)
    for item in items:
        head = item.virtual_path.strip("/").split("/", 1)[0] or "root"
        grouped[head].append(item)

    lines = [
        "# OpenWyd WASM — checklist exaustivo dos arquivos do cliente",
        "",
        "> Gerado por `webclient/client-wasm/tools/audit_client_assets.py`. "
        "`[x]` significa que o arquivo foi aberto, hasheado e inspecionado pelo "
        "validador indicado. `WARNING` identifica payload legado/sem loader ativo; "
        "`EXCLUDED` identifica componentes Windows/launcher conferidos e "
        "intencionalmente ausentes do VFS; nenhum deles foi ignorado.",
        "",
        "## Resumo",
        "",
        f"- Arquivos verificados: **{len(items)}**",
        f"- Arquivos físicos da distribuição: **{physical_release_files}** (nenhum omitido)",
        f"- Arquivos do runtime WASM/stream/gerados: **{len(items) - excluded_files}**",
        f"- Componentes Windows/launcher excluídos do VFS: **{excluded_files}**",
        f"- Bytes verificados: **{sum(item.size for item in items):,}**",
        f"- Resultados: **{dict(sorted(status_counts.items()))}**",
        f"- Referências internas verificadas: **{len(references)}**",
        f"- Referências ativas ausentes: **{len(missing)}**",
        f"- Referências ausentes em catálogos legados inativos: **{len(legacy_missing)}**",
        f"- Entradas opcionais ausentes com fallback conferido: **{len(optional_missing)}**",
        f"- Pontos de I/O do cliente C++ revisados: **{len(loader_sites)}**",
        "",
        "## Cobertura por formato",
        "",
    ]
    for fmt, count in sorted(format_counts.items(), key=lambda value: (-value[1], value[0])):
        lines.append(f"- [x] `{fmt}` — {count} arquivo(s)")
    lines.extend(["", "## Pontos de carregamento no C++", ""])
    for site in loader_sites:
        snippet = site.snippet.replace("`", "'")
        lines.append(
            f"- [x] `{site.source}:{site.line}` `{site.call}` — {site.role}; `{snippet}`"
        )
    lines.extend(["", "## Lista individual", ""])
    for group in sorted(grouped, key=str.lower):
        lines.extend([f"### `{group}`", ""])
        for item in sorted(grouped[group], key=lambda value: value.virtual_path.lower()):
            short_hash = item.sha256[:16]
            detail = item.detail.replace("\n", " ").replace("|", "/")
            lines.append(
                f"- [x] `{item.status}` `{item.virtual_path}` — {item.size} B; "
                f"SHA-256 `{short_hash}…`; `{item.format}`; {item.loader}; {detail}"
            )
        lines.append("")

    lines.extend(["## Referências declaradas mas ausentes", ""])
    if missing:
        lines.append(
            "Esses caminhos não foram aprovados: são referências de catálogos/listas "
            "oficiais para arquivos que não existem no checkout nem no bundle."
        )
        lines.append("")
        for reference in sorted(missing, key=lambda value: (value.target.lower(), value.owner.lower())):
            lines.append(f"- [ ] `{reference.target}` — origem `{reference.owner}`; {reference.detail}")
    else:
        lines.append("- [x] Nenhuma referência ausente.")
    lines.extend(["", "## Ausências legadas ou opcionais", ""])
    for reference in sorted(
        [*legacy_missing, *optional_missing],
        key=lambda value: (value.status, value.target.lower(), value.owner.lower()),
    ):
        lines.append(
            f"- [x] `{reference.status}` `{reference.target}` — origem `{reference.owner}`; "
            f"{reference.detail}"
        )
    if not legacy_missing and not optional_missing:
        lines.append("- [x] Nenhuma ausência legada ou opcional.")
    lines.append("")
    output_markdown.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=SCRIPT_DIR.parents[2])
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("webclient/client-wasm/config/startup-preload-manifest.txt"),
    )
    parser.add_argument("--json", type=Path, default=Path("docs/wasm-asset-audit.json"))
    parser.add_argument("--checklist", type=Path, default=Path("docs/wasm-asset-checklist.md"))
    args = parser.parse_args()
    repo = args.repo.resolve()
    manifest = args.manifest if args.manifest.is_absolute() else repo / args.manifest

    entries = read_entries(repo, manifest)
    destinations = Counter(virtual.lower() for _, virtual in entries)
    duplicate_destinations = [name for name, count in destinations.items() if count > 1]
    if duplicate_destinations:
        print(f"[asset-audit] duplicate virtual destinations: {duplicate_destinations[:20]}", file=sys.stderr)
        return 2

    items: list[AuditItem] = []
    for index, (source_path, virtual) in enumerate(entries, 1):
        relative = source_path.resolve().relative_to(repo).as_posix()
        try:
            data = source_path.read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            fmt, detail, status = classify_and_validate(virtual, data)
            loader = loader_for(virtual, fmt)
        except Exception as exc:  # continue so the checklist remains exhaustive
            data = source_path.read_bytes() if source_path.exists() else b""
            digest = hashlib.sha256(data).hexdigest()
            fmt, detail, status = "invalid", str(exc), "ERROR"
            loader = "validator failed before loader compatibility could be certified"
        items.append(
            AuditItem(relative, virtual, len(data), digest, fmt, loader, status, detail)
        )
        if index % 500 == 0:
            print(f"[asset-audit] inspected {index}/{len(entries)}")

    virtual_paths = {item.virtual_path.lower() for item in items}
    references = audit_catalog_references(items, repo, virtual_paths)
    references.extend(audit_text_list_references(items, repo, virtual_paths))
    references.extend(audit_msa_texture_references(items, repo, virtual_paths))
    references.extend(audit_bone_animation_references(items, repo, virtual_paths))
    references.extend(audit_static_runtime_references(virtual_paths))
    loader_sites = audit_loader_sites(repo)
    output_json = args.json if args.json.is_absolute() else repo / args.json
    output_markdown = args.checklist if args.checklist.is_absolute() else repo / args.checklist
    write_reports(
        output_json,
        output_markdown,
        items,
        references,
        loader_sites,
        manifest.relative_to(repo),
    )

    counts = Counter(item.status for item in items)
    missing_count = sum(reference.status == "MISSING" for reference in references)
    legacy_missing_count = sum(reference.status == "LEGACY_MISSING" for reference in references)
    optional_missing_count = sum(reference.status == "OPTIONAL_MISSING" for reference in references)
    print(
        f"[asset-audit] files={len(items)} bytes={sum(item.size for item in items)} "
        f"status={dict(counts)} references={len(references)} missing={missing_count} "
        f"legacy_missing={legacy_missing_count} optional_missing={optional_missing_count}"
    )
    print(f"[asset-audit] report={output_json}")
    print(f"[asset-audit] checklist={output_markdown}")
    return 1 if counts.get("ERROR", 0) else 0


if __name__ == "__main__":
    raise SystemExit(main())
