#!/usr/bin/env python3
"""Analyze select-server human telemetry snapshots for movement stalls.

The WASM select-server harness can expose per-human fields such as position,
target, progress rate, max speed, and route delta. This helper consumes one or
more telemetry JSON snapshots and flags humans that appear to have route intent
without measurable position movement across the sampled frames.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass
class HumanSample:
    frame: int
    index: int
    present: bool
    pos_x: float | None
    pos_y: float | None
    target_x: float | None
    target_y: float | None
    delta_x: float | None
    delta_y: float | None
    progress_rate: float | None
    max_speed: float | None
    sliding: float | None
    motion: int | None
    sent_motion: int | None


def as_number(value: Any) -> float | None:
    if value is None or isinstance(value, bool):
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def as_int(value: Any) -> int | None:
    number = as_number(value)
    if number is None:
        return None
    return int(number)


def get_first(mapping: dict[str, Any], *keys: str) -> Any:
    for key in keys:
        if key in mapping:
            return mapping[key]
    return None


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def frame_payloads(payload: Any) -> list[dict[str, Any]]:
    if isinstance(payload, list):
        return [item for item in payload if isinstance(item, dict)]
    if not isinstance(payload, dict):
        return []
    for key in ("frames", "snapshots", "samples"):
        value = payload.get(key)
        if isinstance(value, list):
            return [item for item in value if isinstance(item, dict)]
    return [payload]


def human_dicts(frame: dict[str, Any]) -> list[dict[str, Any]]:
    for key in ("humans", "selectServerHumans", "select_server_humans"):
        value = frame.get(key)
        if isinstance(value, list):
            return [item for item in value if isinstance(item, dict)]

    telemetry = frame.get("telemetry")
    if isinstance(telemetry, dict):
        nested = human_dicts(telemetry)
        if nested:
            return nested

    count = as_int(
        get_first(
            frame,
            "wyd_selserver_human_count",
            "_wyd_selserver_human_count",
            "human_count",
        )
    )
    if count is None:
        count = 8 if any("selserver_human" in key for key in frame) else 0

    humans: list[dict[str, Any]] = []
    for index in range(max(0, count)):
        entry: dict[str, Any] = {"index": index}
        for compact, canonical in (
            ("present", "present"),
            ("pos_x", "pos_x"),
            ("pos_y", "pos_y"),
            ("target_x", "target_x"),
            ("target_y", "target_y"),
            ("delta_x", "delta_x"),
            ("delta_y", "delta_y"),
            ("progress_rate", "progress_rate"),
            ("max_speed", "max_speed"),
            ("sliding", "sliding"),
            ("motion", "motion"),
            ("sent_motion", "sent_motion"),
        ):
            for key in (
                f"human_{index}_{compact}",
                f"humans.{index}.{compact}",
                f"wyd_selserver_human_{compact}_{index}",
                f"_wyd_selserver_human_{compact}_{index}",
            ):
                if key in frame:
                    entry[canonical] = frame[key]
                    break
            indexed_key = f"wyd_selserver_human_{compact}[{index}]"
            if indexed_key in frame:
                entry[canonical] = frame[indexed_key]
            indexed_key = f"_wyd_selserver_human_{compact}[{index}]"
            if indexed_key in frame:
                entry[canonical] = frame[indexed_key]
        humans.append(entry)
    return humans


def sample_from_human(frame_index: int, human: dict[str, Any], fallback_index: int) -> HumanSample:
    index = as_int(human.get("index"))
    if index is None:
        index = fallback_index
    present_raw = get_first(human, "present", "visible")
    present = True if present_raw is None else bool(as_number(present_raw))
    return HumanSample(
        frame=frame_index,
        index=index,
        present=present,
        pos_x=as_number(get_first(human, "pos_x", "x", "position_x")),
        pos_y=as_number(get_first(human, "pos_y", "y", "position_y")),
        target_x=as_number(get_first(human, "target_x", "targetX")),
        target_y=as_number(get_first(human, "target_y", "targetY")),
        delta_x=as_number(get_first(human, "delta_x", "deltaX")),
        delta_y=as_number(get_first(human, "delta_y", "deltaY")),
        progress_rate=as_number(get_first(human, "progress_rate", "progressRate")),
        max_speed=as_number(get_first(human, "max_speed", "maxSpeed")),
        sliding=as_number(human.get("sliding")),
        motion=as_int(human.get("motion")),
        sent_motion=as_int(human.get("sent_motion")),
    )


def read_samples(paths: list[Path]) -> list[HumanSample]:
    samples: list[HumanSample] = []
    frame_index = 0
    for path in paths:
        for frame in frame_payloads(load_json(path)):
            for fallback_index, human in enumerate(human_dicts(frame)):
                samples.append(sample_from_human(frame_index, human, fallback_index))
            frame_index += 1
    return samples


def hypot_or_none(x: float | None, y: float | None) -> float | None:
    if x is None or y is None:
        return None
    return math.hypot(x, y)


def analyze(samples: list[HumanSample], movement_epsilon: float, intent_epsilon: float) -> dict[str, Any]:
    by_human: dict[int, list[HumanSample]] = {}
    for sample in samples:
        if sample.present:
            by_human.setdefault(sample.index, []).append(sample)

    humans: list[dict[str, Any]] = []
    stalled = 0
    for index in sorted(by_human):
        series = sorted(by_human[index], key=lambda item: item.frame)
        positioned = [s for s in series if s.pos_x is not None and s.pos_y is not None]
        movement = None
        if len(positioned) >= 2:
            movement = math.hypot(positioned[-1].pos_x - positioned[0].pos_x, positioned[-1].pos_y - positioned[0].pos_y)

        max_route_delta = max(
            (hypot_or_none(s.delta_x, s.delta_y) or 0.0 for s in series),
            default=0.0,
        )
        max_target_distance = max(
            (
                hypot_or_none(
                    None if s.target_x is None or s.pos_x is None else s.target_x - s.pos_x,
                    None if s.target_y is None or s.pos_y is None else s.target_y - s.pos_y,
                )
                or 0.0
                for s in series
            ),
            default=0.0,
        )
        max_progress_rate = max((s.progress_rate or 0.0 for s in series), default=0.0)
        max_speed = max((s.max_speed or 0.0 for s in series), default=0.0)
        moving_intent = max(max_route_delta, max_target_distance, max_progress_rate, max_speed) > intent_epsilon
        has_stall = moving_intent and movement is not None and movement <= movement_epsilon
        if has_stall:
            stalled += 1

        humans.append(
            {
                "index": index,
                "frames": len(series),
                "movement": movement,
                "max_route_delta": max_route_delta,
                "max_target_distance": max_target_distance,
                "max_progress_rate": max_progress_rate,
                "max_speed": max_speed,
                "moving_intent": moving_intent,
                "stalled": has_stall,
                "first_frame": series[0].frame,
                "last_frame": series[-1].frame,
                "first_position": [positioned[0].pos_x, positioned[0].pos_y] if positioned else None,
                "last_position": [positioned[-1].pos_x, positioned[-1].pos_y] if positioned else None,
                "motions": sorted({s.motion for s in series if s.motion is not None}),
                "sent_motions": sorted({s.sent_motion for s in series if s.sent_motion is not None}),
            }
        )

    return {
        "sample_count": len(samples),
        "human_count": len(humans),
        "stalled_human_count": stalled,
        "movement_epsilon": movement_epsilon,
        "intent_epsilon": intent_epsilon,
        "humans": humans,
    }


def write_markdown(path: Path, report: dict[str, Any]) -> None:
    lines = [
        "# Select-Server Motion Trace",
        "",
        "## Summary",
        "",
        f"- samples: **{report['sample_count']}**",
        f"- humans: **{report['human_count']}**",
        f"- stalled humans: **{report['stalled_human_count']}**",
        f"- movement epsilon: `{report['movement_epsilon']}`",
        f"- intent epsilon: `{report['intent_epsilon']}`",
        "",
        "## Humans",
        "",
    ]
    for human in report["humans"]:
        state = "stalled" if human["stalled"] else "ok"
        lines.append(f"- human `{human['index']}`: **{state}**, frames={human['frames']}, movement={human['movement']}")
        lines.append(
            "  "
            f"route_delta={human['max_route_delta']}, "
            f"target_distance={human['max_target_distance']}, "
            f"progress_rate={human['max_progress_rate']}, "
            f"max_speed={human['max_speed']}"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("telemetry", nargs="+", type=Path, help="Telemetry JSON files, in frame order.")
    parser.add_argument("--movement-epsilon", type=float, default=0.001)
    parser.add_argument("--intent-epsilon", type=float, default=0.001)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--md-out", type=Path)
    args = parser.parse_args()

    report = analyze(read_samples(args.telemetry), args.movement_epsilon, args.intent_epsilon)
    text = json.dumps(report, indent=2)
    print(text)

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(text + "\n", encoding="utf-8")
    if args.md_out:
        write_markdown(args.md_out, report)
    return 1 if report["stalled_human_count"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
