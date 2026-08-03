#!/usr/bin/env python3
"""Audit the visual resolution and UI geometry of the complete OpenWyd client.

This is deliberately separate from ``audit_client_assets.py``.  The existing
audit proves that a payload is structurally readable; this tool answers the
different questions needed by the optimized renderer:

* which textures are low resolution or block-compressed;
* whether every UI atlas crop stays inside its source texture;
* whether RC controls, parents, text boxes, edits and grids are geometrically
  coherent;
* whether mesh density can be changed safely (reported, never rewritten);
* which source atlases need human visual review before an HD derivative is
  accepted.

No original asset is modified.  Reports and contact sheets are written below
``build/reports`` by default and are ignored by Git.
"""

from __future__ import annotations

import argparse
import io
import json
import math
import re
import struct
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw, ImageFont


RC_CURRENT_SIZES = {1: 40, 2: 40, 3: 32, 6: 52, 10: 48, 12: 52, 13: 184, 15: 28, 16: 40}
RC_LEGACY_SIZES = {**RC_CURRENT_SIZES, 1: 36, 2: 164, 12: 176}
CONTROL_NAMES = {
    1: "panel",
    2: "button",
    3: "checkbox",
    6: "listbox",
    10: "progress",
    12: "text",
    13: "edit",
    15: "3dobj",
    16: "grid",
}


@dataclass
class TextureInfo:
    path: str
    family: str
    category: str
    width: int
    height: int
    encoding: str
    bits_per_pixel: int
    mip_count: int
    file_bytes: int


@dataclass
class UiCatalogEntry:
    index: int
    requested_path: str
    resolved_path: str | None
    alpha_mode: str
    width: int | None
    height: int | None
    encoding: str | None


@dataclass
class UiSetItem:
    set_name: str
    set_index: int
    item_index: int
    texture_index: int
    x: int
    y: int
    width: int
    height: int
    dest_x: int
    dest_y: int
    status: str
    detail: str


@dataclass
class RcControl:
    source: str
    layout: str
    kind: int
    kind_name: str
    control_id: int
    parent_id: int
    texture_set: int
    x: int
    y: int
    width: int
    height: int
    row_count: int | None = None
    column_count: int | None = None
    align: int | None = None


def normalize_path(value: str) -> str:
    value = value.strip().replace("\\", "/")
    value = re.sub(r"/+", "/", value)
    while value.startswith("./"):
        value = value[2:]
    return value.lstrip("/")


def classify_path(path: Path, release: Path) -> str:
    relative = normalize_path(str(path.relative_to(release))).lower()
    if relative.startswith("ui/"):
        return "ui"
    if relative.startswith("env/"):
        return "environment"
    if relative.startswith("effect/"):
        return "effect"
    if relative.startswith("mesh/"):
        return "model"
    return relative.split("/", 1)[0] if "/" in relative else "root"


def parse_texture(path: Path, release: Path) -> TextureInfo:
    data = path.read_bytes()
    if data[:4] == b"WT10":
        tga = data[4:]
        if len(tga) < 18:
            raise ValueError("truncated WT10/TGA header")
        width, height = struct.unpack_from("<HH", tga, 12)
        bpp = tga[16]
        encoding = f"TGA{bpp}"
        mips = 1
        family = "WYT"
    elif data[:4] == b"WS10":
        dds = bytearray(data[1:])
        if len(dds) < 128:
            raise ValueError("truncated WS10/DDS header")
        dds[:3] = b"DDS"
        width = struct.unpack_from("<I", dds, 16)[0]
        height = struct.unpack_from("<I", dds, 12)[0]
        mips = max(1, struct.unpack_from("<I", dds, 28)[0])
        encoding = "DXT1" if dds[84] == ord("2") else "DXT3"
        bpp = 4 if encoding == "DXT1" else 8
        family = "WYS"
    else:
        raise ValueError("not a wrapped OpenWyd texture")
    if width <= 0 or height <= 0:
        raise ValueError(f"invalid dimensions {width}x{height}")
    return TextureInfo(
        path=normalize_path(str(path.relative_to(release))),
        family=family,
        category=classify_path(path, release),
        width=width,
        height=height,
        encoding=encoding,
        bits_per_pixel=bpp,
        mip_count=mips,
        file_bytes=len(data),
    )


