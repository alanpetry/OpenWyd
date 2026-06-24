#!/usr/bin/env python3
from __future__ import annotations

import json
import mimetypes
import os
import posixpath
import re
import struct
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from flask import Flask, abort, jsonify, request, send_file, send_from_directory

ROOT_DIR = Path(__file__).resolve().parents[2]
WEBCLIENT_DIR = ROOT_DIR / "webclient"
GENERATED_DIR = WEBCLIENT_DIR / "generated"
RELEASE_DIR = ROOT_DIR / "v769ClientRelease"
APP_DIR = WEBCLIENT_DIR / "app"

ASSET_MANIFEST_PATH = GENERATED_DIR / "asset-manifest.json"
ASSET_INDEX_PATH = GENERATED_DIR / "asset-index.json"
BOOTSTRAP_MANIFEST_PATH = GENERATED_DIR / "bootstrap-manifest.json"
BONE_ANI4_PATH = RELEASE_DIR / "Mesh" / "BoneAni4.txt"
VALID_INDEX_PATH = RELEASE_DIR / "Mesh" / "ValidIndex.bin"
MESH_LIST_PATH = RELEASE_DIR / "Mesh" / "MeshList.txt"

MAX_VALID_ANI_COLUMNS = 186
MAX_BONE_ANIMATION_LIST = 100
MAX_TILE_DATA_COUNT = 64 * 64
MAX_OBJECT_LIST = 4096


@dataclass(frozen=True)
class ResolvedAsset:
    requested_path: str
    normalized_path: str
    resolved_path: str
    absolute_path: Path


def _load_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        raise FileNotFoundError(
            f"Missing generated file: {path}. Run `python3 webclient/tools/generate_asset_manifest.py` first."
        )

    return json.loads(path.read_text(encoding="utf-8"))


ASSET_MANIFEST = _load_json(ASSET_MANIFEST_PATH)
ASSET_INDEX = _load_json(ASSET_INDEX_PATH)
BOOTSTRAP_MANIFEST = _load_json(BOOTSTRAP_MANIFEST_PATH)
CASE_INDEX: dict[str, str] = ASSET_INDEX["pathByLowercase"]

app = Flask(__name__, static_folder=str(APP_DIR), static_url_path="/app")


def _build_mesh_catalog() -> list[dict[str, Any]]:
    files = ASSET_MANIFEST.get("files", [])
    catalog: list[dict[str, Any]] = []

    for file_info in files:
        path = file_info.get("path", "")
        if not path.lower().startswith("mesh/"):
            continue
        if not path.lower().endswith(".msh"):
            continue

        base = path[:-4]
        has_wys = f"{base}.wys".lower() in CASE_INDEX
        has_wyt = f"{base}.wyt".lower() in CASE_INDEX
        has_ani = f"{base}.ani".lower() in CASE_INDEX

        catalog.append(
            {
                "path": path.replace("/", "\\"),
                "assetPath": path,
                "size": file_info.get("size"),
                "hasWys": has_wys,
                "hasWyt": has_wyt,
                "hasAni": has_ani,
            }
        )

    catalog.sort(key=lambda item: item["assetPath"].lower())
    return catalog


MESH_CATALOG = _build_mesh_catalog()


def _build_field_minimap_candidates(field_name: str) -> list[str]:
    stem = Path(field_name).stem.lower()
    match = re.match(r"^field(\d{4})$", stem)
    if not match:
        return []

    field_code = match.group(1)
    return [
        f"UI/m{field_code}.wyt",
        f"UI/m{field_code}.wys",
    ]


def _build_field_minimap_payload(field_name: str) -> dict[str, Any]:
    candidates_payload: list[dict[str, Any]] = []
    resolved_path = ""
    resolved_size = 0

    for candidate in _build_field_minimap_candidates(field_name):
        resolved = CASE_INDEX.get(candidate.lower())
        exists = resolved is not None
        resolved_case = resolved.replace("/", "\\") if resolved else ""
        size = 0
        if exists:
            absolute = RELEASE_DIR / resolved
            try:
                size = int(absolute.stat().st_size)
            except OSError:
                size = 0

            if not resolved_path:
                resolved_path = resolved_case
                resolved_size = size

        candidates_payload.append(
            {
                "requestedPath": candidate.replace("/", "\\"),
                "resolvedPath": resolved_case,
                "exists": exists,
                "size": size,
            }
        )

    return {
        "fieldName": field_name,
        "resolvedPath": resolved_path,
        "resolvedSize": resolved_size,
        "candidates": candidates_payload,
    }


