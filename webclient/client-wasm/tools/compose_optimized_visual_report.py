#!/usr/bin/env python3
"""Build pixel metrics, diffs and side-by-side evidence for Optimized."""

from __future__ import annotations

import html
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageStat


REPO_ROOT = Path(__file__).resolve().parents[3]
REPORT_DIR = REPO_ROOT / "webclient/client-wasm/build/reports/optimized-visual-compare"
RAW_REPORT = REPORT_DIR / "raw-report.json"


def _rms(image: Image.Image) -> float:
    histogram = image.convert("RGB").histogram()
    squares = sum((index % 256) ** 2 * count for index, count in enumerate(histogram))
    samples = image.width * image.height * 3
    return math.sqrt(squares / max(1, samples))


def _divergent_percent(image: Image.Image, threshold: int = 8) -> float:
    rgb = image.convert("RGB")
    divergent = 0
    for red, green, blue in rgb.getdata():
        if max(red, green, blue) > threshold:
            divergent += 1
    return divergent * 100.0 / max(1, rgb.width * rgb.height)


def _sharpness(image: Image.Image) -> float:
    edges = image.convert("L").filter(
        ImageFilter.Kernel(
            (3, 3),
            (-1, -1, -1, -1, 8, -1, -1, -1, -1),
            scale=1,
            offset=128,
        )
    )
    return float(ImageStat.Stat(edges).var[0])


def _run_by_label(payload: dict, label: str) -> dict:
    return next(run for run in payload["runs"] if run["label"] == label)


def _state_by_id(run: dict, state: int) -> dict:
    return next(entry for entry in run["states"] if entry["requestedState"] == state)


def _text_signature(entry: dict) -> list[tuple]:
    values = entry["snapshot"].get("visibleText", [])
    return sorted(
        (
            item.get("value", ""),
            round(float(item.get("width") or 0), 3),
            round(float(item.get("height") or 0), 3),
            int(item.get("type") or 0),
        )
        for item in values
    )


def _camera_delta(left: dict, right: dict) -> float:
    names = ("x", "y", "z", "horizon", "vertical", "sightLength", "wantLength")
    return max(
        abs(float(left.get(name) or 0) - float(right.get(name) or 0))
        for name in names
    )


def _make_montage(left: Image.Image, right: Image.Image, path: Path, labels: tuple[str, str]) -> None:
    height = max(left.height, right.height)
    output = Image.new("RGB", (left.width + right.width, height + 28), "#111111")
    output.paste(left.convert("RGB"), (0, 28))
    output.paste(right.convert("RGB"), (left.width, 28))
    draw = ImageDraw.Draw(output)
    draw.text((8, 7), labels[0], fill="white")
    draw.text((left.width + 8, 7), labels[1], fill="white")
    output.save(path, optimize=True)


def _make_text_crop(
    left: Image.Image,
    right: Image.Image,
    entry: dict,
    path: Path,
) -> bool:
    candidates = [
        item
        for item in entry["snapshot"].get("visibleText", [])
        if str(item.get("value") or "").strip()
    ]
    if not candidates:
        return False
    item = next(
        (
            candidate
            for preferred in ("Reino", "Normal", "Guilda", "Nv")
            for candidate in candidates
            if str(candidate.get("value") or "") == preferred
        ),
        candidates[0],
    )
    x = max(0, int(float(item.get("x") or 0)) - 12)
    y = max(0, int(float(item.get("y") or 0)) - 10)
    width = max(120, min(360, int(float(item.get("width") or 0)) + 40))
    height = max(42, min(100, int(float(item.get("height") or 0)) + 28))
    right_edge = min(left.width, x + width)
    bottom_edge = min(left.height, y + height)
    if right_edge <= x or bottom_edge <= y:
        return False
    box = (x, y, right_edge, bottom_edge)
    left_crop = left.crop(box).resize(
        ((right_edge - x) * 4, (bottom_edge - y) * 4),
        Image.Resampling.NEAREST,
    )
    right_crop = right.crop(box).resize(
        ((right_edge - x) * 4, (bottom_edge - y) * 4),
        Image.Resampling.NEAREST,
    )
    _make_montage(
        left_crop,
        right_crop,
        path,
        ("Fonte Legado 4x", "Fonte Otimizado A8 4x"),
    )
    return True


