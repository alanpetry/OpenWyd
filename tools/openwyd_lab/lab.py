#!/usr/bin/env python3
"""Fast deterministic OpenWyd native/WASM scenario launcher."""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from PIL import Image, ImageChops


MAGIC = 0x424C574F
VERSION = 1
HEADER = struct.Struct("<IHHIHHIIIIffffHH")
ACTOR = struct.Struct("<HhhBBH16s18H18B h 6i 4h")
EVENT = struct.Struct("<IHH4i24B")
KINDS = {"field": 1, "isolated": 2, "ui": 3, "scene": 3}
EVENT_KINDS = {
    "create_mob": 1,
    "action": 2,
    "motion": 3,
    "attack": 4,
    "teleport": 5,
}
PIXEL_RMS_LIMIT = 12.0
PIXEL_STRONG_PERCENT_LIMIT = 3.5


def _atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    temporary.write_bytes(data)
    deadline = time.monotonic() + 2.0
    while True:
        try:
            os.replace(temporary, path)
            return
        except PermissionError:
            if os.name != "nt" or time.monotonic() >= deadline:
                temporary.unlink(missing_ok=True)
                raise
            time.sleep(0.01)


def _atomic_json(path: Path, value: object) -> None:
    _atomic_write(
        path,
        (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )


def _compare_pixels(native_path: Path, wasm_path: Path, diff_path: Path) -> dict[str, Any]:
    with Image.open(native_path) as native_source, Image.open(wasm_path) as wasm_source:
        native = native_source.convert("RGB")
        wasm = wasm_source.convert("RGB")
    if native.size != wasm.size:
        raise ValueError(
            f"cannot compare different image sizes: native={native.size}, wasm={wasm.size}"
        )

    difference = ImageChops.difference(native, wasm)
    histogram = difference.histogram()
    pixel_count = native.width * native.height
    channel_count = pixel_count * 3
    absolute_sum = sum((index % 256) * count for index, count in enumerate(histogram))
    squared_sum = sum(
        (index % 256) ** 2 * count for index, count in enumerate(histogram)
    )
    different = 0
    over_8 = 0
    over_16 = 0
    over_32 = 0
    strong_left = native.width
    strong_top = native.height
    strong_right = -1
    strong_bottom = -1
    for index, channels in enumerate(difference.getdata()):
        maximum = max(channels)
        different += maximum > 0
        over_8 += maximum > 8
        over_16 += maximum > 16
        if maximum > 32:
            over_32 += 1
            x = index % native.width
            y = index // native.width
            strong_left = min(strong_left, x)
            strong_top = min(strong_top, y)
            strong_right = max(strong_right, x)
            strong_bottom = max(strong_bottom, y)

    rms = (squared_sum / channel_count) ** 0.5
    strong_percent = 100.0 * over_32 / pixel_count
    amplified = difference.point(lambda value: min(255, value * 4))
    amplified.save(diff_path, format="PNG")
    strong_bounds = (
        {
            "left": strong_left,
            "top": strong_top,
            "right": strong_right,
            "bottom": strong_bottom,
        }
        if over_32
        else None
    )
    passed = (
        rms <= PIXEL_RMS_LIMIT
        and strong_percent <= PIXEL_STRONG_PERCENT_LIMIT
    )
    return {
        "status": "pass" if passed else "review",
        "rms": round(rms, 4),
        "mean_absolute_error": round(absolute_sum / channel_count, 4),
        "max_absolute_error": max(
            index % 256 for index, count in enumerate(histogram) if count
        ),
        "pixels_different_percent": round(100.0 * different / pixel_count, 4),
        "pixels_over_8_percent": round(100.0 * over_8 / pixel_count, 4),
        "pixels_over_16_percent": round(100.0 * over_16 / pixel_count, 4),
        "pixels_over_32_percent": round(strong_percent, 4),
        "strong_difference_bounds": strong_bounds,
        "thresholds": {
            "rms_max": PIXEL_RMS_LIMIT,
            "pixels_over_32_percent_max": PIXEL_STRONG_PERCENT_LIMIT,
        },
    }


def _u32_hex(value: int) -> str:
    return f"{value & 0xFFFFFFFF:08x}"


def _fnv1a(data: bytes, seed: int = 2166136261) -> int:
    value = seed
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def _fixed(values: list[int], count: int) -> list[int]:
    return (values + [0] * count)[:count]


def _actor_bytes(value: dict[str, Any]) -> bytes:
    score = value.get("score", {})
    equip = _fixed([int(item) for item in value.get("equip", [])], 18)
    equip2 = _fixed([int(item) for item in value.get("equip2", [])], 18)
    name = str(value.get("name", "OpenWYD")).encode("cp1252")[:15]
    name = name + bytes(16 - len(name))
    return ACTOR.pack(
        int(value.get("id", 1)),
        int(value.get("x", 2096)),
        int(value.get("y", 2092)),
        int(value.get("class", 0)),
        int(value.get("guild_level", 0)),
        int(value.get("guild", 0)),
        name,
        *equip,
        *equip2,
        int(score.get("level", 0)),
        int(score.get("ac", 12)),
        int(score.get("damage", 18)),
        int(score.get("max_hp", 320)),
        int(score.get("max_mp", 140)),
        int(score.get("hp", score.get("max_hp", 320))),
        int(score.get("mp", score.get("max_mp", 140))),
        int(score.get("str", 12)),
        int(score.get("int", 10)),
        int(score.get("dex", 12)),
        int(score.get("con", 10)),
    )


def _event_bytes(value: dict[str, Any]) -> bytes:
    kind = str(value["kind"])
    route = value.get("route")
    if route is not None:
        data = list(str(route).encode("ascii"))
    else:
        data = [int(item) & 0xFF for item in value.get("data", [])]
    if kind == "attack":
        data = [int(value.get("target_actor", 0)) & 0xFF, *data]
    if kind == "action":
        fields = (
            int(value.get("effect", 0)),
            int(value.get("x", 0)),
            int(value.get("y", 0)),
            int(value.get("speed", value.get("c", 6))),
        )
    else:
        fields = (
            int(value.get("a", value.get("motion", 0))),
            int(value.get("b", value.get("x", 0))),
            int(value.get("c", value.get("y", 0))),
            int(value.get("d", value.get("skill", -1))),
        )
    return EVENT.pack(
        int(value.get("frame", 0)),
        EVENT_KINDS[kind],
        int(value.get("actor", 0)),
        *fields,
        *_fixed(data, 24),
    )


def compile_scenario(source: Path, destination: Path) -> dict[str, Any]:
    value = json.loads(source.read_text(encoding="utf-8"))
    actors = [_actor_bytes(item) for item in value["actors"]]
    events = [_event_bytes(item) for item in value.get("timeline", [])]
    mode = str(value.get("mode", "field"))
    kind = KINDS.get(mode)
    if kind is None:
        raise ValueError(f"unsupported Lab mode: {mode}")
    camera = value.get("camera", {})
    clear = int(str(value.get("clear_color", "0xff000000")), 0)
    total_size = HEADER.size + sum(map(len, actors)) + sum(map(len, events))
    header = HEADER.pack(
        MAGIC,
        VERSION,
        HEADER.size,
        total_size,
        kind,
        int(value.get("flags", 0)),
        int(value.get("seed", 769)),
        int(value.get("start_time_ms", 10000)),
        int(value.get("tick_ms", 50)),
        clear,
        float(camera.get("horizon", 0.78539818)),
        float(camera.get("vertical", -0.78539818)),
        float(camera.get("length", 5.5)),
        float(camera.get("height", 0.4)),
        len(actors),
        len(events),
    )
    binary = b"".join([header, *actors, *events])
    if len(binary) != total_size:
        raise AssertionError("OWLB compiler size mismatch")
    _atomic_write(destination, binary)
    return {
        "schema": "openwyd-lab-scenario",
        "version": VERSION,
        "source": str(source),
        "binary": str(destination),
        "bytes": len(binary),
        "fnv1a": _u32_hex(_fnv1a(binary)),
        "sha256": hashlib.sha256(binary).hexdigest(),
        "actors": len(actors),
        "events": len(events),
        "mode": mode,
    }


def _process_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    if os.name != "nt":
        try:
            os.kill(pid, 0)
            return True
        except OSError:
            return False
    process = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)
    if not process:
        return False
    exit_code = ctypes.c_ulong()
    ok = ctypes.windll.kernel32.GetExitCodeProcess(
        process, ctypes.byref(exit_code)
    )
    ctypes.windll.kernel32.CloseHandle(process)
    return bool(ok and exit_code.value == 259)


