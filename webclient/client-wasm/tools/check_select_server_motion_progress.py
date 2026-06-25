#!/usr/bin/env python3
"""Check select-server human telemetry for stalled demo route progress.

The WASM select-server smoke harness can export per-human telemetry snapshots.
This checker verifies that humans reported as moving also show either position
delta or route-index progress across the captured sample set.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any


POSITION_EPSILON = 0.0001


@dataclass
class HumanSample:
    source: str
    index: int
    moving: bool
    pos_x: float | None
    pos_y: float | None
    delta_x: float
    delta_y: float
    last_route_index: int | None
    max_route_index: int | None
    motion: int | None
    progress_rate: float | None


def coerce_float(value: Any) -> float | None:
    if value is None:
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    if math.isnan(number) or math.isinf(number):
        return None
    return number


def coerce_int(value: Any) -> int | None:
    if value is None:
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def coerce_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.strip().lower() in {"1", "true", "yes", "moving"}
    return bool(value)


def pick(mapping: dict[str, Any], *keys: str) -> Any:
    for key in keys:
        if key in mapping:
            return mapping[key]
    return None


def sample_from_mapping(source: str, index: int, raw: dict[str, Any]) -> HumanSample:
    pos_x = coerce_float(pick(raw, "posX", "pos_x", "x"))
    pos_y = coerce_float(pick(raw, "posY", "pos_y", "y"))
    delta_x = coerce_float(pick(raw, "deltaX", "delta_x", "dx")) or 0.0
    delta_y = coerce_float(pick(raw, "deltaY", "delta_y", "dy")) or 0.0
    return HumanSample(
        source=source,
        index=coerce_int(pick(raw, "index", "humanIndex", "human_index")) if pick(raw, "index", "humanIndex", "human_index") is not None else index,
        moving=coerce_bool(pick(raw, "moving", "isMoving", "moveing", "m_bMoveing")),
        pos_x=pos_x,
        pos_y=pos_y,
        delta_x=delta_x,
        delta_y=delta_y,
        last_route_index=coerce_int(pick(raw, "lastRouteIndex", "last_route_index", "routeIndex")),
        max_route_index=coerce_int(pick(raw, "maxRouteIndex", "max_route_index", "routeCount")),
        motion=coerce_int(pick(raw, "motion", "eMotion")),
        progress_rate=coerce_float(pick(raw, "progressRate", "progress_rate")),
    )


def human_rows(payload: Any) -> list[dict[str, Any]]:
    if isinstance(payload, list):
        return [row for row in payload if isinstance(row, dict)]
    if not isinstance(payload, dict):
        return []
    for key in ("humans", "humanTelemetry", "selectServerHumans", "samples"):
        rows = payload.get(key)
        if isinstance(rows, list):
            return [row for row in rows if isinstance(row, dict)]
    return []


def load_samples(path: Path) -> list[HumanSample]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    return [sample_from_mapping(path.as_posix(), index, row) for index, row in enumerate(human_rows(payload))]


def distance_from_previous(previous: HumanSample | None, current: HumanSample) -> float:
    if previous is None or previous.pos_x is None or previous.pos_y is None:
        return 0.0
    if current.pos_x is None or current.pos_y is None:
        return 0.0
    return math.hypot(current.pos_x - previous.pos_x, current.pos_y - previous.pos_y)


def analyze(samples: list[HumanSample]) -> dict[str, Any]:
    previous_by_index: dict[int, HumanSample] = {}
    moving_total = 0
    moved_by_delta = 0
    moved_by_position = 0
    advanced_route = 0
    stalled: list[dict[str, Any]] = []

    for sample in samples:
        previous = previous_by_index.get(sample.index)
        previous_by_index[sample.index] = sample
        if not sample.moving:
            continue

        moving_total += 1
        delta_distance = math.hypot(sample.delta_x, sample.delta_y)
        position_distance = distance_from_previous(previous, sample)
        route_delta = 0
        if previous and sample.last_route_index is not None and previous.last_route_index is not None:
            route_delta = sample.last_route_index - previous.last_route_index

        has_delta = delta_distance > POSITION_EPSILON
        has_position_step = position_distance > POSITION_EPSILON
        has_route_step = route_delta > 0
        moved_by_delta += int(has_delta)
        moved_by_position += int(has_position_step)
        advanced_route += int(has_route_step)

        if not has_delta and not has_position_step and not has_route_step:
            stalled.append(
                {
                    "source": sample.source,
                    "index": sample.index,
                    "motion": sample.motion,
                    "lastRouteIndex": sample.last_route_index,
                    "maxRouteIndex": sample.max_route_index,
                    "progressRate": sample.progress_rate,
                    "deltaDistance": delta_distance,
                    "positionDistance": position_distance,
                }
            )

    return {
        "ok": not stalled,
        "samples": len(samples),
        "movingSamples": moving_total,
        "movingWithDelta": moved_by_delta,
        "movingWithPositionStep": moved_by_position,
        "movingWithRouteStep": advanced_route,
        "stalledMovingSamples": stalled,
    }


def write_markdown(path: Path, result: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Select-Server Motion Progress",
        "",
        "## Summary",
        "",
        f"- ok: **{str(result['ok']).lower()}**",
        f"- samples: **{result['samples']}**",
        f"- moving samples: **{result['movingSamples']}**",
        f"- moving with delta: **{result['movingWithDelta']}**",
        f"- moving with position step: **{result['movingWithPositionStep']}**",
        f"- moving with route step: **{result['movingWithRouteStep']}**",
        f"- stalled moving samples: **{len(result['stalledMovingSamples'])}**",
        "",
    ]
    if result["stalledMovingSamples"]:
        lines.extend(["## Stalled Samples", ""])
        for item in result["stalledMovingSamples"][:100]:
            lines.append(
                "- "
                f"{item['source']} human {item['index']}: "
                f"motion={item['motion']} route={item['lastRouteIndex']}/{item['maxRouteIndex']} "
                f"progressRate={item['progressRate']}"
            )
        lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("telemetry_json", nargs="+", type=Path)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--md-out", type=Path)
    parser.add_argument(
        "--allow-no-moving",
        action="store_true",
        help="Return success when no moving humans were captured.",
    )
    args = parser.parse_args()

    samples: list[HumanSample] = []
    for path in args.telemetry_json:
        samples.extend(load_samples(path))

    result = analyze(samples)
    if not args.allow_no_moving and result["movingSamples"] == 0:
        result["ok"] = False
        result["error"] = "no moving human samples found"

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(result, indent=2), encoding="utf-8")
    if args.md_out:
        write_markdown(args.md_out, result)

    print(json.dumps(result, indent=2))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
