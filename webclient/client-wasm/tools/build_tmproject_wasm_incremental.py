#!/usr/bin/env python3
"""Fast dependency-file based compiler for the TMProject WASM objects.

This is the day-to-day builder.  It deliberately uses normal compiler
dependencies and timestamps instead of hashing the whole source and SDK tree.
The older content contract remains available to explicit verification builds.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Sequence

from tmproject_wasm_object_contract import (
    compile_arguments,
    object_for_source,
    parse_vcxproj_sources,
)


VALID_OPT_LEVELS = {"O0", "O1", "O2", "O3", "Os", "Oz"}
PCH_EXCLUDED_SOURCES = {"D3DEnumeration.cpp", "DXUtil.cpp"}
INCREMENTAL_SCHEMA_VERSION = 1


@dataclass
class CompileResult:
    source: str
    object: str
    action: str
    reason: str
    ok: bool
    returncode: int
    elapsed_ms: int
    stderr_log: str | None


def _atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.",
        suffix=".tmp",
        dir=path.parent,
        text=True,
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(
            descriptor,
            "w",
            encoding="utf-8",
            newline="\n",
        ) as stream:
            stream.write(text)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _atomic_write_json(path: Path, value: object) -> None:
    _atomic_write_text(
        path,
        json.dumps(value, indent=2, sort_keys=True) + "\n",
    )


def _read_json(path: Path) -> object | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def dependency_path(object_path: Path) -> Path:
    return object_path.with_suffix(".d")


def command_path(object_path: Path) -> Path:
    return object_path.with_suffix(".command.json")


def parse_depfile(path: Path) -> list[Path]:
    """Parse the simple make depfiles emitted by clang on this Windows tree."""

    try:
        value = path.read_text(encoding="utf-8")
    except OSError:
        return []
    value = value.replace("\\\r\n", " ").replace("\\\n", " ")
    separator = value.find(": ")
    if separator < 0:
        return []
    dependencies = []
    for token in value[separator + 2 :].split():
        token = token.replace("\\ ", " ").strip()
        if token:
            dependencies.append(Path(token))
    return dependencies


def _tool_signature(command: str) -> dict[str, object]:
    path = Path(command)
    if path.is_file():
        stat = path.stat()
        return {
            "path": str(path.resolve()),
            "size": stat.st_size,
            "mtime_ns": stat.st_mtime_ns,
        }
    return {"path": command}


def compile_signature(
    repo_root: Path,
    optimization_flag: str,
    *,
    use_pch: bool,
    pch_path: Path | None,
) -> dict[str, object]:
    arguments = compile_arguments(repo_root, optimization_flag)
    return {
        "schema_version": INCREMENTAL_SCHEMA_VERSION,
        "compiler": _tool_signature(arguments[0]),
        "arguments": arguments[1:],
        "use_pch": use_pch,
        "pch": str(pch_path.resolve()) if use_pch and pch_path else None,
    }


def stale_reason(
    source: Path,
    object_path: Path,
    depfile: Path,
    command_file: Path,
    signature: dict[str, object],
    *,
    force: bool,
) -> str | None:
    if force:
        return "forced"
    if not object_path.is_file():
        return "object-missing"
    if _read_json(command_file) != signature:
        return "compile-command-changed"
    dependencies = parse_depfile(depfile)
    if not dependencies:
        return "dependency-file-missing"
    object_mtime = object_path.stat().st_mtime_ns
    for dependency in (source, *dependencies):
        if not dependency.is_file():
            return f"dependency-missing:{dependency}"
        if dependency.stat().st_mtime_ns > object_mtime:
            return f"dependency-newer:{dependency}"
    return None


def _temporary_output(path: Path) -> Path:
    return path.with_name(
        f".{path.name}.{os.getpid()}.{time.time_ns()}.tmp"
    )


def compile_source_incremental(
    *,
    source: Path,
    repo_root: Path,
    object_path: Path,
    logs_dir: Path,
    optimization_flag: str,
    pch_path: Path | None = None,
    force: bool = False,
) -> CompileResult:
    relative = source.relative_to(repo_root).as_posix()
    object_relative = object_path.relative_to(repo_root).as_posix()
    use_pch = (
        pch_path is not None
        and source.name not in PCH_EXCLUDED_SOURCES
        and '#include "pch.h"' in source.read_text(
            encoding="utf-8",
            errors="ignore",
        )
    )
    signature = compile_signature(
        repo_root,
        optimization_flag,
        use_pch=use_pch,
        pch_path=pch_path,
    )
    depfile = dependency_path(object_path)
    command_file = command_path(object_path)
    reason = stale_reason(
        source,
        object_path,
        depfile,
        command_file,
        signature,
        force=force,
    )
    if reason is None:
        return CompileResult(
            source=relative,
            object=object_relative,
            action="reused",
            reason="up-to-date",
            ok=True,
            returncode=0,
            elapsed_ms=0,
            stderr_log=None,
        )

    object_path.parent.mkdir(parents=True, exist_ok=True)
    logs_dir.mkdir(parents=True, exist_ok=True)
    temporary_object = _temporary_output(object_path)
    temporary_depfile = _temporary_output(depfile)
    command = [
        *compile_arguments(repo_root, optimization_flag),
        *(
            ["-include-pch", str(pch_path)]
            if use_pch and pch_path is not None
            else []
        ),
        "-MMD",
        "-MF",
        str(temporary_depfile),
        "-MT",
        str(object_path),
        str(source),
        "-o",
        str(temporary_object),
    ]
    started = time.perf_counter()
    process = subprocess.run(
        command,
        cwd=repo_root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    elapsed_ms = int((time.perf_counter() - started) * 1000)
    log_path = logs_dir / f"{relative.replace('/', '__')}.stderr.txt"
    log_path.write_text(process.stderr or "", encoding="utf-8")
    try:
        if process.returncode == 0:
            if not temporary_object.is_file() or not temporary_depfile.is_file():
                raise RuntimeError(
                    f"compiler did not produce object/depfile for {relative}"
                )
            os.replace(temporary_object, object_path)
            os.replace(temporary_depfile, depfile)
            _atomic_write_json(command_file, signature)
    finally:
        temporary_object.unlink(missing_ok=True)
        temporary_depfile.unlink(missing_ok=True)

    return CompileResult(
        source=relative,
        object=object_relative,
        action="compiled",
        reason=reason,
        ok=process.returncode == 0,
        returncode=process.returncode,
        elapsed_ms=elapsed_ms,
        stderr_log=log_path.relative_to(repo_root).as_posix(),
    )


def _pch_signature(
    repo_root: Path,
    optimization_flag: str,
) -> dict[str, object]:
    arguments = compile_arguments(repo_root, optimization_flag)
    return {
        "schema_version": INCREMENTAL_SCHEMA_VERSION,
        "compiler": _tool_signature(arguments[0]),
        "arguments": arguments[1:],
        "kind": "c++-header",
    }


def ensure_pch(
    *,
    repo_root: Path,
    obj_root: Path,
    optimization_flag: str,
    force: bool,
) -> tuple[Path, bool, int]:
    source = repo_root / "Projects/TMProject/pch.h"
    pch_path = obj_root / "Projects/TMProject/pch.pch"
    depfile = pch_path.with_suffix(".pch.d")
    command_file = pch_path.with_suffix(".pch.command.json")
    signature = _pch_signature(repo_root, optimization_flag)
    reason = stale_reason(
        source,
        pch_path,
        depfile,
        command_file,
        signature,
        force=force,
    )
    if reason is None:
        return pch_path, False, 0

    pch_path.parent.mkdir(parents=True, exist_ok=True)
    temporary_pch = _temporary_output(pch_path)
    temporary_depfile = _temporary_output(depfile)
    arguments = compile_arguments(repo_root, optimization_flag)
    command = [
        *arguments,
        "-x",
        "c++-header",
        "-MMD",
        "-MF",
        str(temporary_depfile),
        "-MT",
        str(pch_path),
        str(source),
        "-o",
        str(temporary_pch),
    ]
    started = time.perf_counter()
    process = subprocess.run(command, cwd=repo_root)
    elapsed_ms = int((time.perf_counter() - started) * 1000)
    try:
        if process.returncode != 0:
            raise subprocess.CalledProcessError(process.returncode, command)
        os.replace(temporary_pch, pch_path)
        os.replace(temporary_depfile, depfile)
        _atomic_write_json(command_file, signature)
    finally:
        temporary_pch.unlink(missing_ok=True)
        temporary_depfile.unlink(missing_ok=True)
    print(
        f"[wasm-inc] pch=compiled reason={reason} "
        f"elapsed_ms={elapsed_ms}"
    )
    return pch_path, True, elapsed_ms


def build_incremental(
    *,
    repo_root: Path,
    vcxproj: Path,
    obj_root: Path,
    report_json: Path,
    jobs: int,
    optimization_flag: str,
    force: bool = False,
    use_pch: bool = True,
) -> dict[str, object]:
    repo_root = repo_root.resolve()
    vcxproj = vcxproj.resolve()
    obj_root = obj_root.resolve()
    sources = parse_vcxproj_sources(vcxproj)
    logs_dir = report_json.parent / "logs-objects-incremental"
    pch_path: Path | None = None
    pch_rebuilt = False
    pch_elapsed_ms = 0
    if use_pch:
        pch_path, pch_rebuilt, pch_elapsed_ms = ensure_pch(
            repo_root=repo_root,
            obj_root=obj_root,
            optimization_flag=optimization_flag,
            force=force,
        )

    started = time.perf_counter()
    results: list[CompileResult] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
        futures = [
            executor.submit(
                compile_source_incremental,
                source=source,
                repo_root=repo_root,
                object_path=object_for_source(
                    repo_root,
                    obj_root,
                    source,
                ),
                logs_dir=logs_dir,
                optimization_flag=optimization_flag,
                pch_path=pch_path,
                force=force,
            )
            for source in sources
        ]
        for future in concurrent.futures.as_completed(futures):
            results.append(future.result())
    elapsed_ms = int((time.perf_counter() - started) * 1000)
    results.sort(key=lambda result: result.source.lower())
    compiled = sum(result.action == "compiled" for result in results)
    reused = sum(result.action == "reused" for result in results)
    failed = sum(not result.ok for result in results)
    report: dict[str, object] = {
        "schema_version": INCREMENTAL_SCHEMA_VERSION,
        "optimization": optimization_flag,
        "jobs": jobs,
        "elapsed_ms": elapsed_ms,
        "pch": {
            "enabled": use_pch,
            "rebuilt": pch_rebuilt,
            "elapsed_ms": pch_elapsed_ms,
        },
        "summary": {
            "total": len(results),
            "compiled": compiled,
            "reused": reused,
            "failed": failed,
        },
        "results": [asdict(result) for result in results],
    }
    _atomic_write_json(report_json, report)
    print(
        f"[wasm-inc] total={len(results)} compiled={compiled} "
        f"reused={reused} failed={failed} elapsed_ms={elapsed_ms}"
    )
    return report


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument(
        "--vcxproj",
        type=Path,
        default=Path("Projects/TMProject/TMProject.vcxproj"),
    )
    parser.add_argument(
        "--obj-root",
        type=Path,
        default=Path("webclient/client-wasm/build/obj"),
    )
    parser.add_argument(
        "--report-json",
        type=Path,
        default=Path(
            "webclient/client-wasm/build/reports/"
            "tmproject-wasm-incremental.json"
        ),
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=max(1, min(8, (os.cpu_count() or 4) // 2)),
    )
    parser.add_argument("--rebuild", action="store_true")
    parser.add_argument("--no-pch", action="store_true")
    args = parser.parse_args(argv)
    repo_root = args.repo_root.resolve()
    opt_level = os.environ.get("OPENWYD_WASM_OPT_LEVEL", "O2")
    if opt_level not in VALID_OPT_LEVELS:
        parser.error(
            "OPENWYD_WASM_OPT_LEVEL must be one of: "
            + ", ".join(sorted(VALID_OPT_LEVELS))
        )
    report = build_incremental(
        repo_root=repo_root,
        vcxproj=(repo_root / args.vcxproj).resolve(),
        obj_root=(repo_root / args.obj_root).resolve(),
        report_json=(repo_root / args.report_json).resolve(),
        jobs=max(1, args.jobs),
        optimization_flag=f"-{opt_level}",
        force=args.rebuild,
        use_pch=not args.no_pch,
    )
    return 1 if report["summary"]["failed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
