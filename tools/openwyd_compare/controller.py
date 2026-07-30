"""Source-driven process orchestration for paired OpenWyd frame captures.

The controller intentionally knows nothing about checked-in or previously
compiled game binaries.  Every process command comes from JSON configuration,
which lets the native client, servers, proxies and browser controller point at
the outputs produced by the current source build.
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import os
import re
import shutil
import socket
import subprocess
import sys
import time
import uuid
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Mapping, Sequence

from PIL import UnidentifiedImageError

from . import frame_compare, paired_report
from .frame_schema import FrameSchemaError, validate_frame_record


CONTROLLER_SCHEMA = "openwyd.compare-run"
CONTROLLER_SCHEMA_VERSION = 1
CONFIG_VERSION = 1
CAPTURE_WIDTH = 800
CAPTURE_HEIGHT = 600
REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RUN_ROOT = (
    REPO_ROOT / "artifacts" / "openwyd_compare" / "runs"
)
CAPTURE_HELPER = Path(__file__).with_name("capture_webgl_canvas.mjs")
FRAME_SCHEMA_PATH = Path(__file__).with_name("frame.schema.json")

COMPARISON_OPTION_NAMES = {
    "alpha_mode",
    "candidate_orientation",
    "heatmap_gain",
    "reference_orientation",
    "resize_filter",
    "size_policy",
    "ssim_window",
    "target_size",
    "threshold",
}


class ControllerError(RuntimeError):
    """Base class for actionable controller failures."""


class ConfigError(ControllerError):
    """Raised when controller JSON is structurally invalid."""


class RunFailed(ControllerError):
    """A run failed after its artifact directory was created."""

    def __init__(self, message: str, run_dir: Path):
        super().__init__(message)
        self.run_dir = run_dir


@dataclass(frozen=True)
class LoadedConfig:
    path: Path
    directory: Path
    data: dict[str, Any]


@dataclass
class ManagedProcess:
    popen: subprocess.Popen[bytes]
    log_path: Path
    record: dict[str, Any]
    shutdown_timeout: float


def _utc_now() -> str:
    return datetime.now(UTC).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def _write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = json.dumps(
        value,
        ensure_ascii=False,
        indent=2,
        sort_keys=True,
        allow_nan=False,
    )
    path.write_text(encoded + "\n", encoding="utf-8", newline="\n")


def _safe_name(value: str, *, fallback: str) -> str:
    safe = re.sub(r"[^A-Za-z0-9._-]+", "-", value).strip("-")
    return (safe or fallback)[:96]


def _path_from(directory: Path, value: str | os.PathLike[str]) -> Path:
    path = Path(value)
    if not path.is_absolute():
        path = directory / path
    return path.resolve()


def _number(
    value: Any,
    description: str,
    *,
    minimum: float = 0.0,
    strictly_greater: bool = False,
) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ConfigError(f"{description} must be a number")
    number = float(value)
    if not math.isfinite(number):
        raise ConfigError(f"{description} must be finite")
    invalid = number <= minimum if strictly_greater else number < minimum
    if invalid:
        relation = "greater than" if strictly_greater else "at least"
        raise ConfigError(f"{description} must be {relation} {minimum}")
    return number


def _command(value: Any, description: str) -> list[str]:
    if isinstance(value, str):
        if not value:
            raise ConfigError(f"{description} must not be empty")
        return [value]
    if (
        not isinstance(value, list)
        or not value
        or any(not isinstance(item, str) or not item for item in value)
    ):
        raise ConfigError(f"{description} must be a non-empty string array")
    return list(value)


def _readiness_rules(value: Any, process_name: str) -> list[dict[str, Any]]:
    if value is None:
        rules: list[Any] = [{"type": "process", "min_uptime_seconds": 0.1}]
    elif isinstance(value, Mapping):
        rules = [value]
    elif isinstance(value, list) and value:
        rules = value
    else:
        raise ConfigError(
            f"process {process_name!r} readiness must be an object or non-empty array"
        )

    normalized: list[dict[str, Any]] = []
    for index, original in enumerate(rules):
        if not isinstance(original, Mapping):
            raise ConfigError(
                f"process {process_name!r} readiness[{index}] must be an object"
            )
        rule = dict(original)
        kind = rule.get("type", "process")
        if kind not in {"process", "tcp", "log"}:
            raise ConfigError(
                f"process {process_name!r} readiness[{index}] has unknown type {kind!r}"
            )
        rule["type"] = kind
        timeout = rule.get("timeout_seconds", 30)
        _number(
            timeout,
            f"process {process_name!r} readiness[{index}].timeout_seconds",
            strictly_greater=True,
        )
        if kind == "process":
            _number(
                rule.get("min_uptime_seconds", 0.1),
                f"process {process_name!r} readiness[{index}].min_uptime_seconds",
            )
        elif kind == "tcp":
            host = rule.get("host", "127.0.0.1")
            port = rule.get("port")
            if not isinstance(host, str) or not host:
                raise ConfigError(
                    f"process {process_name!r} TCP readiness host must be a string"
                )
            if isinstance(port, bool) or not isinstance(port, int) or not 1 <= port <= 65535:
                raise ConfigError(
                    f"process {process_name!r} TCP readiness port must be 1..65535"
                )
        else:
            pattern = rule.get("pattern")
            if not isinstance(pattern, str) or not pattern:
                raise ConfigError(
                    f"process {process_name!r} log readiness pattern must be a string"
                )
            try:
                re.compile(pattern)
            except re.error as error:
                raise ConfigError(
                    f"process {process_name!r} log readiness regex is invalid: {error}"
                ) from error
        normalized.append(rule)
    return normalized


def _validate_config(data: Any) -> dict[str, Any]:
    if not isinstance(data, Mapping):
        raise ConfigError("controller config must be a JSON object")
    config = copy.deepcopy(dict(data))
    if config.get("version", CONFIG_VERSION) != CONFIG_VERSION:
        raise ConfigError(
            f"unsupported controller config version: {config.get('version')!r}"
        )
    config["version"] = CONFIG_VERSION

    if "run_root" in config and not isinstance(config["run_root"], str):
        raise ConfigError("run_root must be a path string")
    if "cwd" in config and not isinstance(config["cwd"], str):
        raise ConfigError("cwd must be a path string")
    if "hold_seconds" in config:
        _number(config["hold_seconds"], "hold_seconds")

    processes = config.get("processes", [])
    if not isinstance(processes, list):
        raise ConfigError("processes must be an array")
    names: set[str] = set()
    for index, process in enumerate(processes):
        if not isinstance(process, Mapping):
            raise ConfigError(f"processes[{index}] must be an object")
        name = process.get("name")
        if not isinstance(name, str) or not name:
            raise ConfigError(f"processes[{index}].name must be a non-empty string")
        if name in names:
            raise ConfigError(f"process name is duplicated: {name!r}")
        names.add(name)
        if "enabled" in process and not isinstance(process["enabled"], bool):
            raise ConfigError(f"process {name!r} enabled must be a boolean")
        if process.get("enabled", True) is not False:
            _command(process.get("command"), f"process {name!r} command")
        if "cwd" in process and not isinstance(process["cwd"], str):
            raise ConfigError(f"process {name!r} cwd must be a path string")
        env = process.get("env", {})
        if not isinstance(env, Mapping) or any(
            not isinstance(key, str) or not isinstance(value, str)
            for key, value in env.items()
        ):
            raise ConfigError(f"process {name!r} env must map strings to strings")
        _readiness_rules(process.get("readiness"), name)
        if "shutdown_timeout_seconds" in process:
            _number(
                process["shutdown_timeout_seconds"],
                f"process {name!r} shutdown_timeout_seconds",
                strictly_greater=True,
            )

    captures = config.get("captures", [])
    if not isinstance(captures, list):
        raise ConfigError("captures must be an array")
    for index, capture in enumerate(captures):
        if not isinstance(capture, Mapping):
            raise ConfigError(f"captures[{index}] must be an object")
        url = capture.get("url")
        if not isinstance(url, str) or not url:
            raise ConfigError(f"captures[{index}].url must be a non-empty string")
        width = capture.get("width", CAPTURE_WIDTH)
        height = capture.get("height", CAPTURE_HEIGHT)
        if width != CAPTURE_WIDTH or height != CAPTURE_HEIGHT:
            raise ConfigError(
                f"captures[{index}] must use the exact {CAPTURE_WIDTH}x{CAPTURE_HEIGHT} "
                "backing canvas"
            )
        _command(
            capture.get("node_command", config.get("node_command", ["node"])),
            f"captures[{index}].node_command",
        )
        for field in (
            "name",
            "selector",
            "wait_expression",
            "metadata_expression",
            "node_cwd",
            "browser",
            "reference_png",
        ):
            if field in capture and not isinstance(capture[field], str):
                raise ConfigError(f"captures[{index}].{field} must be a string")
        capture_env = capture.get("env", {})
        if not isinstance(capture_env, Mapping) or any(
            not isinstance(key, str) or not isinstance(value, str)
            for key, value in capture_env.items()
        ):
            raise ConfigError(f"captures[{index}].env must map strings to strings")
        if "launch_args" in capture and (
            not isinstance(capture["launch_args"], list)
            or any(not isinstance(value, str) for value in capture["launch_args"])
        ):
            raise ConfigError(f"captures[{index}].launch_args must be a string array")
        if "headful" in capture and not isinstance(capture["headful"], bool):
            raise ConfigError(f"captures[{index}].headful must be a boolean")
        nested_comparison = capture.get("comparison", {})
        if not isinstance(nested_comparison, Mapping):
            raise ConfigError(f"captures[{index}].comparison must be an object")
        nested_options = nested_comparison.get("options", {})
        if not isinstance(nested_options, Mapping):
            raise ConfigError(
                f"captures[{index}].comparison.options must be an object"
            )
        nested_unknown = sorted(
            set(nested_options).difference(COMPARISON_OPTION_NAMES)
        )
        if nested_unknown:
            raise ConfigError(
                f"captures[{index}].comparison has unknown options: "
                + ", ".join(nested_unknown)
            )
        _number(
            capture.get("timeout_seconds", 30),
            f"captures[{index}].timeout_seconds",
            strictly_greater=True,
        )
        settle_frames = capture.get("settle_frames", 1)
        if (
            isinstance(settle_frames, bool)
            or not isinstance(settle_frames, int)
            or settle_frames < 0
        ):
            raise ConfigError(f"captures[{index}].settle_frames must be an integer >= 0")

    comparisons = config.get("comparisons", [])
    if not isinstance(comparisons, list):
        raise ConfigError("comparisons must be an array")
    for index, comparison in enumerate(comparisons):
        if not isinstance(comparison, Mapping):
            raise ConfigError(f"comparisons[{index}] must be an object")
        for field in ("reference_png", "candidate_png"):
            if not isinstance(comparison.get(field), str) or not comparison[field]:
                raise ConfigError(
                    f"comparisons[{index}].{field} must be a non-empty path string"
                )
        options = comparison.get("options", {})
        if not isinstance(options, Mapping):
            raise ConfigError(f"comparisons[{index}].options must be an object")
        unknown = sorted(set(options).difference(COMPARISON_OPTION_NAMES))
        if unknown:
            raise ConfigError(
                f"comparisons[{index}] has unknown options: {', '.join(unknown)}"
            )

    return config


def load_controller_config(path: str | Path) -> LoadedConfig:
    config_path = Path(path).resolve()
    try:
        source = json.loads(config_path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise ConfigError(f"controller config does not exist: {config_path}") from error
    except json.JSONDecodeError as error:
        raise ConfigError(
            f"invalid controller JSON at line {error.lineno}, column {error.colno}: "
            f"{error.msg}"
        ) from error
    return LoadedConfig(
        path=config_path,
        directory=config_path.parent,
        data=_validate_config(source),
    )


def _configured_cwd(
    loaded: LoadedConfig,
    value: str | None,
    *,
    fallback: Path | None = None,
) -> Path:
    if value is not None:
        return _path_from(loaded.directory, value)
    if fallback is not None:
        return fallback.resolve()
    default = loaded.data.get("cwd")
    if default is not None:
        if not isinstance(default, str):
            raise ConfigError("cwd must be a path string")
        return _path_from(loaded.directory, default)
    return loaded.directory


def _manifest_event(
    manifest: dict[str, Any],
    event: str,
    **details: Any,
) -> None:
    manifest["events"].append(
        {
            "at": _utc_now(),
            "event": event,
            **details,
        }
    )


def _save_manifest(run_dir: Path, manifest: dict[str, Any]) -> None:
    _write_json(run_dir / "run.json", manifest)


def _new_run_directory(
    loaded: LoadedConfig,
    *,
    run_dir: str | Path | None,
    run_root: str | Path | None,
) -> Path:
    if run_dir is not None:
        requested = Path(run_dir)
        if not requested.is_absolute():
            requested = Path.cwd() / requested
        requested = requested.resolve()
        if requested.exists():
            if any(requested.iterdir()):
                raise ConfigError(f"run directory is not empty: {requested}")
        else:
            requested.mkdir(parents=True)
        return requested

    if run_root is not None:
        root = Path(run_root)
        if not root.is_absolute():
            root = Path.cwd() / root
        root = root.resolve()
    elif "run_root" in loaded.data:
        root = _path_from(loaded.directory, loaded.data["run_root"])
    else:
        root = DEFAULT_RUN_ROOT
    root.mkdir(parents=True, exist_ok=True)

    stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%S")
    for _ in range(20):
        identifier = f"{stamp}Z-{os.getpid()}-{uuid.uuid4().hex[:8]}"
        candidate = root / identifier
        try:
            candidate.mkdir()
            return candidate.resolve()
        except FileExistsError:
            continue
    raise ControllerError(f"could not allocate a unique run directory under {root}")


def _start_process(
    loaded: LoadedConfig,
    spec: Mapping[str, Any],
    run_dir: Path,
    manifest: dict[str, Any],
    index: int,
) -> ManagedProcess:
    name = str(spec["name"])
    role = str(spec.get("role", name))
    command = _command(spec["command"], f"process {name!r} command")
    cwd = _configured_cwd(loaded, spec.get("cwd"))
    if not cwd.is_dir():
        raise ControllerError(f"process {name!r} cwd does not exist: {cwd}")
    explicit_env = dict(spec.get("env", {}))
    environment = os.environ.copy()
    environment.update(explicit_env)

    log_path = run_dir / "logs" / f"{index:02d}-{_safe_name(name, fallback='process')}.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_stream = log_path.open("ab", buffering=0)

    creationflags = 0
    if os.name == "nt":
        creationflags = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    record: dict[str, Any] = {
        "command": command,
        "cwd": str(cwd),
        "env": explicit_env,
        "log": str(log_path.relative_to(run_dir)),
        "name": name,
        "readiness": [],
        "role": role,
        "started_at": _utc_now(),
        "status": "starting",
    }
    manifest["processes"].append(record)
    _manifest_event(manifest, "process_starting", name=name, role=role)
    _save_manifest(run_dir, manifest)

    try:
        popen = subprocess.Popen(
            command,
            cwd=cwd,
            env=environment,
            stdin=subprocess.DEVNULL,
            stdout=log_stream,
            stderr=subprocess.STDOUT,
            shell=False,
            creationflags=creationflags,
        )
    except OSError:
        record["status"] = "start-failed"
        record["finished_at"] = _utc_now()
        _save_manifest(run_dir, manifest)
        raise
    finally:
        # CreateProcess duplicates the configured standard handles into the
        # child.  Keeping the parent's file object alive for the whole run is
        # unnecessary and, on Windows, leaves the artifact locked until
        # shutdown cleanup reaches this ManagedProcess.  Readiness reads the
        # path through independent short-lived handles, so release the parent
        # handle as soon as process creation has completed.
        log_stream.close()

    record["pid"] = popen.pid
    record["status"] = "waiting-readiness"
    _manifest_event(manifest, "process_started", name=name, pid=popen.pid)
    _save_manifest(run_dir, manifest)
    return ManagedProcess(
        popen=popen,
        log_path=log_path,
        record=record,
        shutdown_timeout=float(spec.get("shutdown_timeout_seconds", 5)),
    )


def _process_exit_message(process: ManagedProcess) -> str:
    return_code = process.popen.poll()
    return (
        f"process {process.record['name']!r} exited with code {return_code} "
        "before readiness"
    )


def _wait_for_windows_delete_access(path: Path, timeout: float) -> None:
    if os.name != "nt":
        return

    import ctypes
    from ctypes import wintypes

    delete_access = 0x00010000
    share_read = 0x00000001
    share_write = 0x00000002
    share_delete = 0x00000004
    open_existing = 3
    file_attribute_normal = 0x00000080
    invalid_handle_value = ctypes.c_void_p(-1).value
    retryable_errors = {5, 32, 33}  # access denied, sharing/lock violation

    create_file = ctypes.WinDLL("kernel32", use_last_error=True).CreateFileW
    create_file.argtypes = (
        wintypes.LPCWSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.LPVOID,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.HANDLE,
    )
    create_file.restype = wintypes.HANDLE
    close_handle = ctypes.WinDLL("kernel32", use_last_error=True).CloseHandle
    close_handle.argtypes = (wintypes.HANDLE,)
    close_handle.restype = wintypes.BOOL

    deadline = time.monotonic() + timeout
    while True:
        handle = create_file(
            str(path),
            delete_access,
            share_read | share_write | share_delete,
            None,
            open_existing,
            file_attribute_normal,
            None,
        )
        if handle != invalid_handle_value:
            close_handle(handle)
            return

        error_code = ctypes.get_last_error()
        if error_code not in retryable_errors or time.monotonic() >= deadline:
            raise OSError(
                error_code,
                f"log artifact did not become releasable: {path}",
            )
        time.sleep(0.01)


def _wait_for_rule(process: ManagedProcess, rule: Mapping[str, Any]) -> dict[str, Any]:
    kind = str(rule["type"])
    timeout = float(rule.get("timeout_seconds", 30))
    deadline = time.monotonic() + timeout
    started = time.monotonic()
    pattern = re.compile(str(rule["pattern"])) if kind == "log" else None
    last_error = ""

    while True:
        if process.popen.poll() is not None:
            raise ControllerError(_process_exit_message(process))

        ready = False
        if kind == "process":
            ready = time.monotonic() - started >= float(
                rule.get("min_uptime_seconds", 0.1)
            )
        elif kind == "tcp":
            try:
                with socket.create_connection(
                    (str(rule.get("host", "127.0.0.1")), int(rule["port"])),
                    timeout=min(0.25, max(0.05, deadline - time.monotonic())),
                ):
                    ready = True
            except OSError as error:
                last_error = str(error)
        else:
            try:
                contents = process.log_path.read_text(
                    encoding="utf-8",
                    errors="replace",
                )
                ready = pattern.search(contents) is not None if pattern else False
            except OSError as error:
                last_error = str(error)

        if ready:
            result = {
                "elapsed_seconds": round(time.monotonic() - started, 3),
                "status": "ready",
                "type": kind,
            }
            if kind == "tcp":
                result.update(
                    {
                        "host": str(rule.get("host", "127.0.0.1")),
                        "port": int(rule["port"]),
                    }
                )
            elif kind == "log":
                result["pattern"] = str(rule["pattern"])
            return result

        if time.monotonic() >= deadline:
            detail = f"; last error: {last_error}" if last_error else ""
            raise ControllerError(
                f"process {process.record['name']!r} did not satisfy {kind} "
                f"readiness in {timeout:g}s{detail}"
            )
        time.sleep(0.05)


def _wait_for_process(
    process: ManagedProcess,
    rules: list[dict[str, Any]],
    run_dir: Path,
    manifest: dict[str, Any],
) -> None:
    for rule in rules:
        result = _wait_for_rule(process, rule)
        process.record["readiness"].append(result)
        _manifest_event(
            manifest,
            "readiness_satisfied",
            name=process.record["name"],
            readiness_type=rule["type"],
        )
        _save_manifest(run_dir, manifest)
    process.record["ready_at"] = _utc_now()
    process.record["status"] = "ready"
    _manifest_event(
        manifest,
        "process_ready",
        name=process.record["name"],
        pid=process.popen.pid,
    )
    _save_manifest(run_dir, manifest)


def _stop_process(
    process: ManagedProcess,
    run_dir: Path,
    manifest: dict[str, Any],
) -> None:
    name = str(process.record["name"])
    manifest["shutdown_order"].append(name)
    action = "already-exited"
    try:
        if process.popen.poll() is None:
            action = "terminate"
            process.popen.terminate()
            try:
                process.popen.wait(timeout=process.shutdown_timeout)
            except subprocess.TimeoutExpired:
                action = "kill"
                process.popen.kill()
                process.popen.wait(timeout=process.shutdown_timeout)
        _wait_for_windows_delete_access(
            process.log_path,
            process.shutdown_timeout,
        )
        process.record["return_code"] = process.popen.returncode
        process.record["shutdown_action"] = action
        process.record["status"] = "stopped"
    except OSError as error:
        process.record["shutdown_action"] = action
        process.record["shutdown_error"] = str(error)
        process.record["status"] = "stop-failed"
        manifest["errors"].append(
            {"at": _utc_now(), "message": f"could not stop {name!r}: {error}"}
        )
    finally:
        process.record["finished_at"] = _utc_now()
        _manifest_event(
            manifest,
            "process_stopped",
            action=action,
            name=name,
            return_code=process.popen.returncode,
        )
        _save_manifest(run_dir, manifest)


def _capture_node_cwd(loaded: LoadedConfig, capture: Mapping[str, Any]) -> Path:
    value = capture.get("node_cwd", loaded.data.get("node_cwd"))
    fallback = REPO_ROOT / "webclient"
    return _configured_cwd(loaded, value, fallback=fallback)


def _run_capture(
    loaded: LoadedConfig,
    capture: Mapping[str, Any],
    run_dir: Path,
    manifest: dict[str, Any],
    index: int,
) -> tuple[Path, Path]:
    frame_id = str(capture.get("frame_id", index))
    name = str(capture.get("name", f"frame-{frame_id}"))
    frame_directory = (
        run_dir / "frames" / _safe_name(name, fallback=f"frame-{index}")
    )
    frame_directory.mkdir(parents=True, exist_ok=True)
    png_path = frame_directory / "candidate.webgl.png"
    metadata_path = frame_directory / "frame.json"
    log_path = frame_directory / "capture.log"
    node_cwd = _capture_node_cwd(loaded, capture)
    if not node_cwd.is_dir():
        raise ControllerError(f"capture Node cwd does not exist: {node_cwd}")

    command = _command(
        capture.get("node_command", loaded.data.get("node_command", ["node"])),
        f"capture {name!r} node_command",
    )
    command.extend(
        [
            str(CAPTURE_HELPER),
            "--url",
            str(capture["url"]),
            "--selector",
            str(capture.get("selector", "canvas")),
            "--output",
            str(png_path),
            "--metadata-output",
            str(metadata_path),
            "--frame-id",
            frame_id,
            "--width",
            str(CAPTURE_WIDTH),
            "--height",
            str(CAPTURE_HEIGHT),
            "--timeout-ms",
            str(int(float(capture.get("timeout_seconds", 30)) * 1000)),
            "--settle-frames",
            str(int(capture.get("settle_frames", 1))),
            "--browser",
            str(capture.get("browser", "chromium")),
        ]
    )
    if capture.get("wait_expression"):
        command.extend(["--wait-expression", str(capture["wait_expression"])])
    if capture.get("metadata_expression"):
        command.extend(
            ["--metadata-expression", str(capture["metadata_expression"])]
        )
    if capture.get("headful", False):
        command.append("--headful")
    for argument in capture.get("launch_args", []):
        command.extend(["--launch-arg", str(argument)])

    action: dict[str, Any] = {
        "command": command,
        "cwd": str(node_cwd),
        "env": dict(capture.get("env", {})),
        "frame_id": frame_id,
        "kind": "webgl-canvas-capture",
        "log": str(log_path.relative_to(run_dir)),
        "name": name,
        "started_at": _utc_now(),
        "status": "running",
    }
    manifest["actions"].append(action)
    _manifest_event(manifest, "capture_starting", frame_id=frame_id, name=name)
    _save_manifest(run_dir, manifest)

    creationflags = 0
    if os.name == "nt":
        creationflags = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    try:
        environment = os.environ.copy()
        environment.update(dict(capture.get("env", {})))
        popen = subprocess.Popen(
            command,
            cwd=node_cwd,
            env=environment,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            shell=False,
            creationflags=creationflags,
        )
    except OSError as error:
        action.update(
            {
                "error": str(error),
                "finished_at": _utc_now(),
                "status": "failed",
            }
        )
        _save_manifest(run_dir, manifest)
        raise ControllerError(f"could not start canvas capture {name!r}: {error}") from error

    action["pid"] = popen.pid
    _save_manifest(run_dir, manifest)
    timeout = float(capture.get("timeout_seconds", 30)) + 10
    try:
        stdout, stderr = popen.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        popen.terminate()
        try:
            stdout, stderr = popen.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            popen.kill()
            stdout, stderr = popen.communicate()
        action.update(
            {
                "error": f"capture exceeded {timeout:g}s controller timeout",
                "finished_at": _utc_now(),
                "return_code": popen.returncode,
                "status": "failed",
            }
        )
        log_path.write_text(
            f"stdout:\n{stdout}\n\nstderr:\n{stderr}",
            encoding="utf-8",
        )
        _save_manifest(run_dir, manifest)
        raise ControllerError(str(action["error"]))

    log_path.write_text(
        f"stdout:\n{stdout}\n\nstderr:\n{stderr}",
        encoding="utf-8",
    )
    action["return_code"] = popen.returncode
    action["finished_at"] = _utc_now()
    if popen.returncode != 0:
        action["status"] = "failed"
        _save_manifest(run_dir, manifest)
        raise ControllerError(
            f"canvas capture {name!r} exited with code {popen.returncode}; "
            f"see {log_path}"
        )

    if not png_path.is_file() or not metadata_path.is_file():
        action["status"] = "failed"
        _save_manifest(run_dir, manifest)
        raise ControllerError(
            f"canvas capture {name!r} did not create its PNG and frame metadata"
        )
    try:
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        validate_frame_record(metadata)
    except (json.JSONDecodeError, FrameSchemaError) as error:
        action["status"] = "failed"
        action["error"] = str(error)
        _save_manifest(run_dir, manifest)
        raise ControllerError(
            f"canvas capture {name!r} emitted invalid frame metadata: {error}"
        ) from error

    try:
        helper_result = json.loads(stdout.strip().splitlines()[-1])
    except (IndexError, json.JSONDecodeError):
        helper_result = {}
    action.update(
        {
            "capture": helper_result,
            "frame_metadata": str(metadata_path.relative_to(run_dir)),
            "output": str(png_path.relative_to(run_dir)),
            "status": "complete",
        }
    )
    _manifest_event(manifest, "capture_complete", frame_id=frame_id, name=name)
    _save_manifest(run_dir, manifest)
    return png_path, frame_directory


def _comparison_options(value: Mapping[str, Any]) -> dict[str, Any]:
    options = dict(value.get("options", {}))
    if "target_size" in options and isinstance(options["target_size"], str):
        options["target_size"] = frame_compare._parse_size(options["target_size"])
    return options


def _run_comparison(
    loaded: LoadedConfig,
    comparison: Mapping[str, Any],
    run_dir: Path,
    manifest: dict[str, Any],
    index: int,
    *,
    candidate_override: Path | None = None,
    output_override: Path | None = None,
) -> dict[str, Any]:
    frame_id = str(comparison.get("frame_id", index))
    name = str(comparison.get("name", f"frame-{frame_id}"))
    reference = _path_from(loaded.directory, str(comparison["reference_png"]))
    candidate = (
        candidate_override
        if candidate_override is not None
        else _path_from(loaded.directory, str(comparison["candidate_png"]))
    )
    output = output_override or (
        run_dir
        / "comparisons"
        / _safe_name(name, fallback=f"comparison-{index}")
    )
    action: dict[str, Any] = {
        "candidate": str(candidate),
        "frame_id": frame_id,
        "kind": "frame-comparison",
        "name": name,
        "output": str(output.relative_to(run_dir)),
        "reference": str(reference),
        "started_at": _utc_now(),
        "status": "running",
    }
    manifest["actions"].append(action)
    _manifest_event(manifest, "comparison_starting", frame_id=frame_id, name=name)
    _save_manifest(run_dir, manifest)
    try:
        report = frame_compare.compare_frame_pair(
            reference,
            candidate,
            output,
            frame_id=frame_id,
            **_comparison_options(comparison),
        )
    except (OSError, ValueError, UnidentifiedImageError) as error:
        action.update(
            {
                "error": str(error),
                "finished_at": _utc_now(),
                "status": "failed",
            }
        )
        _save_manifest(run_dir, manifest)
        raise ControllerError(f"comparison {name!r} failed: {error}") from error
    action.update(
        {
            "finished_at": _utc_now(),
            "metrics": report["metrics"],
            "report": str((output / "report.json").relative_to(run_dir)),
            "status": "complete",
        }
    )
    _manifest_event(manifest, "comparison_complete", frame_id=frame_id, name=name)
    _save_manifest(run_dir, manifest)
    return report


def run_controller(
    config_path: str | Path,
    *,
    run_dir: str | Path | None = None,
    run_root: str | Path | None = None,
) -> dict[str, Any]:
    """Run configured source-built components and always tear them down."""

    loaded = load_controller_config(config_path)
    actual_run_dir = _new_run_directory(
        loaded,
        run_dir=run_dir,
        run_root=run_root,
    )
    manifest: dict[str, Any] = {
        "actions": [],
        "config": loaded.data,
        "config_path": str(loaded.path),
        "errors": [],
        "events": [],
        "processes": [],
        "run_dir": str(actual_run_dir),
        "run_id": actual_run_dir.name,
        "schema": CONTROLLER_SCHEMA,
        "schema_version": CONTROLLER_SCHEMA_VERSION,
        "shutdown_order": [],
        "started_at": _utc_now(),
        "status": "starting",
    }
    _write_json(actual_run_dir / "config.resolved.json", loaded.data)
    _manifest_event(manifest, "run_created")
    _save_manifest(actual_run_dir, manifest)

    processes: list[ManagedProcess] = []
    failure: BaseException | None = None
    try:
        manifest["status"] = "starting-processes"
        _save_manifest(actual_run_dir, manifest)
        enabled_processes = [
            process
            for process in loaded.data.get("processes", [])
            if process.get("enabled", True) is not False
        ]
        for index, spec in enumerate(enabled_processes):
            managed = _start_process(
                loaded,
                spec,
                actual_run_dir,
                manifest,
                index,
            )
            processes.append(managed)
            rules = _readiness_rules(spec.get("readiness"), str(spec["name"]))
            _wait_for_process(managed, rules, actual_run_dir, manifest)

        manifest["status"] = "running-actions"
        _manifest_event(manifest, "all_processes_ready", count=len(processes))
        _save_manifest(actual_run_dir, manifest)

        for index, capture in enumerate(loaded.data.get("captures", [])):
            png_path, frame_directory = _run_capture(
                loaded,
                capture,
                actual_run_dir,
                manifest,
                index,
            )
            if capture.get("reference_png"):
                nested = dict(capture.get("comparison", {}))
                nested.update(
                    {
                        "frame_id": capture.get("frame_id", index),
                        "name": capture.get("name", f"frame-{index}"),
                        "reference_png": capture["reference_png"],
                    }
                )
                _run_comparison(
                    loaded,
                    nested,
                    actual_run_dir,
                    manifest,
                    index,
                    candidate_override=png_path,
                    output_override=frame_directory / "comparison",
                )

        for index, comparison in enumerate(
            loaded.data.get("comparisons", [])
        ):
            _run_comparison(
                loaded,
                comparison,
                actual_run_dir,
                manifest,
                index,
            )

        hold_seconds = float(loaded.data.get("hold_seconds", 0))
        hold_deadline = time.monotonic() + hold_seconds
        while time.monotonic() < hold_deadline:
            for process in processes:
                if process.popen.poll() is not None:
                    raise ControllerError(
                        f"process {process.record['name']!r} exited with code "
                        f"{process.popen.returncode} during hold"
                    )
            time.sleep(min(0.1, max(0.0, hold_deadline - time.monotonic())))
    except BaseException as error:
        failure = error
        manifest["errors"].append(
            {
                "at": _utc_now(),
                "message": str(error),
                "type": type(error).__name__,
            }
        )
        _manifest_event(
            manifest,
            "run_failed",
            error=str(error),
            error_type=type(error).__name__,
        )
    finally:
        manifest["status"] = "stopping"
        _save_manifest(actual_run_dir, manifest)
        for process in reversed(processes):
            _stop_process(process, actual_run_dir, manifest)
        manifest["finished_at"] = _utc_now()
        manifest["status"] = "failed" if failure is not None else "complete"
        _manifest_event(manifest, f"run_{manifest['status']}")
        _save_manifest(actual_run_dir, manifest)

    if failure is not None:
        if isinstance(failure, KeyboardInterrupt):
            raise failure
        raise RunFailed(str(failure), actual_run_dir) from failure
    return manifest


def _doctor_check(
    checks: list[dict[str, Any]],
    name: str,
    ok: bool,
    detail: str,
    *,
    required: bool = True,
) -> None:
    checks.append(
        {
            "detail": detail,
            "name": name,
            "ok": bool(ok),
            "required": required,
        }
    )


def _executable_location(
    command: Sequence[str],
    cwd: Path,
    environment: Mapping[str, str],
) -> Path | None:
    executable = command[0]
    if Path(executable).is_absolute() or "/" in executable or "\\" in executable:
        path = Path(executable)
        if not path.is_absolute():
            path = cwd / path
        return path.resolve() if path.is_file() else None
    found = shutil.which(executable, path=environment.get("PATH"))
    return Path(found).resolve() if found else None


def doctor(config_path: str | Path | None = None) -> dict[str, Any]:
    """Perform read-only configuration and dependency checks."""

    checks: list[dict[str, Any]] = []
    _doctor_check(
        checks,
        "python",
        sys.version_info >= (3, 10),
        f"{sys.version.split()[0]} at {Path(sys.executable).resolve()}",
    )
    try:
        import PIL

        pillow_detail = f"Pillow {PIL.__version__}"
        pillow_ok = True
    except ImportError as error:
        pillow_detail = str(error)
        pillow_ok = False
    _doctor_check(checks, "pillow", pillow_ok, pillow_detail)
    _doctor_check(
        checks,
        "capture-helper",
        CAPTURE_HELPER.is_file(),
        str(CAPTURE_HELPER),
    )
    _doctor_check(
        checks,
        "frame-schema",
        FRAME_SCHEMA_PATH.is_file(),
        str(FRAME_SCHEMA_PATH),
    )

    loaded: LoadedConfig | None = None
    if config_path is not None:
        try:
            loaded = load_controller_config(config_path)
            _doctor_check(checks, "config", True, str(loaded.path))
        except ConfigError as error:
            _doctor_check(checks, "config", False, str(error))

    if loaded is not None:
        for process in loaded.data.get("processes", []):
            if process.get("enabled", True) is False:
                continue
            name = str(process["name"])
            cwd = _configured_cwd(loaded, process.get("cwd"))
            explicit_env = dict(process.get("env", {}))
            environment = os.environ.copy()
            environment.update(explicit_env)
            command = _command(process["command"], f"process {name!r} command")
            location = _executable_location(command, cwd, environment)
            _doctor_check(
                checks,
                f"process:{name}",
                cwd.is_dir() and location is not None,
                (
                    f"command={location or command[0]}; cwd={cwd}"
                    if cwd.is_dir()
                    else f"cwd does not exist: {cwd}"
                ),
            )

        if loaded.data.get("captures"):
            representative = loaded.data["captures"][0]
            node_command = _command(
                representative.get(
                    "node_command",
                    loaded.data.get("node_command", ["node"]),
                ),
                "node_command",
            )
            node_cwd = _capture_node_cwd(loaded, representative)
            capture_environment = os.environ.copy()
            capture_environment.update(dict(representative.get("env", {})))
            _doctor_check(
                checks,
                "node-cwd",
                node_cwd.is_dir(),
                str(node_cwd),
            )
            node_location = _executable_location(
                node_command,
                node_cwd,
                capture_environment,
            )
            _doctor_check(
                checks,
                "node",
                node_location is not None,
                str(node_location or node_command[0]),
            )
            if node_location is not None and node_cwd.is_dir():
                browser_name = json.dumps(
                    str(representative.get("browser", "chromium"))
                )
                probe = (
                    "const {createRequire}=require('node:module');"
                    "const fs=require('node:fs');"
                    "const path=require('node:path');"
                    "const {pathToFileURL}=require('node:url');"
                    "const r=createRequire(path.join(process.cwd(),'package.json'));"
                    f"const browserName={browser_name};"
                    "(async()=>{"
                    "const modulePath=r.resolve('playwright');"
                    "const playwright=r('playwright');"
                    "const browser=playwright[browserName];"
                    "if(!browser)throw new Error(`unknown browser ${browserName}`);"
                    "let launch={};"
                    "if(browserName==='chromium'){try{"
                    "const helper=await import(pathToFileURL("
                    "path.join(process.cwd(),'tools','playwright_portable_browser.mjs')"
                    ").href);launch=helper.chromiumLaunchOptions({});"
                    "}catch(error){if(error.code!=='ERR_MODULE_NOT_FOUND')throw error;}}"
                    "const executable=launch.executablePath||browser.executablePath();"
                    "const result={module:modulePath,executable,"
                    "browser_exists:fs.existsSync(executable)};"
                    "process.stdout.write(JSON.stringify(result));"
                    "if(!result.browser_exists)process.exitCode=3;"
                    "})().catch(error=>{console.error(error);process.exitCode=2;});"
                )
                try:
                    result = subprocess.run(
                        [*node_command, "-e", probe],
                        cwd=node_cwd,
                        env=capture_environment,
                        check=False,
                        capture_output=True,
                        text=True,
                        timeout=10,
                    )
                    playwright_ok = result.returncode in {0, 3}
                    playwright_detail = result.stdout.strip() or (
                        result.stderr.strip() or f"exit {result.returncode}"
                    )
                except (OSError, subprocess.TimeoutExpired) as error:
                    playwright_ok = False
                    playwright_detail = str(error)
                _doctor_check(
                    checks,
                    "playwright",
                    playwright_ok,
                    playwright_detail,
                )
                browser_ok = False
                browser_detail = playwright_detail
                if playwright_ok:
                    try:
                        browser_probe = json.loads(result.stdout)
                        browser_ok = bool(browser_probe["browser_exists"])
                        browser_detail = str(browser_probe["executable"])
                    except (json.JSONDecodeError, KeyError, TypeError):
                        browser_ok = False
                _doctor_check(
                    checks,
                    "playwright-browser",
                    browser_ok,
                    browser_detail,
                )

    required_checks = [check for check in checks if check["required"]]
    return {
        "checks": checks,
        "config": str(loaded.path) if loaded is not None else None,
        "ok": all(check["ok"] for check in required_checks),
        "schema": "openwyd.compare-doctor",
        "schema_version": 1,
    }


def _print_json(value: Any, stream: Any = sys.stdout) -> None:
    json.dump(
        value,
        stream,
        ensure_ascii=False,
        indent=2,
        sort_keys=True,
        allow_nan=False,
    )
    stream.write("\n")


def _global_help() -> str:
    return """usage: python -m tools.openwyd_compare COMMAND [options]