def decode_texture(path: Path) -> Image.Image:
    data = path.read_bytes()
    if data[:4] == b"WT10":
        payload = data[4:]
    elif data[:4] == b"WS10":
        dds = bytearray(data[1:])
        dds[:3] = b"DDS"
        dds[84:88] = b"DXT1" if dds[84] == ord("2") else b"DXT3"
        payload = bytes(dds)
    else:
        raise ValueError(f"unsupported texture wrapper: {path}")
    image = Image.open(io.BytesIO(payload))
    image.load()
    return image.convert("RGBA")


def file_index(release: Path) -> dict[str, Path]:
    return {
        normalize_path(str(path.relative_to(release))).lower(): path
        for path in release.rglob("*")
        if path.is_file()
    }


def parse_ui_catalog(release: Path, textures: dict[str, TextureInfo]) -> list[UiCatalogEntry]:
    data = (release / "UI/UITextureListN.bin").read_bytes()
    if len(data) != 512 * 528:
        raise ValueError(f"UITextureListN.bin size {len(data)} != {512 * 528}")
    paths = file_index(release)
    result: list[UiCatalogEntry] = []
    for index in range(512):
        record = data[index * 528 : (index + 1) * 528]
        requested = record[:255].split(b"\0", 1)[0].decode("cp1252", errors="replace")
        fallback = record[255:510].split(b"\0", 1)[0].decode("cp1252", errors="replace")
        alpha_mode = chr(record[510]) if record[510] else ""
        if not requested:
            continue
        candidates = [normalize_path(requested)]
        if fallback:
            candidates.append(normalize_path(fallback))
        resolved = next((paths.get(candidate.lower()) for candidate in candidates if paths.get(candidate.lower())), None)
        info = None
        relative = None
        if resolved:
            relative = normalize_path(str(resolved.relative_to(release)))
            info = textures.get(relative.lower())
        result.append(UiCatalogEntry(
            index=index,
            requested_path=normalize_path(requested),
            resolved_path=relative,
            alpha_mode=alpha_mode,
            width=info.width if info else None,
            height=info.height if info else None,
            encoding=info.encoding if info else None,
        ))
    return result


def parse_ui_sets(release: Path, catalog: dict[int, UiCatalogEntry]) -> tuple[list[UiSetItem], list[str]]:
    path = release / "UI/UITextureSetList.txt"
    lines = path.read_text(encoding="cp1252").splitlines()
    result: list[UiSetItem] = []
    errors: list[str] = []
    index = 0
    while index < len(lines):
        line = lines[index].strip()
        index += 1
        if not line:
            continue
        if not (line.startswith("[") and line.endswith("]")):
            errors.append(f"line {index}: expected [section], got {line!r}")
            continue
        name = line[1:-1]
        if index + 1 >= len(lines):
            errors.append(f"section {name}: truncated header")
            break
        set_match = re.fullmatch(r"SetIndex:\s*(-?\d+)", lines[index].strip())
        count_match = re.fullmatch(r"ItemCount:\s*(\d+)", lines[index + 1].strip())
        index += 2
        if not set_match or not count_match:
            errors.append(f"section {name}: malformed SetIndex/ItemCount")
            continue
        set_index = int(set_match.group(1))
        item_count = int(count_match.group(1))
        for item_index in range(item_count):
            if index >= len(lines):
                errors.append(f"section {name}: expected {item_count} items")
                break
            fields = [part.strip() for part in lines[index].split(",")]
            index += 1
            if len(fields) != 7:
                errors.append(f"section {name} item {item_index}: expected 7 fields")
                continue
            try:
                texture_index, x, y, width, height, dest_x, dest_y = map(int, fields)
            except ValueError:
                errors.append(f"section {name} item {item_index}: non-integer field")
                continue
            entry = catalog.get(texture_index)
            status = "OK"
            detail = "crop is inside source texture"
            if not entry or entry.width is None or entry.height is None:
                status = "MISSING_TEXTURE"
                detail = f"catalog texture {texture_index} is unresolved"
            elif width <= 0 or height <= 0:
                status = "INVALID_SIZE"
                detail = f"crop has non-positive size {width}x{height}"
            elif x < 0 or y < 0 or x + width > entry.width or y + height > entry.height:
                status = "OUT_OF_BOUNDS"
                detail = (
                    f"crop {x},{y} {width}x{height} exceeds "
                    f"{entry.width}x{entry.height}"
                )
            result.append(UiSetItem(
                set_name=name,
                set_index=set_index,
                item_index=item_index,
                texture_index=texture_index,
                x=x,
                y=y,
                width=width,
                height=height,
                dest_x=dest_x,
                dest_y=dest_y,
                status=status,
                detail=detail,
            ))
    return result, errors


