#!/usr/bin/env python3
"""Find select-server demo humans whose route state advances without movement.

The WASM startup harness can sample the `wyd_selserver_human_*` exports into a
JSON file. This helper keeps the route-motion validation separate from visual
comparison: it flags humans that look active by speed, target, or progress rate
but keep reporting near-zero XY deltas across consecutive samples.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


@dataclass
class HumanSample:
    frame: int
    index: int
    present: bool
    pos_x: float
    pos_y: float
    delta_x: float
    delta_y: float
    target_x: float
    target_y: float
    max_speed: float
    progress_rate: float

    @property
    def move_delta(self) -> float:
        return math.hypot(self.delta_x, self.delta_y)

    @property
    def target_distance(self) -> float:
        return math.hypot(self.target_x - self.pos_x, self.target_y - self.pos_y)


def as_float(value: Any, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def as_bool(value: Any) -> bool:
    if isinstance(value, str):
        return value.lower() not in {"", "0", "false", "no", "none"}
    return bool(value)


def pick(data: dict[str, Any], *names: str, default: Any = 0) -> Any:
    for name in names:
        if name in data:
            return data[name]
    return default


def iter_human_dicts(payload: Any) -> Iterable[tuple[int, int, dict[str, Any]]]:
    """Accept common frame shapes from Playwright or ad-hoc ccall dumps."""
    if isinstance(payload, dict):
        if "frames" in payload:
            payload = payload["frames"]
        elif "samples" in payload:
            payload = payload["samples"]
        else:
            payload = [payload]

    if not isinstance(payload, list):
        raise TypeError("expected a JSON object or array of frame samples")

    for frame_index, frame in enumerate(payload):
        if not isinstance(frame, dict):
            continue
        frame_id = int(pick(frame, "frame", "frameIndex", default=frame_index))
        humans = pick(frame, "humans", "humanTelemetry", "selserverHumans", default=[])
        if isinstance(humans, dict):
            iterable = humans.items()
        elif isinstance(humans, list):
            iterable = enumerate(humans)
        else:
            continue

        for human_index, human in iterable:
            if not isinstance(human, dict):
                continue
            yield frame_id, int(human_index), human


def parse_samples(payload: Any) -> list[HumanSample]:
    samples: list[HumanSample] = []
    for frame_id, index, human in iter_human_dicts(payload):
        samples.append(
            HumanSample(
                frame=frame_id,
                index=int(pick(human, "index", "humanIndex", default=index)),
                present=as_bool(pick(human, "present", default=1)),
                pos_x=as_float(pick(human, "posX", "pos_x", "x")),
                pos_y=as_float(pick(human, "posY", "pos_y", "y")),
                delta_x=as_float(pick(human, "deltaX", "delta_x", "dx")),
                delta_y=as_float(pick(human, "deltaY", "delta_y", "dy")),
                target_x=as_float(pick(human, "targetX", "target_x")),
                target_y=as_float(pick(human, "targetY", "target_y")),
                max_speed=as_float(pick(human, "maxSpeed", "max_speed")),
                progress_rate=as_float(pick(human, "progressRate", "progress_rate")),
            )
        )
    return samples


def is_route_candidate(sample: HumanSample, target_epsilon: float) -> bool:
    if not sample.present:
        return False
    if sample.progress_rate > 0.0:
        return True
    if sample.max_speed > 0.0 and sample.target_distance > target_epsilon:
        return True
    return False


def find_stalls(
    samples: list[HumanSample],
    min_frames: int,
    delta_epsilon: float,
    target_epsilon: float,
) -> list[dict[str, Any]]:
    by_human: dict[int, list[HumanSample]] = {}
    for sample in samples:
        by_human.setdefault(sample.index, []).append(sample)

    stalls: list[dict[str, Any]] = []
    for index, human_samples in sorted(by_human.items()):
        current: list[HumanSample] = []
        for sample in sorted(human_samples, key=lambda item: item.frame):
            stalled = (
                is_route_candidate(sample, target_epsilon)
                and sample.move_delta <= delta_epsilon
                and sample.target_distance > target_epsilon
            )
            if stalled:
                current.append(sample)
                continue
            if len(current) >= min_frames:
                stalls.append(summarize_stall(index, current))
            current = []
        if len(current) >= min_frames:
            stalls.append(summarize_stall(index, current))
    return stalls


def summarize_stall(index: int, samples: list[HumanSample]) -> dict[str, Any]:
    first = samples[0]
    last = samples[-1]
    return {
        "human": index,
        "frames": len(samples),
        "firstFrame": first.frame,
        "lastFrame": last.frame,
        "pos": [round(last.pos_x, 4), round(last.pos_y, 4)],
        "target": [round(last.target_x, 4), round(last.target_y, 4)],
        "targetDistance": round(last.target_distance, 4),
        "maxSpeed": round(last.max_speed, 4),
        "progressRate": round(last.progress_rate, 4),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("telemetry_json", type=Path)
    parser.add_argument("--min-frames", type=int, default=3)
    parser.add_argument("--delta-epsilon", type=float, default=0.001)
    parser.add_argument("--target-epsilon", type=float, default=0.25)
    parser.add_argument("--json", action="store_true", help="Emit machine-readable findings.")
    args = parser.parse_args()

    payload = json.loads(args.telemetry_json.read_text(encoding="utf-8"))
    samples = parse_samples(payload)
    stalls = find_stalls(samples, args.min_frames, args.delta_epsilon, args.target_epsilon)

    if args.json:
        print(json.dumps({"samples": len(samples), "stalls": stalls}, indent=2))
    elif stalls:
        print(f"Route stalls detected: {len(stalls)}")
        for stall in stalls:
            print(
                "human {human}: frames {firstFrame}-{lastFrame} "
                "targetDistance={targetDistance} maxSpeed={maxSpeed} "
                "progressRate={progressRate}".format(**stall)
            )
    else:
        print(f"No route stalls detected across {len(samples)} samples.")

    return 1 if stalls else 0


if __name__ == "__main__":
    raise SystemExit(main())