def main() -> int:
    payload = json.loads(RAW_REPORT.read_text(encoding="utf-8"))
    legacy = _run_by_label(payload, "legacy-800x600")
    optimized = _run_by_label(payload, "optimized-800x600")
    wide = _run_by_label(payload, "optimized-1920x1080")
    metrics = []

    for state in payload["states"]:
        legacy_entry = _state_by_id(legacy, state)
        optimized_entry = _state_by_id(optimized, state)
        wide_entry = _state_by_id(wide, state)
        legacy_image = Image.open(REPO_ROOT / legacy_entry["screenshot"]).convert("RGBA")
        optimized_image = Image.open(REPO_ROOT / optimized_entry["screenshot"]).convert("RGBA")
        wide_image = Image.open(REPO_ROOT / wide_entry["screenshot"]).convert("RGBA")
        difference = ImageChops.difference(legacy_image, optimized_image)
        enhanced = difference.convert("RGB").point(lambda value: min(255, value * 4))
        diff_path = REPORT_DIR / f"diff-state-{state}.png"
        enhanced.save(diff_path, optimize=True)
        side_path = REPORT_DIR / f"side-by-side-state-{state}.png"
        _make_montage(
            legacy_image,
            optimized_image,
            side_path,
            ("Legado 800x600", "Otimizado 800x600"),
        )
        wide_preview = wide_image.resize((960, 540), Image.Resampling.LANCZOS)
        wide_path = REPORT_DIR / f"legacy-vs-wide-state-{state}.png"
        _make_montage(
            legacy_image,
            wide_preview,
            wide_path,
            ("Legado 800x600", "Otimizado 1920x1080 (preview 960x540)"),
        )
        legacy_snapshot = legacy_entry["snapshot"]
        optimized_snapshot = optimized_entry["snapshot"]
        wide_snapshot = wide_entry["snapshot"]
        legacy_proj = legacy_snapshot["compare3d"]["projection"]
        optimized_proj = optimized_snapshot["compare3d"]["projection"]
        wide_proj = wide_snapshot["compare3d"]["projection"]
        legacy_pixel_scale = float(legacy_proj[5]) * 600.0
        wide_pixel_scale = float(wide_proj[5]) * 1080.0
        pixel_scale_ratio = wide_pixel_scale / max(0.0001, legacy_pixel_scale)
        screen_fraction_ratio = (
            (wide_pixel_scale / 1080.0) / max(0.0001, legacy_pixel_scale / 600.0)
        )
        text_crop_path = REPORT_DIR / f"text-crop-state-{state}.png"
        has_text_crop = _make_text_crop(
            legacy_image,
            optimized_image,
            legacy_entry,
            text_crop_path,
        )
        metrics.append(
            {
                "state": state,
                "rms": round(_rms(difference), 4),
                "divergentPixelsPercent": round(_divergent_percent(difference), 4),
                "legacySharpness": round(_sharpness(legacy_image), 3),
                "optimizedSharpness": round(_sharpness(optimized_image), 3),
                "textMetricsExactAt800": _text_signature(legacy_entry)
                == _text_signature(optimized_entry),
                "textMetricsExactAt1920": _text_signature(legacy_entry)
                == _text_signature(wide_entry),
                "cameraDeltaAt800": round(
                    _camera_delta(legacy_snapshot["camera"], optimized_snapshot["camera"]), 7
                ),
                "cameraDeltaAt1920": round(
                    _camera_delta(legacy_snapshot["camera"], wide_snapshot["camera"]), 7
                ),
                "verticalProjectionLegacy": legacy_proj[5],
                "verticalProjectionOptimized800": optimized_proj[5],
                "verticalProjectionOptimized1920": wide_proj[5],
                "verticalPixelScaleLegacy": round(legacy_pixel_scale, 7),
                "verticalPixelScaleOptimized1920": round(wide_pixel_scale, 7),
                "verticalPixelScaleRatioAt1920": round(pixel_scale_ratio, 7),
                "verticalScreenFractionRatioAt1920": round(screen_fraction_ratio, 7),
                "verticalPixelScaleDeltaAt1920": round(
                    abs(legacy_pixel_scale - wide_pixel_scale),
                    7,
                ),
                "legacyTextCount": len(legacy_snapshot.get("visibleText", [])),
                "optimizedTextCount": len(optimized_snapshot.get("visibleText", [])),
                "wideTextCount": len(wide_snapshot.get("visibleText", [])),
                "sideBySide": side_path.name,
                "wideEvidence": wide_path.name,
                "diff": diff_path.name,
                "textCrop": text_crop_path.name if has_text_crop else None,
            }
        )

    metric_path = REPORT_DIR / "metrics.json"
    metric_path.write_text(json.dumps(metrics, indent=2) + "\n", encoding="utf-8")
    rows = "\n".join(
        "<tr>"
        f"<td>{item['state']}</td>"
        f"<td>{item['rms']}</td>"
        f"<td>{item['divergentPixelsPercent']}%</td>"
        f"<td>{html.escape(str(item['textMetricsExactAt800']))}</td>"
        f"<td>{item['cameraDeltaAt1920']}</td>"
        f"<td>{item['verticalPixelScaleRatioAt1920']}</td>"
        f"<td>{item['verticalScreenFractionRatioAt1920']}</td>"
        f"<td><a href='{item['sideBySide']}'>800</a> · "
        f"<a href='{item['wideEvidence']}'>wide</a> · "
        f"<a href='{item['diff']}'>diff</a>"
        + (f" · <a href='{item['textCrop']}'>fonte 4×</a>" if item["textCrop"] else "")
        + "</td>"
        "</tr>"
        for item in metrics
    )
    (REPORT_DIR / "index.html").write_text(
        """<!doctype html><meta charset='utf-8'><title>OpenWyd Optimized evidence</title>
<style>body{font:14px system-ui;background:#151515;color:#eee;margin:24px}table{border-collapse:collapse}td,th{padding:7px 10px;border:1px solid #555}a{color:#8cf}</style>
<h1>OpenWyd Optimized — deterministic visual evidence</h1>
<p>Same seed, fake clock and 45 ticks per scene. The 800×600 pair isolates renderer changes; the 1920×1080 pair verifies wide framing without enlarging text metrics.</p>
<table><thead><tr><th>State</th><th>RMS</th><th>Pixels &gt; 8</th><th>Text metrics 800 exact</th><th>Camera delta wide</th><th>Physical world scale</th><th>Screen occupancy</th><th>Evidence</th></tr></thead><tbody>"""
        + rows
        + "</tbody></table>\n",
        encoding="utf-8",
    )
    passed = all(
        item["textMetricsExactAt800"]
        and item["cameraDeltaAt800"] <= 0.0001
        and 1.0 <= item["verticalPixelScaleRatioAt1920"] <= 1.25
        and 0.5 <= item["verticalScreenFractionRatioAt1920"] <= 0.75
        for item in metrics
    )
    print(
        json.dumps(
            {
                "ok": passed,
                "states": len(metrics),
                "metrics": metric_path.relative_to(REPO_ROOT).as_posix(),
                "html": (REPORT_DIR / "index.html").relative_to(REPO_ROOT).as_posix(),
            },
            indent=2,
        )
    )
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