def _process_path(pid: int) -> Path | None:
    if pid <= 0 or os.name != "nt":
        return None
    process = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)
    if not process:
        return None
    try:
        buffer = ctypes.create_unicode_buffer(32768)
        length = ctypes.c_ulong(len(buffer))
        ok = ctypes.windll.kernel32.QueryFullProcessImageNameW(
            process, 0, buffer, ctypes.byref(length)
        )
        return Path(buffer.value).resolve() if ok else None
    finally:
        ctypes.windll.kernel32.CloseHandle(process)


def _process_matches(pid: int, executable: Path) -> bool:
    if not _process_alive(pid):
        return False
    if os.name != "nt":
        return True
    actual = _process_path(pid)
    return (
        actual is not None
        and os.path.normcase(str(actual))
        == os.path.normcase(str(executable.resolve()))
    )


def _json_pid(path: Path) -> int:
    try:
        return int(json.loads(path.read_text(encoding="utf-8")).get("pid", 0))
    except (OSError, ValueError, json.JSONDecodeError):
        return 0


def _read_pid(path: Path) -> int:
    try:
        return int(path.read_text(encoding="ascii").strip())
    except (OSError, ValueError):
        return 0


def _png_dimensions(path: Path) -> tuple[int, int] | None:
    try:
        header = path.read_bytes()[:24]
    except OSError:
        return None
    if (
        len(header) != 24
        or header[:8] != b"\x89PNG\r\n\x1a\n"
        or header[12:16] != b"IHDR"
    ):
        return None
    return (
        int.from_bytes(header[16:20], "big"),
        int.from_bytes(header[20:24], "big"),
    )


