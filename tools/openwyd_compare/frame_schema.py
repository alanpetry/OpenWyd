"""Shared, deliberately small debug-frame contract.

The native client and the WASM client can emit much richer diagnostics over
time without changing the orchestration protocol: implementation-specific
data belongs under ``extensions`` until it becomes part of a later schema
version.
"""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any


FRAME_SCHEMA = "openwyd.debug-frame"
FRAME_SCHEMA_VERSION = 1
FRAME_FIELDS = (
    "frame_id",
    "state",
    "ticks",
    "clock",
    "camera",
    "matrices",
    "draws",
    "render",
    "network",
    "extensions",
)


class FrameSchemaError(ValueError):
    """Raised when a producer emits an incompatible debug-frame record."""


def new_frame_record(
    frame_id: str | int,
    *,
    state: str | int | Mapping[str, Any] | None = None,
    ticks: Mapping[str, Any] | None = None,
    clock: Mapping[str, Any] | None = None,
    camera: Mapping[str, Any] | None = None,
    matrices: Mapping[str, Any] | None = None,
    draws: list[Mapping[str, Any]] | None = None,
    render: Mapping[str, Any] | None = None,
    network: Mapping[str, Any] | None = None,
    extensions: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Create a valid version-1 frame record with empty optional sections."""

    record: dict[str, Any] = {
        "schema": FRAME_SCHEMA,
        "schema_version": FRAME_SCHEMA_VERSION,
        "frame_id": frame_id,
        "state": state,
        "ticks": dict(ticks or {}),
        "clock": dict(clock or {}),
        "camera": dict(camera or {}),
        "matrices": dict(matrices or {}),
        "draws": [dict(draw) for draw in (draws or [])],
        "render": dict(render or {}),
        "network": dict(network or {}),
        "extensions": dict(extensions or {}),
    }
    validate_frame_record(record)
    return record


def validate_frame_record(record: Mapping[str, Any]) -> None:
    """Validate the dependency-free subset mirrored by ``frame.schema.json``."""

    if not isinstance(record, Mapping):
        raise FrameSchemaError("frame record must be a JSON object")

    expected = {"schema", "schema_version", *FRAME_FIELDS}
    missing = sorted(expected.difference(record))
    unknown = sorted(set(record).difference(expected))
    if missing:
        raise FrameSchemaError(f"frame record is missing fields: {', '.join(missing)}")
    if unknown:
        raise FrameSchemaError(
            "unknown top-level frame fields must be placed under extensions: "
            + ", ".join(unknown)
        )
    if record["schema"] != FRAME_SCHEMA:
        raise FrameSchemaError(f"unsupported frame schema: {record['schema']!r}")
    if record["schema_version"] != FRAME_SCHEMA_VERSION:
        raise FrameSchemaError(
            f"unsupported frame schema version: {record['schema_version']!r}"
        )

    frame_id = record["frame_id"]
    if isinstance(frame_id, bool) or not isinstance(frame_id, (str, int)):
        raise FrameSchemaError("frame_id must be a string or integer")
    if isinstance(frame_id, str) and not frame_id:
        raise FrameSchemaError("frame_id must not be empty")

    state = record["state"]
    if isinstance(state, bool) or (
        state is not None and not isinstance(state, (str, int, Mapping))
    ):
        raise FrameSchemaError("state must be null, a string, an integer, or an object")

    for field in (
        "ticks",
        "clock",
        "camera",
        "matrices",
        "render",
        "network",
        "extensions",
    ):
        if not isinstance(record[field], Mapping):
            raise FrameSchemaError(f"{field} must be a JSON object")

    if not isinstance(record["draws"], list):
        raise FrameSchemaError("draws must be a JSON array")
    if any(not isinstance(draw, Mapping) for draw in record["draws"]):
        raise FrameSchemaError("every draws entry must be a JSON object")