Source-driven OpenWyd client/server/WebGL comparison controller.

commands:
  doctor   validate config, commands, Node/Playwright and Python dependencies
  run      start configured processes, capture/compare, then stop in reverse
  compare  run the deterministic paired-PNG comparator
  report-paired
           turn paired-run.json into a complete multi-frame report

Compatibility: the historical positional form
  python -m tools.openwyd_compare reference.png candidate.png [options]
is treated as `compare`.
"""


def main(argv: Sequence[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    if not arguments or arguments[0] in {"-h", "--help"}:
        sys.stdout.write(_global_help())
        return 0

    command = arguments[0]
    if command == "compare":
        return frame_compare.main(arguments[1:])
    if command == "report-paired":
        return paired_report.main(arguments[1:])
    if command not in {"doctor", "run"}:
        # Preserve the original CLI while scripts migrate to the explicit
        # `compare` subcommand.
        return frame_compare.main(arguments)

    parser = argparse.ArgumentParser(
        prog=f"python -m tools.openwyd_compare {command}"
    )
    if command == "doctor":
        parser.description = "Check controller config and local dependencies."
        parser.add_argument("--config", type=Path)
        parsed = parser.parse_args(arguments[1:])
        report = doctor(parsed.config)
        _print_json(report)
        return 0 if report["ok"] else 1

    parser.description = (
        "Run configured source-built components, captures and comparisons."
    )
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument(
        "--run-dir",
        type=Path,
        help="exact empty output directory (default: unique ignored artifact dir)",
    )
    parsed = parser.parse_args(arguments[1:])
    try:
        report = run_controller(parsed.config, run_dir=parsed.run_dir)
    except RunFailed as error:
        _print_json(
            {
                "error": str(error),
                "run_dir": str(error.run_dir),
                "status": "failed",
            },
            sys.stderr,
        )
        return 1
    except (ConfigError, ControllerError, OSError) as error:
        parser.exit(2, f"error: {error}\n")
    _print_json(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
