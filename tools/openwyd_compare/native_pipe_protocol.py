"""Contract helpers for the debug-only native OPENWYD_COMPARE pipe.

This module deliberately contains no process discovery and no fallback to an
existing executable.  A controller supplies the pipe name to a freshly built
Debug|Win32 client and can use these helpers to encode commands and validate
the line responses.
"""

from __future__ import annotations

from dataclasses import dataclass


PROTOCOL_VERSION = 1
MAX_FRAME_ID = (1 << 64) - 1
MAX_TIME_MS = (1 << 32) - 1
MAX_RANDOM_SEED = (1 << 32) - 1
MAX_MOUSE_COORDINATE = (1 << 16) - 1
MAX_VIRTUAL_KEY = 254


class NativePipeProtocolError(ValueError):
    """Raised when a native comparison command or event violates version 1."""


@dataclass(frozen=True)
class NativePipeEvent:
    kind: str
    values: tuple[int | str | bool, ...] = ()


def _unsigned(value: int, maximum: int, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise NativePipeProtocolError(f"{field} must be an integer")
    if not 0 <= value <= maximum:
        raise NativePipeProtocolError(f"{field} must be in range 0..{maximum}")
    return value


def step_command(frame_id: int, time_ms: int) -> bytes:
    """Encode one monotonically assigned frame and controlled DWORD clock."""

    frame_id = _unsigned(frame_id, MAX_FRAME_ID, "frame_id")
    time_ms = _unsigned(time_ms, MAX_TIME_MS, "time_ms")
    return f"STEP {frame_id} {time_ms}\n".encode("ascii")


def ping_command() -> bytes:
    return b"PING\n"


def close_command() -> bytes:
    return b"CLOSE\n"


def random_seed_command(seed: int) -> bytes:
    seed = _unsigned(seed, MAX_RANDOM_SEED, "random_seed")
    return f"RANDOM_SEED {seed}\n".encode("ascii")


def _input_prefix(frame_id: int, kind: str) -> str:
    frame_id = _unsigned(frame_id, MAX_FRAME_ID, "frame_id")
    return f"INPUT {PROTOCOL_VERSION} {frame_id} {kind}"


def mouse_move_command(frame_id: int, x: int, y: int) -> bytes:
    x = _unsigned(x, MAX_MOUSE_COORDINATE, "x")
    y = _unsigned(y, MAX_MOUSE_COORDINATE, "y")
    return f"{_input_prefix(frame_id, 'MOUSE_MOVE')} {x} {y}\n".encode("ascii")


def _mouse_button_command(
    frame_id: int,
    button: str,
    x: int,
    y: int,
    *,
    down: bool,
) -> bytes:
    if not isinstance(button, str) or button.upper() not in {"LEFT", "RIGHT"}:
        raise NativePipeProtocolError("button must be LEFT or RIGHT")
    x = _unsigned(x, MAX_MOUSE_COORDINATE, "x")
    y = _unsigned(y, MAX_MOUSE_COORDINATE, "y")
    kind = "MOUSE_DOWN" if down else "MOUSE_UP"
    return (
        f"{_input_prefix(frame_id, kind)} {button.upper()} {x} {y}\n".encode("ascii")
    )


def mouse_down_command(frame_id: int, button: str, x: int, y: int) -> bytes:
    return _mouse_button_command(frame_id, button, x, y, down=True)


def mouse_up_command(frame_id: int, button: str, x: int, y: int) -> bytes:
    return _mouse_button_command(frame_id, button, x, y, down=False)


def key_down_command(frame_id: int, virtual_key: int) -> bytes:
    virtual_key = _unsigned(virtual_key, MAX_VIRTUAL_KEY, "virtual_key")
    if virtual_key == 0:
        raise NativePipeProtocolError("virtual_key must be in range 1..254")
    return f"{_input_prefix(frame_id, 'KEY_DOWN')} {virtual_key}\n".encode("ascii")


def key_up_command(frame_id: int, virtual_key: int) -> bytes:
    virtual_key = _unsigned(virtual_key, MAX_VIRTUAL_KEY, "virtual_key")
    if virtual_key == 0:
        raise NativePipeProtocolError("virtual_key must be in range 1..254")
    return f"{_input_prefix(frame_id, 'KEY_UP')} {virtual_key}\n".encode("ascii")


def char_command(frame_id: int, character: int | str) -> bytes:
    if isinstance(character, str):
        if len(character) != 1:
            raise NativePipeProtocolError("character text must contain exactly one code point")
        try:
            encoded = character.encode("cp1252")
        except UnicodeEncodeError as error:
            raise NativePipeProtocolError("character is not representable in CP1252") from error
        value = encoded[0]
    else:
        value = _unsigned(character, 255, "character")
    if value == 0:
        raise NativePipeProtocolError("CP1252 NUL is not accepted")
    return f"{_input_prefix(frame_id, 'CHAR')} {value}\n".encode("ascii")


def text_commands(frame_id: int, text: str) -> tuple[bytes, ...]:
    if not isinstance(text, str):
        raise NativePipeProtocolError("text must be a string")
    return tuple(char_command(frame_id, character) for character in text)


def _parse_uint(token: str, maximum: int, field: str) -> int:
    if not token or not token.isascii() or not token.isdecimal():
        raise NativePipeProtocolError(f"{field} is not an unsigned decimal integer")
    return _unsigned(int(token, 10), maximum, field)


def parse_event(line: bytes | str) -> NativePipeEvent:
    """Parse one complete ASCII response line from the native client."""

    try:
        text = line.decode("ascii") if isinstance(line, bytes) else line
    except UnicodeDecodeError as error:
        raise NativePipeProtocolError("native event is not ASCII") from error
    if not isinstance(text, str):
        raise NativePipeProtocolError("native event must be bytes or text")

    fields = text.rstrip("\r\n").split()
    if not fields:
        raise NativePipeProtocolError("native event is empty")
    kind = fields[0]

    if kind == "READY" and len(fields) == 6:
        version = _parse_uint(fields[1], 0xFFFF, "protocol_version")
        if version != PROTOCOL_VERSION:
            raise NativePipeProtocolError(
                f"unsupported native protocol version: {version}"
            )
        pid = _parse_uint(fields[2], 0xFFFFFFFF, "process_id")
        width = _parse_uint(fields[3], 0xFFFFFFFF, "width")
        height = _parse_uint(fields[4], 0xFFFFFFFF, "height")
        capture = _parse_uint(fields[5], 1, "capture_enabled")
        return NativePipeEvent(kind, (version, pid, width, height, bool(capture)))

    if kind == "STEP_ACCEPTED" and len(fields) == 3:
        return NativePipeEvent(
            kind,
            (
                _parse_uint(fields[1], MAX_FRAME_ID, "frame_id"),
                _parse_uint(fields[2], MAX_TIME_MS, "time_ms"),
            ),
        )

    if kind == "PRESENT" and len(fields) == 5:
        frame_id = _parse_uint(fields[1], MAX_FRAME_ID, "frame_id")
        time_ms = _parse_uint(fields[2], MAX_TIME_MS, "time_ms")
        hresult_text = fields[3]
        if (
            len(hresult_text) != 10
            or not hresult_text.startswith("0x")
            or any(character not in "0123456789abcdefABCDEF" for character in hresult_text[2:])
        ):
            raise NativePipeProtocolError("capture_hresult must be 0x plus 8 hex digits")
        hresult = int(hresult_text[2:], 16)
        snapshot = _parse_uint(fields[4], 1, "snapshot_written")
        return NativePipeEvent(
            kind,
            (frame_id, time_ms, hresult, bool(snapshot)),
        )

    if kind == "INPUT_QUEUED" and len(fields) == 3:
        return NativePipeEvent(
            kind,
            (
                _parse_uint(fields[1], MAX_FRAME_ID, "input_sequence"),
                _parse_uint(fields[2], MAX_FRAME_ID, "frame_id"),
            ),
        )

    if kind == "RANDOM_SEEDED" and len(fields) == 2:
        return NativePipeEvent(
            kind,
            (_parse_uint(fields[1], MAX_RANDOM_SEED, "random_seed"),),
        )

    if kind == "ERROR" and len(fields) == 2:
        return NativePipeEvent(kind, (fields[1],))

    if kind in {"PONG", "CLOSING", "BYE"} and len(fields) == 1:
        return NativePipeEvent(kind)

    raise NativePipeProtocolError(f"malformed or unknown native event: {text!r}")