def walk_rc(data: bytes, sizes: dict[int, int]) -> list[tuple[int, bytes]] | None:
    offset = 0
    records: list[tuple[int, bytes]] = []
    while offset < len(data):
        if offset + 4 > len(data):
            return None
        kind = struct.unpack_from("<i", data, offset)[0]
        offset += 4
        size = sizes.get(kind)
        if size is None or offset + size > len(data):
            return None
        records.append((kind, data[offset : offset + size]))
        offset += size
    return records


def parse_rc_control(source: str, layout: str, kind: int, raw: bytes) -> RcControl:
    values = struct.unpack_from(f"<{len(raw) // 4}i", raw)
    control_id, parent_id = values[0], values[1]
    texture_set = values[2] if kind != 15 else -1
    # List boxes and progress bars insert current/max values before geometry.
    # Every other persisted RC record stores x/y/width/height immediately
    # after the texture-set field (or the 3D object placeholder).
    geometry_offset = 5 if kind in (6, 10) else 3
    x, y, width, height = values[geometry_offset : geometry_offset + 4]
    row_count = column_count = align = None
    if kind == 16:
        row_count, column_count = values[7], values[8]
    elif kind == 12 and layout == "current":
        align = values[11]
    elif kind == 12 and layout == "legacy":
        align = values[11]
    elif kind == 13:
        align = values[11]
    return RcControl(
        source=source,
        layout=layout,
        kind=kind,
        kind_name=CONTROL_NAMES[kind],
        control_id=control_id,
        parent_id=parent_id,
        texture_set=texture_set,
        x=x,
        y=y,
        width=width,
        height=height,
        row_count=row_count,
        column_count=column_count,
        align=align,
    )


def parse_all_rc(release: Path) -> tuple[list[RcControl], list[dict[str, str]]]:
    controls: list[RcControl] = []
    skipped: list[dict[str, str]] = []
    for path in sorted((release / "UI").glob("*.bin")):
        if "scene" not in path.name.lower() or not path.stat().st_size:
            continue
        data = path.read_bytes()
        current = walk_rc(data, RC_CURRENT_SIZES)
        legacy = walk_rc(data, RC_LEGACY_SIZES)
        layout = "current" if current is not None else "legacy" if legacy is not None else ""
        records = current if current is not None else legacy
        if records is None:
            skipped.append({"source": path.name, "reason": "mixed/unsupported recovered RC layout"})
            continue
        for kind, raw in records:
            controls.append(parse_rc_control(path.name, layout, kind, raw))
    return controls, skipped