def _build_field_catalog() -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]]]:
    by_name: dict[str, dict[str, Any]] = {}
    files = ASSET_MANIFEST.get("files", [])

    for file_info in files:
        path = file_info.get("path", "")
        lower = path.lower()
        if not lower.startswith("env/"):
            continue
        if not (lower.endswith(".trn") or lower.endswith(".dat") or lower.endswith(".map")):
            continue

        stem = Path(path).stem
        stem_lower = stem.lower()
        if not re.match(r"^(field\d{4}|character)$", stem_lower):
            continue

        entry = by_name.setdefault(
            stem_lower,
            {
                "name": stem,
                "nameLower": stem_lower,
                "trnPath": "",
                "datPath": "",
                "mapPath": "",
                "trnSize": 0,
                "datSize": 0,
                "mapSize": 0,
            },
        )

        ext = Path(path).suffix.lower()
        size = int(file_info.get("size") or 0)
        if ext == ".trn":
            entry["trnPath"] = path.replace("/", "\\")
            entry["trnSize"] = size
        elif ext == ".dat":
            entry["datPath"] = path.replace("/", "\\")
            entry["datSize"] = size
        elif ext == ".map":
            entry["mapPath"] = path.replace("/", "\\")
            entry["mapSize"] = size

    fields = sorted(by_name.values(), key=lambda item: item["nameLower"])
    for item in fields:
        item["hasTrn"] = bool(item["trnPath"])
        item["hasDat"] = bool(item["datPath"])
        item["hasMap"] = bool(item["mapPath"])
        minimap_info = _build_field_minimap_payload(item["name"])
        item["hasMinimap"] = bool(minimap_info["resolvedPath"])
        item["minimapPath"] = minimap_info["resolvedPath"]
    return fields, by_name


FIELD_CATALOG, FIELD_BY_NAME = _build_field_catalog()


def _normalize_asset_path(path: str) -> str:
    normalized = path.replace("\\", "/").strip()
    normalized = normalized.lstrip("/")
    normalized = posixpath.normpath(normalized)
    if normalized in ("", "."):
        return ""
    if normalized.startswith("../") or normalized == "..":
        return ""
    return normalized


def _extract_stem_from_path(path: str) -> str:
    normalized = path.replace("\\", "/")
    name = Path(normalized).name
    stem = Path(name).stem
    return stem


def _resolve_case_path(path: str) -> str:
    normalized = _normalize_asset_path(path)
    if not normalized:
        return ""
    resolved = CASE_INDEX.get(normalized.lower())
    if resolved is not None:
        return resolved
    return normalized


def _build_object_mesh_map() -> tuple[dict[int, dict[str, Any]], list[dict[str, Any]]]:
    by_type: dict[int, dict[str, Any]] = {}
    entries: list[dict[str, Any]] = []

    if not MESH_LIST_PATH.exists():
        return by_type, entries

    lines = MESH_LIST_PATH.read_text(encoding="latin1", errors="ignore").splitlines()
    for raw_line in lines:
        line = raw_line.strip()
        if not line:
            continue

        parts = line.split()
        if len(parts) < 2:
            continue

        try:
            obj_type = int(parts[0])
        except ValueError:
            continue

        source_path_raw = parts[1].strip()
        source_path = _normalize_asset_path(source_path_raw)
        if not source_path:
            continue

        resolved_mesh = _resolve_case_path(source_path)
        exists = bool(resolved_mesh and (RELEASE_DIR / resolved_mesh).exists())
        source_group = source_path.split("/", 1)[0].lower() if "/" in source_path else ""
        stem = _extract_stem_from_path(source_path)

        texture_candidates: list[dict[str, Any]] = []
        if stem and source_group in {"mesh", "effect"}:
            for ext in (".wys", ".wyt"):
                requested = f"{source_group}/{stem}{ext}"
                resolved_texture = _resolve_case_path(requested)
                texture_exists = bool(resolved_texture and (RELEASE_DIR / resolved_texture).exists())
                texture_candidates.append(
                    {
                        "requestedPath": requested.replace("/", "\\"),
                        "resolvedPath": resolved_texture.replace("/", "\\"),
                        "exists": texture_exists,
                    }
                )

        entry = {
            "objType": obj_type,
            "sourcePath": source_path.replace("/", "\\"),
            "resolvedPath": resolved_mesh.replace("/", "\\"),
            "exists": exists,
            "sourceGroup": source_group,
            "stem": stem,
            "textureCandidates": texture_candidates,
        }

        by_type[obj_type] = entry
        entries.append(entry)

    entries.sort(key=lambda item: item["objType"])
    return by_type, entries


OBJECT_MESH_BY_TYPE, OBJECT_MESH_ENTRIES = _build_object_mesh_map()


