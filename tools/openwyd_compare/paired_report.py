"""Build a deterministic report from one paired native/WASM tick run."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import shutil
import sys
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Any

from PIL import UnidentifiedImageError

from . import frame_compare
from .frame_schema import FrameSchemaError, validate_frame_record


PAIRED_RUN_SCHEMA = "openwyd.paired-tick-run"
PAIRED_RUN_SCHEMA_VERSION = 1
REPORT_SCHEMA = "openwyd.paired-comparison-report"
REPORT_SCHEMA_VERSION = 1
MAX_FRAME_ID = (1 << 64) - 1
MAX_TIME_MS = (1 << 32) - 1
MAX_UINT32 = (1 << 32) - 1
RANDOM_UINT32_FIELDS = (
    "configured_seed",
    "state",
    "rand_calls",
    "srand_calls",
    "last_requested_seed",
)


class PairedReportError(ValueError):
    """Raised when paired-run inputs cannot form an honest comparison."""


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _write_json(path: Path, value: Mapping[str, Any]) -> None:
    encoded = json.dumps(
        value,
        ensure_ascii=False,
        indent=2,
        sort_keys=True,
        allow_nan=False,
    )
    path.write_text(encoded + "\n", encoding="utf-8", newline="\n")


def _load_json_object(path: Path, description: str) -> dict[str, Any]:
    def reject_nonstandard_constant(value: str) -> None:
        raise PairedReportError(
            f"{description} contains non-standard JSON number {value}: {path}"
        )

    try:
        value = json.loads(
            path.read_text(encoding="utf-8"),
            parse_constant=reject_nonstandard_constant,
        )
    except json.JSONDecodeError as error:
        raise PairedReportError(
            f"{description} is not valid JSON: {path}: {error}"
        ) from error
    if not isinstance(value, dict):
        raise PairedReportError(f"{description} must be a JSON object: {path}")
    return value


def _unsigned_frame_id(value: Any, description: str) -> int:
    if isinstance(value, bool):
        raise PairedReportError(f"{description} must be an unsigned integer")
    if isinstance(value, int):
        parsed = value
    elif isinstance(value, str) and re.fullmatch(r"(0|[1-9][0-9]*)", value):
        parsed = int(value, 10)
    else:
        raise PairedReportError(
            f"{description} must be an unsigned decimal integer or string"
        )
    if not 0 <= parsed <= MAX_FRAME_ID:
        raise PairedReportError(
            f"{description} must be in range 0..{MAX_FRAME_ID}"
        )
    return parsed


def _artifact_path(
    manifest_directory: Path,
    frame: Mapping[str, Any],
    field: str,
    frame_id: Any,
) -> Path:
    value = frame.get(field)
    if not isinstance(value, str) or not value:
        raise PairedReportError(
            f"frame {frame_id!r} field {field!r} must be a non-empty path"
        )
    path = Path(value)
    if not path.is_absolute():
        path = manifest_directory / path
    path = path.resolve()
    if not path.is_file():
        raise PairedReportError(
            f"frame {frame_id!r} artifact does not exist: {field}={path}"
        )
    return path


def _relative(path: Path, output_directory: Path) -> str:
    return path.relative_to(output_directory).as_posix()


def _prepare_output_directory(path: Path) -> None:
    if path.exists():
        if not path.is_dir():
            raise PairedReportError(f"report output is not a directory: {path}")
        if any(path.iterdir()):
            raise PairedReportError(
                f"report output must be new or empty to avoid stale artifacts: {path}"
            )
    else:
        path.mkdir(parents=True)


def _snapshot(
    source: Path,
    destination: Path,
    expected_frame_id: int,
    description: str,
) -> dict[str, Any]:
    record = _load_json_object(source, description)
    try:
        validate_frame_record(record)
    except FrameSchemaError as error:
        raise PairedReportError(f"{description} is invalid: {source}: {error}") from error
    actual_frame_id = _unsigned_frame_id(
        record["frame_id"],
        f"{description} frame_id",
    )
    if actual_frame_id != expected_frame_id:
        raise PairedReportError(
            f"{description} frame_id {record['frame_id']!r} does not match "
            f"paired frame {expected_frame_id}"
        )
    shutil.copyfile(source, destination)
    return record


def _snapshot_logical_position(
    record: Mapping[str, Any],
    description: str,
) -> tuple[int, int]:
    compare_frame_value = record["ticks"].get("compare_frame")
    try:
        compare_frame = _unsigned_frame_id(
            compare_frame_value,
            f"{description} ticks.compare_frame",
        )
    except PairedReportError as error:
        raise PairedReportError(
            f"{description} must contain a valid ticks.compare_frame: {error}"
        ) from error

    controlled_time_ms = record["clock"].get("controlled_time_ms")
    if (
        isinstance(controlled_time_ms, bool)
        or not isinstance(controlled_time_ms, int)
        or not 0 <= controlled_time_ms <= MAX_TIME_MS
    ):
        raise PairedReportError(
            f"{description} clock.controlled_time_ms must be uint32"
        )
    return compare_frame, controlled_time_ms


def _validate_logical_pair(
    native_record: Mapping[str, Any],
    wasm_record: Mapping[str, Any],
    expected_frame_id: int,
    expected_time_ms: int,
) -> None:
    native_position = _snapshot_logical_position(
        native_record,
        "DirectX snapshot",
    )
    wasm_position = _snapshot_logical_position(
        wasm_record,
        "WebGL snapshot",
    )
    if native_position != wasm_position:
        raise PairedReportError(
            "DirectX/WebGL snapshots do not identify the same logical frame: "
            f"DirectX compare_frame={native_position[0]}, "
            f"controlled_time_ms={native_position[1]}; "
            f"WebGL compare_frame={wasm_position[0]}, "
            f"controlled_time_ms={wasm_position[1]}"
        )

    for description, (compare_frame, controlled_time_ms) in (
        ("DirectX snapshot", native_position),
        ("WebGL snapshot", wasm_position),
    ):
        if compare_frame != expected_frame_id:
            raise PairedReportError(
                f"{description} ticks.compare_frame {compare_frame} does not "
                f"match paired frame {expected_frame_id}"
            )
        if controlled_time_ms != expected_time_ms:
            raise PairedReportError(
                f"{description} clock.controlled_time_ms {controlled_time_ms} "
                f"does not match paired frame time_ms {expected_time_ms}"
            )


def _runtime_random_snapshot(
    record: Mapping[str, Any],
    runtime: str,
    description: str,
    *,
    required: bool,
) -> dict[str, Any] | None:
    extensions = record.get("extensions")
    runtime_extension = (
        extensions.get(runtime) if isinstance(extensions, Mapping) else None
    )
    random = (
        runtime_extension.get("random")
        if isinstance(runtime_extension, Mapping)
        else None
    )
    if random is None:
        if required:
            raise PairedReportError(
                f"{description} must contain extensions.{runtime}.random "
                "for a seeded paired run"
            )
        return None
    if not isinstance(random, Mapping):
        raise PairedReportError(
            f"{description} extensions.{runtime}.random must be an object"
        )

    armed_value = random.get("armed")
    if isinstance(armed_value, bool):
        armed = armed_value
    elif (
        isinstance(armed_value, int)
        and not isinstance(armed_value, bool)
        and armed_value in (0, 1)
    ):
        armed = bool(armed_value)
    else:
        raise PairedReportError(
            f"{description} extensions.{runtime}.random.armed must be "
            "boolean or integer 0/1"
        )

    normalized: dict[str, Any] = {"armed": armed}
    for field in RANDOM_UINT32_FIELDS:
        value = random.get(field)
        if (
            isinstance(value, bool)
            or not isinstance(value, int)
            or not 0 <= value <= MAX_UINT32
        ):
            raise PairedReportError(
                f"{description} extensions.{runtime}.random.{field} "
                "must be uint32"
            )
        normalized[field] = value
    return normalized


def _frame_directory_name(frame_id: int) -> str:
    return f"frame-{frame_id:020d}"


def _parse_size(value: str) -> tuple[int, int]:
    match = re.fullmatch(r"\s*(\d+)[xX](\d+)\s*", value)
    if not match:
        raise argparse.ArgumentTypeError("size must use WIDTHxHEIGHT")
    width, height = (int(part) for part in match.groups())
    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("size dimensions must be positive")
    return width, height


def _summary(frames: list[dict[str, Any]]) -> dict[str, Any]:
    total_pixels = sum(frame["metrics"]["pixel_count"] for frame in frames)
    total_changed = sum(frame["metrics"]["changed_pixels"] for frame in frames)
    divergent = [
        frame for frame in frames if frame["metrics"]["changed_pixels"] > 0
    ]
    internal_divergent = [
        frame for frame in frames if not frame["snapshots"]["internal_equal"]
    ]
    ssim_values = [
        frame["metrics"]["ssim"]["rgb"]
        for frame in frames
        if frame["metrics"]["ssim"] is not None
    ]

    def weighted_rms(name: str) -> float:
        if total_pixels == 0:
            return 0.0
        mean_square = sum(
            (frame["metrics"][name] ** 2)
            * frame["metrics"]["pixel_count"]
            for frame in frames
        ) / total_pixels
        return round(math.sqrt(mean_square), 10)

    gl_error_values = [
        frame["wasm_gl_error_total"]
        for frame in frames
        if isinstance(frame["wasm_gl_error_total"], (int, float))
        and not isinstance(frame["wasm_gl_error_total"], bool)
    ]
    return {
        "all_frames_exact": not divergent,
        "divergent_frame_count": len(divergent),
        "exact_frame_count": len(frames) - len(divergent),
        "first_divergent_frame_id": (
            divergent[0]["frame_id"] if divergent else None
        ),
        "first_internal_mismatch_frame_id": (
            internal_divergent[0]["frame_id"]
            if internal_divergent
            else None
        ),
        "frame_count": len(frames),
        "internal_mismatch_frame_count": len(internal_divergent),
        "max_changed_pixel_percentage": max(
            frame["metrics"]["changed_pixel_percentage"] for frame in frames
        ),
        "max_rms_rgb": max(frame["metrics"]["rms_rgb"] for frame in frames),
        "mean_ssim_rgb": (
            round(sum(ssim_values) / len(ssim_values), 10)
            if ssim_values
            else None
        ),
        "min_ssim_rgb": min(ssim_values) if ssim_values else None,
        "pixel_weighted_rms_rgb": weighted_rms("rms_rgb"),
        "pixel_weighted_rms_rgba": weighted_rms("rms_rgba"),
        "total_changed_pixel_percentage": round(
            total_changed * 100.0 / total_pixels,
            10,
        ),
        "total_changed_pixels": total_changed,
        "total_pixels": total_pixels,
        "wasm_gl_error_total_max": (
            max(gl_error_values) if gl_error_values else None
        ),
    }


def report_paired_run(
    manifest_path: str | Path,
    output_directory: str | Path | None = None,
    *,
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
    """Validate and report every frame listed by ``paired-run.json``."""

    manifest_path = Path(manifest_path).resolve()
    manifest = _load_json_object(manifest_path, "paired-run manifest")
    if manifest.get("schema") != PAIRED_RUN_SCHEMA:
        raise PairedReportError(
            f"unsupported paired-run schema: {manifest.get('schema')!r}"
        )
    if manifest.get("schema_version") != PAIRED_RUN_SCHEMA_VERSION:
        raise PairedReportError(
            "unsupported paired-run schema version: "
            f"{manifest.get('schema_version')!r}"
        )
    random_seed = manifest.get("random_seed")
    if (
        random_seed is not None
        and (
            isinstance(random_seed, bool)
            or not isinstance(random_seed, int)
            or not 0 <= random_seed <= MAX_UINT32
        )
    ):
        raise PairedReportError("paired-run random_seed must be null or uint32")
    random_required = random_seed is not None

    width = manifest.get("width")
    height = manifest.get("height")
    if (
        isinstance(width, bool)
        or isinstance(height, bool)
        or not isinstance(width, int)
        or not isinstance(height, int)
        or width <= 0
        or height <= 0
    ):
        raise PairedReportError("paired-run width and height must be positive integers")
    source_frames = manifest.get("frames")
    if not isinstance(source_frames, list) or not source_frames:
        raise PairedReportError("paired-run frames must be a non-empty array")

    if output_directory is None:
        output_directory = manifest_path.parent / "comparison-report"
    output_directory = Path(output_directory).resolve()
    _prepare_output_directory(output_directory)

    frames: list[dict[str, Any]] = []
    previous_frame_id: int | None = None
    for index, source_frame in enumerate(source_frames):
        if not isinstance(source_frame, Mapping):
            raise PairedReportError(f"frames[{index}] must be a JSON object")
        source_frame_id = source_frame.get("frame_id")
        numeric_frame_id = _unsigned_frame_id(
            source_frame_id,
            f"frames[{index}].frame_id",
        )
        if previous_frame_id is not None and numeric_frame_id <= previous_frame_id:
            raise PairedReportError(
                "paired frame IDs must be strictly increasing; "
                f"received {numeric_frame_id} after {previous_frame_id}"
            )
        previous_frame_id = numeric_frame_id

        time_ms = source_frame.get("time_ms")
        if (
            isinstance(time_ms, bool)
            or not isinstance(time_ms, int)
            or not 0 <= time_ms <= MAX_TIME_MS
        ):
            raise PairedReportError(
                f"frame {source_frame_id!r} time_ms must be uint32"
            )

        native_png = _artifact_path(
            manifest_path.parent,
            source_frame,
            "native_png",
            source_frame_id,
        )
        wasm_png = _artifact_path(
            manifest_path.parent,
            source_frame,
            "wasm_png",
            source_frame_id,
        )
        native_snapshot = _artifact_path(
            manifest_path.parent,
            source_frame,
            "native_snapshot",
            source_frame_id,
        )
        wasm_snapshot = _artifact_path(
            manifest_path.parent,
            source_frame,
            "wasm_snapshot",
            source_frame_id,
        )

        frame_directory = (
            output_directory / "frames" / _frame_directory_name(numeric_frame_id)
        )
        frame_directory.mkdir(parents=True)
        copied_native_png = frame_directory / "directx.png"
        copied_wasm_png = frame_directory / "webgl.png"
        copied_native_snapshot = frame_directory / "directx.snapshot.json"
        copied_wasm_snapshot = frame_directory / "webgl.snapshot.json"
        shutil.copyfile(native_png, copied_native_png)
        shutil.copyfile(wasm_png, copied_wasm_png)
        native_record = _snapshot(
            native_snapshot,
            copied_native_snapshot,
            numeric_frame_id,
            "DirectX snapshot",
        )
        wasm_record = _snapshot(
            wasm_snapshot,
            copied_wasm_snapshot,
            numeric_frame_id,
            "WebGL snapshot",
        )
        _validate_logical_pair(
            native_record,
            wasm_record,
            numeric_frame_id,
            time_ms,
        )
        native_random = _runtime_random_snapshot(
            native_record,
            "native",
            "DirectX snapshot",
            required=random_required,
        )
        wasm_random = _runtime_random_snapshot(
            wasm_record,
            "wasm",
            "WebGL snapshot",
            required=random_required,
        )
        if random_required:
            for description, runtime_random in (
                ("DirectX snapshot", native_random),
                ("WebGL snapshot", wasm_random),
            ):
                if runtime_random is None:
                    raise AssertionError(
                        "seeded random telemetry was required but not loaded"
                    )
                if not runtime_random["armed"]:
                    raise PairedReportError(
                        f"{description} deterministic random generator is not "
                        "armed for this seeded paired run"
                    )
                if runtime_random["configured_seed"] != random_seed:
                    raise PairedReportError(
                        f"{description} deterministic random configured_seed "
                        f"{runtime_random['configured_seed']} does not match "
                        f"paired-run random_seed {random_seed}"
                    )
        states_equal = native_record["state"] == wasm_record["state"]
        random_equal = native_random == wasm_random
        internal_mismatches: list[str] = []
        if not states_equal:
            internal_mismatches.append("state")
        if not random_equal:
            internal_mismatches.append("random")

        comparison = frame_compare.compare_frame_pair(
            copied_native_png,
            copied_wasm_png,
            frame_directory,
            frame_id=source_frame_id,
            reference_orientation=reference_orientation,
            candidate_orientation=candidate_orientation,
            size_policy=size_policy,
            target_size=target_size,
            resize_filter=resize_filter,
            alpha_mode=alpha_mode,
            threshold=threshold,
            heatmap_gain=heatmap_gain,
            ssim_window=ssim_window,
        )
        for input_name in ("reference", "candidate"):
            original_size = comparison["inputs"][input_name]["original_size"]
            if (
                original_size["width"] != width
                or original_size["height"] != height
            ):
                raise PairedReportError(
                    f"frame {source_frame_id!r} {input_name} capture is "
                    f"{original_size['width']}x{original_size['height']}, "
                    f"but paired-run declares {width}x{height}"
                )
        normalized_size = comparison["normalization"]["normalized_size"]
        if target_size is None and (
            normalized_size["width"] != width
            or normalized_size["height"] != height
        ):
            raise PairedReportError(
                f"frame {source_frame_id!r} normalized to "
                f"{normalized_size['width']}x{normalized_size['height']}, "
                f"but paired-run declares {width}x{height}"
            )

        artifacts = {
            "absolute_diff": _relative(
                frame_directory / "diff.absolute.png",
                output_directory,
            ),
            "comparison_report": _relative(
                frame_directory / "report.json",
                output_directory,
            ),
            "directx_normalized": _relative(
                frame_directory / "reference.normalized.png",
                output_directory,
            ),
            "directx_png": _relative(copied_native_png, output_directory),
            "directx_snapshot": _relative(
                copied_native_snapshot,
                output_directory,
            ),
            "heatmap": _relative(
                frame_directory / "diff.heatmap.png",
                output_directory,
            ),
            "webgl_normalized": _relative(
                frame_directory / "candidate.normalized.png",
                output_directory,
            ),
            "webgl_png": _relative(copied_wasm_png, output_directory),
            "webgl_snapshot": _relative(
                copied_wasm_snapshot,
                output_directory,
            ),
        }
        frames.append(
            {
                "artifacts": artifacts,
                "frame_id": source_frame_id,
                "metrics": comparison["metrics"],
                "snapshots": {
                    "directx": {
                        "random": native_random,
                        "sha256": _sha256_file(copied_native_snapshot),
                        "state": native_record["state"],
                    },
                    "internal_equal": not internal_mismatches,
                    "internal_mismatches": internal_mismatches,
                    "random_equal": random_equal,
                    "states_equal": states_equal,
                    "webgl": {
                        "random": wasm_random,
                        "sha256": _sha256_file(copied_wasm_snapshot),
                        "state": wasm_record["state"],
                    },
                },
                "source_capture_sha256": {
                    "directx": _sha256_file(copied_native_png),
                    "webgl": _sha256_file(copied_wasm_png),
                },
                "time_ms": time_ms,
                "wasm_gl_error_total": wasm_record["render"].get(
                    "gl_error_total"
                ),
            }
        )

    report: dict[str, Any] = {
        "frames": frames,
        "normalization": {
            "alpha_mode": alpha_mode,
            "candidate_orientation": candidate_orientation,
            "heatmap_gain": heatmap_gain,
            "reference_orientation": reference_orientation,
            "resize_filter": resize_filter,
            "size_policy": size_policy,
            "ssim_window": ssim_window,
            "target_size": (
                {"width": target_size[0], "height": target_size[1]}
                if target_size is not None
                else None
            ),
            "threshold": threshold,
        },
        "schema": REPORT_SCHEMA,
        "schema_version": REPORT_SCHEMA_VERSION,
        "source": {
            "manifest_file": manifest_path.name,
            "manifest_sha256": _sha256_file(manifest_path),
            "paired_height": height,
            "paired_width": width,
        },
        "summary": _summary(frames),
    }
    _write_json(output_directory / "report.json", report)
    return report


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python -m tools.openwyd_compare report-paired",
        description=(
            "Turn paired-run.json into copied DirectX/WebGL captures, "
            "validated snapshots, diffs, heatmaps and aggregate metrics."
        )
    )
    parser.add_argument("paired_run_json", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument(
        "--reference-orientation",
        choices=frame_compare.ORIENTATIONS,
        default="identity",
    )
    parser.add_argument(
        "--candidate-orientation",
        choices=frame_compare.ORIENTATIONS,
        default="identity",
    )
    parser.add_argument(
        "--size-policy",
        choices=("strict", "reference", "candidate"),
        default="strict",
    )
    parser.add_argument("--target-size", type=_parse_size, metavar="WIDTHxHEIGHT")
    parser.add_argument(
        "--resize-filter",
        choices=frame_compare.RESIZE_FILTERS,
        default="nearest",
    )
    parser.add_argument(
        "--alpha-mode",
        choices=("compare", "opaque"),
        default="compare",
    )
    parser.add_argument("--threshold", type=int, default=0)
    parser.add_argument("--heatmap-gain", type=float, default=4.0)
    parser.add_argument("--ssim-window", type=int, default=8)
    parser.add_argument("--no-ssim", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)
    try:
        report = report_paired_run(
            args.paired_run_json,
            args.output_dir,
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
    except (
        FrameSchemaError,
        OSError,
        PairedReportError,
        UnidentifiedImageError,
        ValueError,
    ) as error:
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