def _start_detached(
    command: list[str],
    *,
    cwd: Path,
    env: dict[str, str] | None,
    stdout_path: Path,
) -> int:
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    stream = stdout_path.open("ab", buffering=0)
    creationflags = 0
    if os.name == "nt":
        creationflags = (
            subprocess.CREATE_NEW_PROCESS_GROUP
            | subprocess.DETACHED_PROCESS
        )
    process = subprocess.Popen(
        command,
        cwd=cwd,
        env=env,
        stdin=subprocess.DEVNULL,
        stdout=stream,
        stderr=subprocess.STDOUT,
        creationflags=creationflags,
        close_fds=True,
    )
    stream.close()
    return process.pid


class Lab:
    def __init__(self, repo_root: Path) -> None:
        self.repo = repo_root.resolve()
        self.tools = self.repo / "tools" / "openwyd_lab"
        self.scenarios = self.tools / "scenarios"
        self.artifacts = self.repo / "artifacts" / "openwyd_lab"
        self.runtime = self.artifacts / "runtime"
        self.native_exe = (
            self.repo
            / "artifacts/native-build/TMProject/Debug-Lab/bin/WYD.exe"
        )
        self.web_root = self.repo
        self.wasm_js = (
            self.repo
            / "webclient/client-wasm/build/link/tmproject_startup.js"
        )
        self.native_pid = self.runtime / "native.pid"
        self.wasm_pid = self.runtime / "wasm-host.pid"

    def next_generation(self) -> int:
        counter = self.runtime / "generation.txt"
        candidates = [_read_pid(counter)]
        for request in (
            self.runtime / "native-request.txt",
            self.runtime / "wasm-request.json",
            self.runtime / "native-response.txt",
            self.runtime / "wasm-response.json",
        ):
            try:
                text = request.read_text(encoding="utf-8")
                if request.suffix == ".json":
                    candidates.append(int(json.loads(text).get("generation", 0)))
                else:
                    for line in text.splitlines():
                        if line.startswith("generation="):
                            candidates.append(int(line.partition("=")[2]))
            except (OSError, ValueError, json.JSONDecodeError):
                pass
        generation = max(candidates, default=0) + 1
        if generation >= 0xFFFFFFFF:
            raise RuntimeError("Lab generation counter exhausted; restart runtimes")
        _atomic_write(counter, f"{generation}\n".encode("ascii"))
        return generation

    def build(self) -> None:
        if (
            _process_matches(_read_pid(self.native_pid), self.native_exe)
            or _process_alive(_read_pid(self.wasm_pid))
        ):
            print("Stopping Lab runtimes so the incremental linker can replace binaries")
            self.stop()
        subprocess.run(
            [
                "powershell.exe",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(self.repo / "tools/build_windows_source.ps1"),
                "-OpenWydLab",
            ],
            cwd=self.repo,
            check=True,
        )
        subprocess.run(
            [
                sys.executable,
                str(
                    self.repo
                    / "webclient/client-wasm/tools/build_wasm_dev.py"
                ),
                "dev",
                "--repo-root",
                str(self.repo),
            ],
            cwd=self.repo,
            check=True,
        )

    def ensure_binaries(self) -> None:
        missing = [
            path for path in (self.native_exe, self.wasm_js)
            if not path.is_file()
        ]
        if missing:
            formatted = "\n".join(f"  {path}" for path in missing)
            raise RuntimeError(
                "Lab binaries are missing. Run '.\\tools\\lab.ps1 build'.\n"
                + formatted
            )

    def start(self) -> None:
        self.ensure_binaries()
        self.runtime.mkdir(parents=True, exist_ok=True)
        native = _read_pid(self.native_pid)
        if not _process_matches(native, self.native_exe):
            # A previous `stop` leaves a monotonic quit command on disk.  A
            # freshly started client must wait for a new command rather than
            # consume that stale request and immediately exit.
            for stale in (
                self.runtime / "native-request.txt",
                self.runtime / "native-response.txt",
            ):
                stale.unlink(missing_ok=True)
            environment = os.environ.copy()
            environment["OPENWYD_LAB_CONTROL_DIR"] = str(self.runtime)
            environment["OPENWYD_LAB_ASSET_DIR"] = str(
                self.repo / "v769ClientRelease"
            )
            native = _start_detached(
                [str(self.native_exe)],
                cwd=self.repo / "v769ClientRelease",
                env=environment,
                stdout_path=self.runtime / "native.log",
            )
            _atomic_write(self.native_pid, f"{native}\n".encode("ascii"))

        wasm = _read_pid(self.wasm_pid)
        wasm_ready = self.runtime / "wasm-ready.json"
        if not (_process_alive(wasm) and _json_pid(wasm_ready) == wasm):
            for stale in (
                self.runtime / "wasm-ready.json",
                self.runtime / "wasm-request.json",
                self.runtime / "wasm-response.json",
            ):
                stale.unlink(missing_ok=True)
            node = shutil.which("node.exe") or shutil.which("node")
            if not node:
                candidates = sorted(
                    (self.repo.parent / ".tools" / "emsdk" / "node").glob(
                        "*/bin/node.exe"
                    ),
                    reverse=True,
                )
                node = str(candidates[0]) if candidates else None
            if not node:
                raise RuntimeError("Node.js was not found")
            wasm = _start_detached(
                [
                    node,
                    str(self.tools / "wasm_lab_host.mjs"),
                    "--repo-root",
                    str(self.repo),
                    "--control-dir",
                    str(self.runtime),
                ],
                cwd=self.repo,
                env=os.environ.copy(),
                stdout_path=self.runtime / "wasm-host.log",
            )
            _atomic_write(self.wasm_pid, f"{wasm}\n".encode("ascii"))
        self._wait_json(
            wasm_ready,
            lambda value: (
                value.get("status") == "ready"
                and value.get("pid") == wasm
            ),
            timeout=120,
            process_pid=wasm,
        )
        print(f"OpenWyd Lab ready: native PID {native}, WASM host PID {wasm}")

    def _wait_json(
        self,
        path: Path,
        predicate: Any,
        *,
        timeout: float,
        process_pid: int = 0,
    ) -> dict[str, Any]:
        deadline = time.monotonic() + timeout
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            if process_pid and not _process_alive(process_pid):
                raise RuntimeError(f"Lab process {process_pid} exited; see runtime logs")
            try:
                value = json.loads(path.read_text(encoding="utf-8"))
                if predicate(value):
                    return value
            except (OSError, json.JSONDecodeError) as error:
                last_error = error
            time.sleep(0.025)
        raise TimeoutError(f"timed out waiting for {path}: {last_error}")

    def _wait_native(
        self,
        generation: int,
        *,
        timeout: float,
    ) -> dict[str, str]:
        path = self.runtime / "native-response.txt"
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                result: dict[str, str] = {}
                for line in path.read_text(
                    encoding="utf-8", errors="replace"
                ).splitlines():
                    key, separator, value = line.partition("=")
                    if separator:
                        result[key] = value
                if (
                    int(result.get("generation", "0")) == generation
                    and result.get("status") not in {"accepted", "running"}
                ):
                    return result
            except (OSError, ValueError):
                pass
            if not _process_matches(
                _read_pid(self.native_pid), self.native_exe
            ):
                raise RuntimeError("native Lab client exited; see native.log")
            time.sleep(0.025)
        raise TimeoutError("native Lab capture timed out")

    def show(self, scenario_name: str, frame: int) -> Path:
        self.start()
        source = self.scenarios / f"{scenario_name}.json"
        if not source.is_file():
            raise FileNotFoundError(f"unknown scenario: {scenario_name}")
        generation = self.next_generation()
        run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        output = self.artifacts / scenario_name / run_id
        output.mkdir(parents=True, exist_ok=False)
        compiled = compile_scenario(source, output / "scenario.owlb")
        shutil.copy2(source, output / "scenario.json")
        native_png = output / "native.png"
        wasm_png = output / "wasm.png"
        diff_png = output / "diff.png"

        native_request = (
            f"generation={generation}\n"
            f"scenario={output / 'scenario.owlb'}\n"
            f"frame={frame}\n"
            f"capture={native_png}\n"
        ).encode("utf-8")
        _atomic_write(self.runtime / "native-request.txt", native_request)
        _atomic_json(
            self.runtime / "wasm-request.json",
            {
                "generation": generation,
                "command": "show",
                "scenario": str(output / "scenario.owlb"),
                "frame": frame,
                "capture": str(wasm_png),
            },
        )
        native = self._wait_native(generation, timeout=120)
        wasm = self._wait_json(
            self.runtime / "wasm-response.json",
            lambda value: (
                value.get("generation") == generation
                and value.get("status") not in {"accepted", "running"}
            ),
            timeout=120,
            process_pid=_read_pid(self.wasm_pid),
        )
        failures = []
        if native.get("status") != "complete":
            failures.append(f"native={native}")
        if wasm.get("status") != "complete":
            failures.append(f"wasm={wasm}")
        for image in (native_png, wasm_png):
            if not image.is_file() or image.stat().st_size < 100:
                failures.append(f"missing PNG: {image}")
        if native.get("scenario_hash") != wasm.get("scenario_hash"):
            failures.append("scenario hashes differ")
        if native.get("packet_hash") != wasm.get("packet_hash"):
            failures.append("packet hashes differ")
        if native.get("scenario_hash") != compiled["fnv1a"]:
            failures.append("native scenario hash differs from compiled bytes")
        for label, image in (("native", native_png), ("wasm", wasm_png)):
            dimensions = _png_dimensions(image)
            if dimensions != (800, 600):
                failures.append(f"{label} PNG is {dimensions}, expected 800x600")
        for label, value in (
            ("native screen", (native.get("screen_width"), native.get("screen_height"))),
            ("wasm screen", (wasm.get("screen_width"), wasm.get("screen_height"))),
        ):
            if tuple(map(int, value)) != (800, 600):
                failures.append(f"{label} is {value}, expected 800x600")
        for key in (
            "frame",
            "clock_ms",
            "scene_type",
            "player_visible",
            "player_hidden",
            "player_has_skin",
            "player_familiar_item",
            "player_has_familiar",
            "player_familiar_visible",
            "player_familiar_has_skin",
            "player_familiar_visibility_reason",
            "player_class",
            "player_motion",
            "player_skin_type",
            "player_moving",
            "player_last_route",
            "player_max_route",
            "player_move_started_ms",
            "player_animation_started_ms",
            "player_animation_index",
            "player_animation_last_index",
            "player_skin_fps",
            "player_skin_offset",
            "player_skin_start_offset",
            "player_skin_tick_last",
            "player_skin_animation_base",
            "player_pose_hash",
        ):
            if key.endswith("_hash"):
                values_match = str(native.get(key, "")) == str(wasm.get(key, "!"))
            else:
                values_match = int(native.get(key, -1)) == int(wasm.get(key, -2))
            if not values_match:
                failures.append(
                    f"{key} differs: native={native.get(key)} wasm={wasm.get(key)}"
                )
        if int(native.get("frame", -1)) != frame:
            failures.append(
                f"captured frame {native.get('frame')}, expected {frame}"
            )
        for key in (
            "player_x",
            "player_y",
            "player_height",
            "player_speed",
            "player_progress",
            "render_fps",
            "camera_x",
            "camera_y",
            "camera_z",
            "camera_horizon",
            "camera_vertical",
            "camera_length",
            "camera_height",
        ):
            if abs(float(native.get(key, "nan")) - float(wasm.get(key, "nan"))) > 1e-4:
                failures.append(
                    f"{key} differs: native={native.get(key)} wasm={wasm.get(key)}"
                )
        if int(wasm.get("gl_error_total", -1)) != 0:
            failures.append(
                f"glErrorTotal={wasm.get('gl_error_total')}, expected 0"
            )
        pixel_check = None
        if native_png.is_file() and wasm_png.is_file():
            try:
                pixel_check = _compare_pixels(native_png, wasm_png, diff_png)
            except (OSError, ValueError) as error:
                failures.append(f"pixel comparison failed: {error}")
        manifest = {
            "schema": "openwyd-lab-capture",
            "version": 1,
            "scenario": scenario_name,
            "frame": frame,
            "generation": generation,
            "compiled": compiled,
            "native": native,
            "wasm": wasm,
            "images": {
                "native": str(native_png),
                "wasm": str(wasm_png),
                "diff": str(diff_png),
            },
            "pixel_check": pixel_check,
            "valid": not failures,
            "failures": failures,
        }
        _atomic_json(output / "manifest.json", manifest)
        latest = self.artifacts / scenario_name / "latest.txt"
        _atomic_write(latest, f"{output}\n".encode("utf-8"))
        if failures:
            raise RuntimeError("; ".join(failures))
        print(
            f"{scenario_name} frame {frame}: "
            f"packet={native.get('packet_hash')} glErrorTotal="
            f"{wasm.get('gl_error_total')} pixel="
            f"{pixel_check.get('status', 'error').upper() if pixel_check else 'ERROR'} "
            f"rms={pixel_check.get('rms', 'n/a') if pixel_check else 'n/a'} "
            f"strong={pixel_check.get('pixels_over_32_percent', 'n/a') if pixel_check else 'n/a'}% "
            f"-> {output}"
        )
        return output

    def status(self) -> None:
        native = _read_pid(self.native_pid)
        wasm = _read_pid(self.wasm_pid)
        native_valid = _process_matches(native, self.native_exe)
        wasm_valid = (
            _process_alive(wasm)
            and _json_pid(self.runtime / "wasm-ready.json") == wasm
        )
        print(
            json.dumps(
                {
                    "native": {"pid": native, "alive": native_valid},
                    "wasm": {"pid": wasm, "alive": wasm_valid},
                    "runtime": str(self.runtime),
                },
                indent=2,
            )
        )

    def stop(self) -> None:
        generation = self.next_generation()
        native = _read_pid(self.native_pid)
        wasm = _read_pid(self.wasm_pid)
        if _process_matches(native, self.native_exe):
            _atomic_write(
                self.runtime / "native-request.txt",
                f"generation={generation}\nquit=1\n".encode("utf-8"),
            )
        if (
            _process_alive(wasm)
            and _json_pid(self.runtime / "wasm-ready.json") == wasm
        ):
            _atomic_json(
                self.runtime / "wasm-request.json",
                {"generation": generation, "command": "quit"},
            )
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            native_running = _process_matches(native, self.native_exe)
            wasm_running = (
                _process_alive(wasm)
                and _json_pid(self.runtime / "wasm-ready.json") == wasm
            )
            if not native_running and not wasm_running:
                break
            time.sleep(0.1)
        self.native_pid.unlink(missing_ok=True)
        self.wasm_pid.unlink(missing_ok=True)
        print("OpenWyd Lab stopped")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("build")
    commands.add_parser("start")
    commands.add_parser("stop")
    commands.add_parser("status")
    listing = commands.add_parser("list")
    listing.set_defaults(command="list")
    show = commands.add_parser("show")
    show.add_argument("scenario")
    show.add_argument("--frame", type=int, default=0)
    hot = commands.add_parser("hot-swap-test")
    hot.add_argument("--count", type=int, default=10)
    args = parser.parse_args()
    lab = Lab(args.repo_root)
    if args.command == "build":
        lab.build()
    elif args.command == "start":
        lab.start()
    elif args.command == "stop":
        lab.stop()
    elif args.command == "status":
        lab.status()
    elif args.command == "list":
        for path in sorted(lab.scenarios.glob("*.json")):
            print(path.stem)
    elif args.command == "show":
        if args.frame < 0:
            raise ValueError("--frame must be non-negative")
        lab.show(args.scenario, args.frame)
    elif args.command == "hot-swap-test":
        cases = [
            ("field_idle", 1),
            ("field_mounted", 1),
            ("field_mob", 1),
            ("field_move", 21),
            ("field_attack", 8),
            ("field_teleport", 6),
            ("isolated_human_attack", 18),
        ]
        for index in range(args.count):
            name, frame = cases[index % len(cases)]
            output = lab.show(name, frame)
            manifest = json.loads(
                (output / "manifest.json").read_text(encoding="utf-8")
            )
            if manifest["pixel_check"]["status"] != "pass":
                raise RuntimeError(
                    f"pixel check requested review for {name} frame {frame}"
                )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"OpenWyd Lab failed: {error}", file=sys.stderr)
        raise