def _build_related_asset_indexes() -> dict[str, dict[str, list[str]]]:
    ani_by_stem: dict[str, list[str]] = {}
    ani_by_prefix4: dict[str, list[str]] = {}
    ani_by_prefix6: dict[str, list[str]] = {}

    bon_by_stem: dict[str, list[str]] = {}
    bon_by_prefix4: dict[str, list[str]] = {}
    bon_by_prefix6: dict[str, list[str]] = {}

    def append_index(index: dict[str, list[str]], key: str, path: str) -> None:
        bucket = index.get(key)
        if bucket is None:
            index[key] = [path]
            return
        if path not in bucket:
            bucket.append(path)

    files = ASSET_MANIFEST.get("files", [])
    for file_info in files:
        path = file_info.get("path", "")
        lower = path.lower()
        if not lower.startswith("mesh/"):
            continue

        stem = Path(path).stem.lower()
        if len(stem) < 4:
            continue
        prefix4 = stem[:4]
        prefix6 = stem[:6]

        if lower.endswith(".ani"):
            append_index(ani_by_stem, stem, path.replace("/", "\\"))
            append_index(ani_by_prefix4, prefix4, path.replace("/", "\\"))
            append_index(ani_by_prefix6, prefix6, path.replace("/", "\\"))
        elif lower.endswith(".bon"):
            append_index(bon_by_stem, stem, path.replace("/", "\\"))
            append_index(bon_by_prefix4, prefix4, path.replace("/", "\\"))
            append_index(bon_by_prefix6, prefix6, path.replace("/", "\\"))

    for index in (
        ani_by_stem,
        ani_by_prefix4,
        ani_by_prefix6,
        bon_by_stem,
        bon_by_prefix4,
        bon_by_prefix6,
    ):
        for key, bucket in index.items():
            bucket.sort(key=lambda value: value.lower())

    return {
        "ani_by_stem": ani_by_stem,
        "ani_by_prefix4": ani_by_prefix4,
        "ani_by_prefix6": ani_by_prefix6,
        "bon_by_stem": bon_by_stem,
        "bon_by_prefix4": bon_by_prefix4,
        "bon_by_prefix6": bon_by_prefix6,
    }


RELATED_INDEXES = _build_related_asset_indexes()


def _read_ani_header(asset_path: str) -> dict[str, int] | None:
    absolute = RELEASE_DIR / asset_path
    try:
        with absolute.open("rb") as fp:
            header = fp.read(8)
    except OSError:
        return None

    if len(header) < 8:
        return None

    tick_count, bones_per_frame = struct.unpack("<II", header)
    return {
        "tickCount": int(tick_count),
        "bonesPerFrame": int(bones_per_frame),
    }


def _parse_valid_index_table() -> list[list[int]]:
    if not VALID_INDEX_PATH.exists():
        return [[0] * MAX_VALID_ANI_COLUMNS for _ in range(MAX_BONE_ANIMATION_LIST)]

    raw = VALID_INDEX_PATH.read_bytes()
    int_count = len(raw) // 4
    if int_count <= 0:
        return [[0] * MAX_VALID_ANI_COLUMNS for _ in range(MAX_BONE_ANIMATION_LIST)]

    values = struct.unpack(f"<{int_count}i", raw[: int_count * 4])
    rows: list[list[int]] = []

    for row_index in range(MAX_BONE_ANIMATION_LIST):
        start = row_index * MAX_VALID_ANI_COLUMNS
        end = start + MAX_VALID_ANI_COLUMNS
        if end <= int_count:
            rows.append(list(values[start:end]))
        else:
            rows.append([0] * MAX_VALID_ANI_COLUMNS)

    return rows


def _compact_animation_record(record: dict[str, Any]) -> dict[str, Any]:
    return {
        "tableIndex": record["tableIndex"],
        "arrayIndex": record["arrayIndex"],
        "weaponType": record["weaponType"],
        "motionIndex": record["motionIndex"],
        "aniPath": record["aniPath"],
        "exists": record["exists"],
        "tickCount": record.get("tickCount"),
        "bonesPerFrame": record.get("bonesPerFrame"),
    }


def _summarize_bone_animation_entry(entry: dict[str, Any]) -> dict[str, Any]:
    return {
        "index": entry["index"],
        "aniTypeCount": entry["aniTypeCount"],
        "parts": entry["parts"],
        "basePath": entry["basePath"],
        "baseStem": entry["baseStem"],
        "bonPath": entry["bonPath"],
        "bonExists": entry["bonExists"],
        "existingAniCount": entry["existingAniCount"],
        "availableWeaponTypes": entry["availableWeaponTypes"],
        "availableMotionCount": entry["availableMotionCount"],
    }