def audit_rc_geometry(controls: list[RcControl]) -> list[dict[str, object]]:
    findings: list[dict[str, object]] = []
    by_source: dict[str, list[RcControl]] = {}
    for control in controls:
        by_source.setdefault(control.source, []).append(control)
    for source, scene_controls in by_source.items():
        by_id = {control.control_id: control for control in scene_controls}
        for control in scene_controls:
            context = {"source": source, "id": control.control_id, "type": control.kind_name}
            if control.width <= 0 or control.height <= 0:
                findings.append({**context, "severity": "ERROR", "issue": "non_positive_size", "detail": f"{control.width}x{control.height}"})
            if control.parent_id and control.parent_id not in by_id:
                findings.append({**context, "severity": "ERROR", "issue": "missing_parent", "detail": str(control.parent_id)})
            parent = by_id.get(control.parent_id)
            if parent and (control.x < 0 or control.y < 0 or control.x + control.width > parent.width or control.y + control.height > parent.height):
                findings.append({
                    **context,
                    "severity": "REVIEW",
                    "issue": "extends_parent",
                    "detail": f"child {control.x},{control.y} {control.width}x{control.height}; parent {parent.width}x{parent.height}",
                })
            if control.kind in (12, 13) and control.height < 12:
                findings.append({**context, "severity": "REVIEW", "issue": "text_box_shorter_than_font", "detail": f"height={control.height}, font=12"})
            if control.kind == 16:
                rows = control.row_count or 0
                columns = control.column_count or 0
                if rows <= 0 or columns <= 0:
                    findings.append({**context, "severity": "ERROR", "issue": "invalid_grid_shape", "detail": f"{columns}x{rows}"})
                else:
                    cell_width = control.width / columns
                    cell_height = control.height / rows
                    if not math.isclose(cell_width, round(cell_width), abs_tol=1e-6) or not math.isclose(cell_height, round(cell_height), abs_tol=1e-6):
                        findings.append({
                            **context,
                            "severity": "REVIEW",
                            "issue": "fractional_grid_cell",
                            "detail": f"cell={cell_width:.3f}x{cell_height:.3f} from {control.width}x{control.height}/{columns}x{rows}",
                        })
    return findings