def _build_bone_animation_catalog() -> dict[str, Any]:
    valid_index_table = _parse_valid_index_table()
    ani_header_cache: dict[str, dict[str, int] | None] = {}

    entries: list[dict[str, Any]] = []
    entries_by_index: dict[int, dict[str, Any]] = {}
    entries_by_stem: dict[str, list[dict[str, Any]]] = {}

    if not BONE_ANI4_PATH.exists():
        return {
            "loaded": False,
            "source": {
                "boneAni4Path": str(BONE_ANI4_PATH),
                "validIndexPath": str(VALID_INDEX_PATH),
            },
            "entries": entries,
            "entriesByIndex": entries_by_index,
            "entriesByStem": entries_by_stem,
        }

    lines = BONE_ANI4_PATH.read_text(encoding="utf-8", errors="ignore").splitlines()
    for raw_line in lines:
        line = raw_line.strip()
        if not line:
            continue

        parts = line.split()
        if len(parts) < 4:
            continue

        try:
            entry_index = int(parts[0])
            ani_type_count = int(parts[1])
            part_count = int(parts[2])
        except ValueError:
            continue

        raw_base_path = parts[3]
        base_normalized = posixpath.normpath(raw_base_path.replace("\\", "/").lstrip("/"))
        if base_normalized in ("", ".", ".."):
            continue

        base_stem = Path(base_normalized).name.lower()
        bon_candidate = f"{base_normalized}.bon"
        bon_resolved = CASE_INDEX.get(bon_candidate.lower())
        bon_path = bon_resolved.replace("/", "\\") if bon_resolved else bon_candidate.replace("/", "\\")

        row_values = valid_index_table[entry_index] if 0 <= entry_index < len(valid_index_table) else [0] * MAX_VALID_ANI_COLUMNS
        records: list[dict[str, Any]] = []
        existing_ani_count = 0

        for table_index in range(min(ani_type_count, len(row_values))):
            valid_value = row_values[table_index]
            if valid_value < 0:
                continue

            array_index = valid_value + 1
            if array_index <= 0:
                continue

            weapon_type = (array_index // 100) - 1
            motion_index = (array_index % 100) - 1
            ani_candidate = f"{base_normalized}{array_index:04d}.ani"
            ani_resolved = CASE_INDEX.get(ani_candidate.lower())
            ani_asset_path = ani_resolved if ani_resolved is not None else ani_candidate
            ani_path = ani_asset_path.replace("/", "\\")
            exists = ani_resolved is not None

            record: dict[str, Any] = {
                "tableIndex": table_index,
                "validIndexValue": valid_value,
                "arrayIndex": array_index,
                "weaponType": weapon_type,
                "motionIndex": motion_index,
                "aniPath": ani_path,
                "exists": exists,
            }

            if exists:
                existing_ani_count += 1
                header_info = ani_header_cache.get(ani_resolved)
                if header_info is None:
                    header_info = _read_ani_header(ani_resolved)
                    ani_header_cache[ani_resolved] = header_info
                if header_info:
                    record["tickCount"] = header_info["tickCount"]
                    record["bonesPerFrame"] = header_info["bonesPerFrame"]

            records.append(record)

        existing_records = [record for record in records if record["exists"]]
        available_weapon_types = sorted({record["weaponType"] for record in existing_records if record["weaponType"] >= 0})
        available_motion_count = len({record["motionIndex"] for record in existing_records if record["motionIndex"] >= 0})

        entry = {
            "index": entry_index,
            "aniTypeCount": ani_type_count,
            "parts": part_count,
            "basePath": base_normalized.replace("/", "\\"),
            "baseStem": base_stem,
            "bonPath": bon_path,
            "bonExists": bon_resolved is not None,
            "existingAniCount": existing_ani_count,
            "availableWeaponTypes": available_weapon_types,
            "availableMotionCount": available_motion_count,
            "records": records,
        }

        entries.append(entry)
        entries_by_index[entry_index] = entry
        entries_by_stem.setdefault(base_stem, []).append(entry)

    entries.sort(key=lambda item: item["index"])
    return {
        "loaded": True,
        "source": {
            "boneAni4Path": str(BONE_ANI4_PATH),
            "validIndexPath": str(VALID_INDEX_PATH),
        },
        "entries": entries,
        "entriesByIndex": entries_by_index,
        "entriesByStem": entries_by_stem,
    }


BONE_ANIMATION_CATALOG = _build_bone_animation_catalog()


def _find_bone_animation_entries_for_mesh(mesh_resolved_path: str) -> list[dict[str, Any]]:
    mesh_stem = Path(mesh_resolved_path).stem.lower()
    matches: list[dict[str, Any]] = []

    for entry in BONE_ANIMATION_CATALOG["entries"]:
        if mesh_stem.startswith(entry["baseStem"]):
            matches.append(entry)

    matches.sort(key=lambda item: (-len(item["baseStem"]), item["index"]))
    return matches


def _pick_recommended_animation_record(records: list[dict[str, Any]]) -> dict[str, Any] | None:
    if not records:
        return None

    preferred_pairs = [
        (0, 0),
        (0, 1),
        (0, 2),
        (0, 3),
        (0, 4),
        (0, 11),
        (0, 12),
        (0, 14),
    ]
    for weapon_type, motion_index in preferred_pairs:
        for record in records:
            if record["weaponType"] == weapon_type and record["motionIndex"] == motion_index:
                return record

    return sorted(
        records,
        key=lambda item: (
            0 if item["weaponType"] >= 0 else 1,
            item["weaponType"],
            item["motionIndex"],
            item["tableIndex"],
        ),
    )[0]


def _build_mesh_animation_profile(mesh_resolved_path: str) -> dict[str, Any] | None:
    matches = _find_bone_animation_entries_for_mesh(mesh_resolved_path)
    if not matches:
        return None

    entry = matches[0]
    existing_records = [record for record in entry["records"] if record["exists"]]
    candidate_records = existing_records if existing_records else entry["records"]
    recommended_record = _pick_recommended_animation_record(candidate_records)

    motions_by_weapon: dict[str, list[int]] = {}
    for record in candidate_records:
        weapon_key = str(record["weaponType"])
        weapon_bucket = motions_by_weapon.setdefault(weapon_key, [])
        motion_index = int(record["motionIndex"])
        if motion_index not in weapon_bucket:
            weapon_bucket.append(motion_index)

    for motion_list in motions_by_weapon.values():
        motion_list.sort()

    return {
        "meshPath": mesh_resolved_path.replace("/", "\\"),
        "entry": _summarize_bone_animation_entry(entry),
        "bonPath": entry["bonPath"],
        "records": [_compact_animation_record(record) for record in candidate_records],
        "matchCandidates": [
            {
                "index": match["index"],
                "basePath": match["basePath"],
                "baseStem": match["baseStem"],
                "aniTypeCount": match["aniTypeCount"],
                "existingAniCount": match["existingAniCount"],
            }
            for match in matches[:6]
        ],
        "availableWeaponTypes": sorted(
            {record["weaponType"] for record in candidate_records if record["weaponType"] >= 0}
        ),
        "motionsByWeapon": motions_by_weapon,
        "recommended": _compact_animation_record(recommended_record) if recommended_record else None,
    }


def _find_related_assets_for_mesh(mesh_resolved_path: str) -> dict[str, Any]:
    mesh_asset = mesh_resolved_path.replace("\\", "/")
    stem = Path(mesh_asset).stem
    stem_lower = stem.lower()
    prefix4 = stem_lower[:4]
    prefix6 = stem_lower[:6]

    texture_candidates: list[str] = []
    for ext in (".wys", ".wyt"):
        candidate = f"{Path(mesh_asset).with_suffix(ext).as_posix()}".lower()
        resolved = CASE_INDEX.get(candidate)
        if resolved is not None:
            texture_candidates.append(resolved.replace("/", "\\"))

    def build_candidate_list(kind: str) -> list[str]:
        results: list[str] = []
        seen: set[str] = set()

        if kind == "ani":
            by_stem = RELATED_INDEXES["ani_by_stem"]
            by_prefix6 = RELATED_INDEXES["ani_by_prefix6"]
            by_prefix4 = RELATED_INDEXES["ani_by_prefix4"]
        else:
            by_stem = RELATED_INDEXES["bon_by_stem"]
            by_prefix6 = RELATED_INDEXES["bon_by_prefix6"]
            by_prefix4 = RELATED_INDEXES["bon_by_prefix4"]

        def add_many(items: list[str]) -> None:
            for value in items:
                key = value.lower()
                if key in seen:
                    continue
                seen.add(key)
                results.append(value)

        add_many(by_stem.get(stem_lower, []))
        add_many(by_prefix6.get(prefix6, []))
        add_many(by_prefix4.get(prefix4, []))

        # Fallback: prefix "AA00" for stems like "AA000101"
        compact_prefix = re.match(r"^([a-z]{2}\d{2})", stem_lower)
        if compact_prefix:
            add_many(by_stem.get(compact_prefix.group(1), []))

        # Keep payload bounded for the web client.
        return results[:64]

    return {
        "meshPath": mesh_resolved_path.replace("/", "\\"),
        "textureCandidates": texture_candidates,
        "aniCandidates": build_candidate_list("ani"),
        "bonCandidates": build_candidate_list("bon"),
    }


def _is_scaled_object_type(object_type: int) -> bool:
    if 501 <= object_type <= 506:
        return True
    if 511 <= object_type < 519:
        return True
    if 520 <= object_type <= 530:
        return True
    if object_type in (531, 532):
        return True
    if 533 <= object_type < 600:
        return True
    return False


def _parse_trn_tile_map(absolute_path: Path) -> dict[str, Any]:
    raw = absolute_path.read_bytes()
    if len(raw) < 4:
        raise ValueError("TRN file too small")

    offset = 0
    name_len = int(raw[offset])
    offset += 1
    name_len = max(0, min(name_len, 127))
    if offset + name_len + 2 > len(raw):
        name_len = max(0, len(raw) - offset - 2)

    map_name = raw[offset : offset + name_len].decode("latin1", errors="ignore")
    offset += name_len
    if offset + 2 > len(raw):
        raise ValueError("Invalid TRN header")

    pos_x = int(raw[offset])
    pos_y = int(raw[offset + 1])
    offset += 2

    tile_record_size = 12
    available_tiles = max(0, (len(raw) - offset) // tile_record_size)
    tile_count = min(MAX_TILE_DATA_COUNT, available_tiles)
    if tile_count <= 0:
        raise ValueError("TRN tile payload missing")

    heights: list[int] = []
    tile_index_hist = Counter()
    back_tile_index_hist = Counter()

    for index in range(tile_count):
        base = offset + index * tile_record_size
        height = struct.unpack_from("<b", raw, base)[0]
        tile_index = int(raw[base + 1])
        back_tile_index = int(raw[base + 3])
        heights.append(int(height))
        tile_index_hist[tile_index] += 1
        back_tile_index_hist[back_tile_index] += 1

    height_min = min(heights)
    height_max = max(heights)
    height_avg = float(sum(heights)) / float(len(heights))

    height_grid = [[0 for _ in range(64)] for _ in range(64)]
    for index, height in enumerate(heights):
        y = index // 64
        x = index % 64
        if y < 64:
            height_grid[y][x] = int(height)

    tile_histogram_top = [
        {"index": int(tile_index), "count": int(count)}
        for tile_index, count in tile_index_hist.most_common(16)
    ]
    back_tile_histogram_top = [
        {"index": int(tile_index), "count": int(count)}
        for tile_index, count in back_tile_index_hist.most_common(16)
    ]

    return {
        "mapName": map_name,
        "offsetIndexX": pos_x,
        "offsetIndexY": pos_y,
        "tileCount": tile_count,
        "heightMin": int(height_min),
        "heightMax": int(height_max),
        "heightAvg": height_avg,
        "heightGrid": height_grid,
        "tileIndexHistogramTop": tile_histogram_top,
        "backTileIndexHistogramTop": back_tile_histogram_top,
    }


def _parse_dat_objects(absolute_path: Path) -> dict[str, Any]:
    raw = absolute_path.read_bytes()
    offset = 0
    items: list[dict[str, Any]] = []
    type_histogram = Counter()
    min_x: float | None = None
    min_y: float | None = None
    max_x: float | None = None
    max_y: float | None = None

    while offset + 28 <= len(raw) and len(items) < MAX_OBJECT_LIST:
        dw_obj_type, x, y, height, angle, texture_set_index, mask_index = struct.unpack_from("<Iffffii", raw, offset)
        offset += 28

        scale_h = 1.0
        scale_v = 1.0
        has_scale = False
        if _is_scaled_object_type(int(dw_obj_type)):
            if offset + 8 <= len(raw):
                scale_h, scale_v = struct.unpack_from("<ff", raw, offset)
                offset += 8
                has_scale = True
            else:
                break

        item = {
            "objType": int(dw_obj_type),
            "x": float(x),
            "y": float(y),
            "height": float(height),
            "angle": float(angle),
            "textureSetIndex": int(texture_set_index),
            "maskIndex": int(mask_index),
            "scaleH": float(scale_h),
            "scaleV": float(scale_v),
            "hasScale": has_scale,
        }
        items.append(item)
        type_histogram[int(dw_obj_type)] += 1

        min_x = item["x"] if min_x is None else min(min_x, item["x"])
        max_x = item["x"] if max_x is None else max(max_x, item["x"])
        min_y = item["y"] if min_y is None else min(min_y, item["y"])
        max_y = item["y"] if max_y is None else max(max_y, item["y"])

    histogram_top = [
        {"objType": int(obj_type), "count": int(count)}
        for obj_type, count in type_histogram.most_common(24)
    ]

    return {
        "count": len(items),
        "items": items,
        "typeHistogramTop": histogram_top,
        "bytesRead": offset,
        "trailingBytes": max(0, len(raw) - offset),
        "bounds": {
            "minX": min_x,
            "maxX": max_x,
            "minY": min_y,
            "maxY": max_y,
        },
    }


def _extract_map_tile_names(absolute_path: Path) -> list[str]:
    raw = absolute_path.read_bytes()
    names = {match.decode("ascii", errors="ignore") for match in re.findall(rb"Tile\d{5}", raw)}
    return sorted(name for name in names if name)


def _resolve_field_entry(field: str) -> dict[str, Any] | None:
    if not field:
        return None

    normalized = normalize_client_path(field)
    if normalized:
        stem = Path(normalized).stem.lower()
        if stem in FIELD_BY_NAME:
            return FIELD_BY_NAME[stem]

    fallback_key = Path(field.strip().replace("\\", "/")).stem.lower()
    return FIELD_BY_NAME.get(fallback_key)


def normalize_client_path(path: str) -> str:
    normalized = path.replace("\\", "/").strip()
    normalized = normalized.lstrip("/")
    normalized = posixpath.normpath(normalized)

    if normalized in ("", "."):
        return ""

    if normalized.startswith("../") or normalized == "..":
        return ""

    return normalized


def resolve_asset(requested_path: str) -> ResolvedAsset | None:
    normalized = normalize_client_path(requested_path)
    if not normalized:
        return None

    resolved = CASE_INDEX.get(normalized.lower())
    if resolved is None:
        return None

    absolute = (RELEASE_DIR / resolved).resolve()
    try:
        absolute.relative_to(RELEASE_DIR.resolve())
    except ValueError:
        return None

    if not absolute.exists() or not absolute.is_file():
        return None

    return ResolvedAsset(
        requested_path=requested_path,
        normalized_path=normalized,
        resolved_path=resolved,
        absolute_path=absolute,
    )


@app.route("/")
def index() -> Any:
    return send_from_directory(APP_DIR, "index.html")


@app.route("/api/manifest")
def get_manifest() -> Any:
    if request.args.get("full") == "1":
        return jsonify(ASSET_MANIFEST)

    summary = {
        "generatedAt": ASSET_MANIFEST.get("generatedAt"),
        "releaseRoot": ASSET_MANIFEST.get("releaseRoot"),
        "totals": ASSET_MANIFEST.get("totals"),
        "counts": ASSET_MANIFEST.get("counts"),
    }
    return jsonify(summary)


@app.route("/api/manifest/bootstrap")
def get_bootstrap_manifest() -> Any:
    return jsonify(BOOTSTRAP_MANIFEST)


@app.route("/api/assets/meshes")
def get_mesh_catalog() -> Any:
    default_mesh = "Mesh\\CP010102.msh"
    if not any(item["path"].lower() == default_mesh.lower() for item in MESH_CATALOG):
        default_mesh = MESH_CATALOG[0]["path"] if MESH_CATALOG else ""

    return jsonify(
        {
            "count": len(MESH_CATALOG),
            "defaultMesh": default_mesh,
            "items": MESH_CATALOG,
        }
    )


@app.route("/api/assets/fields")
def get_field_catalog() -> Any:
    default_field = "Field0815"
    if default_field.lower() not in FIELD_BY_NAME and FIELD_CATALOG:
        default_field = FIELD_CATALOG[0]["name"]

    return jsonify(
        {
            "ok": True,
            "count": len(FIELD_CATALOG),
            "defaultField": default_field,
            "items": FIELD_CATALOG,
        }
    )


@app.route("/api/assets/field-summary")
def get_field_summary() -> Any:
    requested_field = request.args.get("field", "")
    field_entry = _resolve_field_entry(requested_field)
    if field_entry is None:
        return (
            jsonify(
                {
                    "ok": False,
                    "requestedField": requested_field,
                    "error": "Field not found",
                }
            ),
            404,
        )

    object_limit_raw = request.args.get("objectLimit", "1024")
    try:
        object_limit = int(object_limit_raw)
    except ValueError:
        object_limit = 1024
    object_limit = max(64, min(MAX_OBJECT_LIST, object_limit))

    payload: dict[str, Any] = {
        "ok": True,
        "requestedField": requested_field,
        "field": field_entry,
    }

    warnings: list[str] = []
    tile_map: dict[str, Any] | None = None
    object_data: dict[str, Any] | None = None
    map_data: dict[str, Any] | None = None
    minimap_data: dict[str, Any] = _build_field_minimap_payload(field_entry["name"])

    if field_entry.get("trnPath"):
        trn_resolved = resolve_asset(field_entry["trnPath"])
        if trn_resolved is not None:
            try:
                tile_map = _parse_trn_tile_map(trn_resolved.absolute_path)
            except Exception as exc:  # pragma: no cover - defensive for malformed assets
                warnings.append(f"Failed to parse TRN: {exc}")
        else:
            warnings.append("TRN file not resolvable")

    if field_entry.get("datPath"):
        dat_resolved = resolve_asset(field_entry["datPath"])
        if dat_resolved is not None:
            try:
                object_data = _parse_dat_objects(dat_resolved.absolute_path)
                items = object_data.get("items", [])
                object_data["totalParsedCount"] = len(items)
                object_data["items"] = items[:object_limit]
                object_data["returnedCount"] = len(object_data["items"])
            except Exception as exc:  # pragma: no cover - defensive for malformed assets
                warnings.append(f"Failed to parse DAT: {exc}")
        else:
            warnings.append("DAT file not resolvable")

    if field_entry.get("mapPath"):
        map_resolved = resolve_asset(field_entry["mapPath"])
        if map_resolved is not None:
            try:
                tile_names = _extract_map_tile_names(map_resolved.absolute_path)
                map_data = {
                    "path": field_entry["mapPath"],
                    "size": int(map_resolved.absolute_path.stat().st_size),
                    "tileNameCount": len(tile_names),
                    "tileNames": tile_names[:256],
                }
            except Exception as exc:  # pragma: no cover - defensive for malformed assets
                warnings.append(f"Failed to parse MAP: {exc}")
        else:
            warnings.append("MAP file not resolvable")

    payload["tileMap"] = tile_map
    payload["objects"] = object_data
    payload["map"] = map_data
    payload["minimap"] = minimap_data
    payload["warnings"] = warnings

    return jsonify(payload)


@app.route("/api/assets/object-mesh-map")
def get_object_mesh_map() -> Any:
    raw_types = request.args.get("types", "")
    full = request.args.get("full") == "1"

    selected_types: list[int] = []
    if raw_types:
        for token in re.split(r"[,\s;]+", raw_types):
            token = token.strip()
            if not token:
                continue
            try:
                selected_types.append(int(token))
            except ValueError:
                continue

    selected_types = sorted(set(selected_types))
    if selected_types:
        items = [OBJECT_MESH_BY_TYPE[value] for value in selected_types if value in OBJECT_MESH_BY_TYPE]
    elif full:
        items = OBJECT_MESH_ENTRIES
    else:
        items = []

    return jsonify(
        {
            "ok": True,
            "count": len(items),
            "totalCatalogCount": len(OBJECT_MESH_ENTRIES),
            "requestedTypes": selected_types,
            "items": items,
        }
    )


@app.route("/api/assets/mesh-related")
def get_mesh_related() -> Any:
    mesh_path = request.args.get("mesh", "")
    resolved = resolve_asset(mesh_path)

    if resolved is None or not resolved.resolved_path.lower().endswith(".msh"):
        return (
            jsonify(
                {
                    "ok": False,
                    "requestedMesh": mesh_path,
                    "error": "Mesh not found",
                }
            ),
            404,
        )

    related = _find_related_assets_for_mesh(resolved.resolved_path)
    profile = _build_mesh_animation_profile(resolved.resolved_path)
    if profile is not None:
        related["animationProfileHint"] = {
            "entryIndex": profile["entry"]["index"],
            "basePath": profile["entry"]["basePath"],
            "recommendedAniPath": profile["recommended"]["aniPath"] if profile.get("recommended") else "",
            "bonPath": profile.get("bonPath", ""),
        }

    return jsonify(
        {
            "ok": True,
            "requestedMesh": mesh_path,
            "resolvedMesh": resolved.resolved_path,
            "related": related,
        }
    )


@app.route("/api/assets/bone-ani-catalog")
def get_bone_ani_catalog() -> Any:
    entry_index_raw = request.args.get("index")
    full = request.args.get("full") == "1"

    if entry_index_raw is not None:
        try:
            entry_index = int(entry_index_raw)
        except ValueError:
            return jsonify({"ok": False, "error": "Invalid index parameter"}), 400

        entry = BONE_ANIMATION_CATALOG["entriesByIndex"].get(entry_index)
        if entry is None:
            return jsonify({"ok": False, "error": "Entry not found", "index": entry_index}), 404

        payload = entry if full else _summarize_bone_animation_entry(entry)
        return jsonify(
            {
                "ok": True,
                "loaded": BONE_ANIMATION_CATALOG["loaded"],
                "entry": payload,
                "entryIndex": entry_index,
            }
        )

    entries = BONE_ANIMATION_CATALOG["entries"] if full else [
        _summarize_bone_animation_entry(entry) for entry in BONE_ANIMATION_CATALOG["entries"]
    ]

    return jsonify(
        {
            "ok": True,
            "loaded": BONE_ANIMATION_CATALOG["loaded"],
            "source": BONE_ANIMATION_CATALOG["source"],
            "count": len(entries),
            "entries": entries,
        }
    )


@app.route("/api/assets/mesh-animation-profile")
def get_mesh_animation_profile() -> Any:
    mesh_path = request.args.get("mesh", "")
    resolved = resolve_asset(mesh_path)

    if resolved is None or not resolved.resolved_path.lower().endswith(".msh"):
        return (
            jsonify(
                {
                    "ok": False,
                    "requestedMesh": mesh_path,
                    "error": "Mesh not found",
                }
            ),
            404,
        )

    profile = _build_mesh_animation_profile(resolved.resolved_path)
    return jsonify(
        {
            "ok": True,
            "requestedMesh": mesh_path,
            "resolvedMesh": resolved.resolved_path,
            "profile": profile,
        }
    )


@app.route("/api/resolve")
def get_resolve() -> Any:
    requested_path = request.args.get("path", "")
    resolved = resolve_asset(requested_path)

    if resolved is None:
        return (
            jsonify(
                {
                    "ok": False,
                    "requestedPath": requested_path,
                    "normalizedPath": normalize_client_path(requested_path),
                    "error": "Asset not found",
                }
            ),
            404,
        )

    return jsonify(
        {
            "ok": True,
            "requestedPath": resolved.requested_path,
            "normalizedPath": resolved.normalized_path,
            "resolvedPath": resolved.resolved_path,
            "size": resolved.absolute_path.stat().st_size,
            "assetUrl": f"/assets/{resolved.resolved_path}",
        }
    )


@app.route("/assets/<path:asset_path>")
def get_asset(asset_path: str) -> Any:
    resolved = resolve_asset(asset_path)
    if resolved is None:
        abort(404)

    mime_type, _ = mimetypes.guess_type(resolved.resolved_path)
    return send_file(resolved.absolute_path, mimetype=mime_type, conditional=True)


@app.route("/healthz")
def healthz() -> Any:
    return jsonify({"ok": True})


@app.route("/api/health")
def api_health() -> Any:
    return jsonify({"ok": True})


if __name__ == "__main__":
    host = os.environ.get("WYD_WEB_HOST", "0.0.0.0")
    port = int(os.environ.get("WYD_WEB_PORT", "8765"))
    app.run(host=host, port=port, debug=False)