def mesh_statistics(release: Path) -> dict[str, object]:
    msh_vertices: list[int] = []
    msh_triangles: list[int] = []
    msa_vertices: list[int] = []
    msa_triangles: list[int] = []
    for path in release.rglob("*.msh"):
        data = path.read_bytes()
        if len(data) < 32:
            continue
        _, _, _, stride, _, _, vertices, indices = struct.unpack_from("<8I", data)
        if stride and 32 + vertices * stride <= len(data):
            msh_vertices.append(vertices)
            msh_triangles.append(indices // 3)
    for path in release.rglob("*.msa"):
        data = path.read_bytes()
        if len(data) < 12:
            continue
        _, stride, attributes = struct.unpack_from("<3I", data)
        offset = 12 + attributes * 20 + attributes * 11
        if not stride or offset + 8 > len(data):
            continue
        index_bytes = struct.unpack_from("<I", data, offset)[0]
        offset += 4 + index_bytes
        if offset + 4 > len(data):
            continue
        vertex_bytes = struct.unpack_from("<I", data, offset)[0]
        msa_vertices.append(vertex_bytes // stride)
        msa_triangles.append((index_bytes // 2) // 3)

    def summarize(values: list[int]) -> dict[str, float | int]:
        if not values:
            return {"count": 0, "min": 0, "median": 0, "p95": 0, "max": 0, "sum": 0}
        ordered = sorted(values)
        return {
            "count": len(values),
            "min": ordered[0],
            "median": ordered[len(ordered) // 2],
            "p95": ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))],
            "max": ordered[-1],
            "sum": sum(ordered),
        }

    return {
        "msh_vertices": summarize(msh_vertices),
        "msh_triangles": summarize(msh_triangles),
        "msa_vertices": summarize(msa_vertices),
        "msa_triangles": summarize(msa_triangles),
        "safe_reprocess_policy": (
            "Do not subdivide original meshes globally: it changes silhouettes, bone weights, "
            "attachment seams and animation deformation. Improve texture sampling and the render "
            "path first; any mesh derivative requires an isolated visual/animation approval."
        ),
    }


def make_contact_sheets(
    release: Path,
    catalog: Iterable[UiCatalogEntry],
    output_dir: Path,
) -> list[str]:
    output_dir.mkdir(parents=True, exist_ok=True)
    paths = file_index(release)
    entries = [entry for entry in catalog if entry.resolved_path]
    columns, rows = 5, 4
    cell_w, cell_h = 280, 220
    page_size = columns * rows
    generated: list[str] = []
    font = ImageFont.load_default()
    for page_index in range((len(entries) + page_size - 1) // page_size):
        page_entries = entries[page_index * page_size : (page_index + 1) * page_size]
        sheet = Image.new("RGB", (columns * cell_w, rows * cell_h), (18, 18, 20))
        draw = ImageDraw.Draw(sheet)
        for local_index, entry in enumerate(page_entries):
            column = local_index % columns
            row = local_index // columns
            left, top = column * cell_w, row * cell_h
            path = paths[entry.resolved_path.lower()]
            try:
                image = decode_texture(path)
                # Composite alpha over a neutral checker to expose halos and
                # one-bit/block-compressed edges during human review.
                checker = Image.new("RGBA", image.size, (54, 54, 58, 255))
                checker_draw = ImageDraw.Draw(checker)
                tile = 8
                for y in range(0, image.height, tile):
                    for x in range(0, image.width, tile):
                        if ((x // tile) + (y // tile)) & 1:
                            checker_draw.rectangle((x, y, x + tile - 1, y + tile - 1), fill=(78, 78, 82, 255))
                checker.alpha_composite(image)
                preview = checker.convert("RGB")
                preview.thumbnail((cell_w - 16, cell_h - 44), Image.Resampling.LANCZOS)
                px = left + (cell_w - preview.width) // 2
                py = top + 28 + (cell_h - 34 - preview.height) // 2
                sheet.paste(preview, (px, py))
            except Exception as error:  # keep the page complete and report it visually
                draw.text((left + 8, top + 50), f"decode error: {error}", fill=(255, 96, 96), font=font)
            label = f"#{entry.index} {entry.resolved_path} {entry.width}x{entry.height} {entry.encoding}"
            draw.text((left + 6, top + 7), label[:46], fill=(240, 240, 240), font=font)
            draw.rectangle((left, top, left + cell_w - 1, top + cell_h - 1), outline=(70, 70, 74))
        destination = output_dir / f"ui-atlases-{page_index + 1:02d}.png"
        sheet.save(destination, optimize=True)
        generated.append(str(destination))
    return generated


def markdown_report(report: dict[str, object]) -> str:
    summary = report["summary"]
    findings = report["rc_geometry_findings"]
    high_priority = [item for item in findings if item["severity"] == "ERROR"]
    review = [item for item in findings if item["severity"] == "REVIEW"]
    lines = [
        "# OpenWyd visual quality audit",
        "",
        "This report measures every original texture/mesh payload and every parseable RC record. "
        "It does not claim that an AI-generated derivative is faithful; derivatives remain opt-in "
        "until a deterministic before/after comparison passes.",
        "",
        "## Summary",
        "",
        f"- Textures inspected: **{summary['texture_count']}**",
        f"- UI catalog entries: **{summary['ui_catalog_entries']}**",
        f"- UI atlas crops: **{summary['ui_set_items']}**",
        f"- RC controls: **{summary['rc_controls']}**",
        f"- RC geometry errors: **{len(high_priority)}**",
        f"- RC geometry review items: **{len(review)}**",
        f"- Contact-sheet pages: **{len(report['contact_sheets'])}**",
        "",
        "## Fidelity policy",
        "",
        "- Original files are immutable.",
        "- Optimized derivatives must be content-addressed and runtime-selectable.",
        "- UI crops, alpha, palette/hue and logical dimensions must remain unchanged.",
        "- Mesh subdivision is rejected globally because it changes silhouette, skinning and seams.",
        "- Generative/AI output is never accepted from appearance alone; it needs round-trip metrics, "
        "  alpha-edge checks and representative in-game captures.",
        "",
        "## Findings requiring code or source review",
        "",
    ]
    for item in (high_priority + review)[:500]:
        lines.append(
            f"- [{item['severity']}] `{item['source']}` control `{item['id']}` "
            f"({item['type']}): {item['issue']} — {item['detail']}"
        )
    if len(high_priority) + len(review) > 500:
        lines.append(f"- … remaining findings are available in JSON ({len(high_priority) + len(review) - 500} omitted here).")
    lines.extend(["", "## Contact sheets", ""])
    for sheet in report["contact_sheets"]:
        lines.append(f"- `{sheet}`")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--json", type=Path, default=Path("webclient/client-wasm/build/reports/visual-quality-audit.json"))
    parser.add_argument("--markdown", type=Path, default=Path("webclient/client-wasm/build/reports/visual-quality-audit.md"))
    parser.add_argument("--contact-sheet-dir", type=Path, default=Path("webclient/client-wasm/build/reports/visual-quality-ui-atlases"))
    args = parser.parse_args()

    repo = args.repo.resolve()
    release = repo / "v769ClientRelease"
    json_path = (repo / args.json).resolve() if not args.json.is_absolute() else args.json
    markdown_path = (repo / args.markdown).resolve() if not args.markdown.is_absolute() else args.markdown
    sheet_dir = (repo / args.contact_sheet_dir).resolve() if not args.contact_sheet_dir.is_absolute() else args.contact_sheet_dir

    texture_items = [parse_texture(path, release) for path in sorted(release.rglob("*")) if path.suffix.lower() in (".wyt", ".wys")]
    textures = {item.path.lower(): item for item in texture_items}
    catalog_items = parse_ui_catalog(release, textures)
    catalog = {item.index: item for item in catalog_items}
    ui_set_items, ui_set_errors = parse_ui_sets(release, catalog)
    rc_controls, rc_skipped = parse_all_rc(release)
    rc_findings = audit_rc_geometry(rc_controls)
    contact_sheets = make_contact_sheets(release, catalog_items, sheet_dir)

    texture_by_encoding = Counter(item.encoding for item in texture_items)
    texture_by_category = Counter(item.category for item in texture_items)
    texture_dimension_buckets = Counter()
    for item in texture_items:
        maximum = max(item.width, item.height)
        if maximum <= 64:
            texture_dimension_buckets["<=64"] += 1
        elif maximum <= 128:
            texture_dimension_buckets["65-128"] += 1
        elif maximum <= 256:
            texture_dimension_buckets["129-256"] += 1
        elif maximum <= 512:
            texture_dimension_buckets["257-512"] += 1
        elif maximum <= 1024:
            texture_dimension_buckets["513-1024"] += 1
        else:
            texture_dimension_buckets[">1024"] += 1

    report: dict[str, object] = {
        "schema": 1,
        "repo": str(repo),
        "summary": {
            "texture_count": len(texture_items),
            "texture_pixels": sum(item.width * item.height for item in texture_items),
            "ui_catalog_entries": len(catalog_items),
            "ui_catalog_resolved": sum(item.resolved_path is not None for item in catalog_items),
            "ui_set_items": len(ui_set_items),
            "ui_set_failures": sum(item.status != "OK" for item in ui_set_items),
            "rc_controls": len(rc_controls),
            "rc_sources": len({item.source for item in rc_controls}),
        },
        "texture_by_encoding": dict(sorted(texture_by_encoding.items())),
        "texture_by_category": dict(sorted(texture_by_category.items())),
        "texture_dimension_buckets": dict(texture_dimension_buckets),
        "textures": [asdict(item) for item in texture_items],
        "ui_catalog": [asdict(item) for item in catalog_items],
        "ui_sets": [asdict(item) for item in ui_set_items],
        "ui_set_parse_errors": ui_set_errors,
        "rc_controls": [asdict(item) for item in rc_controls],
        "rc_skipped": rc_skipped,
        "rc_geometry_findings": rc_findings,
        "mesh_statistics": mesh_statistics(release),
        "contact_sheets": [str(Path(path).relative_to(repo)).replace("\\", "/") for path in contact_sheets],
    }

    json_path.parent.mkdir(parents=True, exist_ok=True)
    markdown_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    markdown_path.write_text(markdown_report(report), encoding="utf-8")
    print(json.dumps({
        "summary": report["summary"],
        "encodings": report["texture_by_encoding"],
        "dimension_buckets": report["texture_dimension_buckets"],
        "rc_findings": Counter(item["severity"] for item in rc_findings),
        "json": str(json_path),
        "markdown": str(markdown_path),
        "contact_sheets": len(contact_sheets),
    }, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
