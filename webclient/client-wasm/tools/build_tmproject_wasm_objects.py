#!/usr/bin/env python3
"""Compile TMProject C++ translation units with em++ (-c) and emit object-build reports."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import time
import xml.etree.ElementTree as ET
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

NS = {"msb": "http://schemas.microsoft.com/developer/msbuild/2003"}

FIRST_ERROR_RE = re.compile(r"error: (.*)")


@dataclass
class CompileResult:
    source_rel: str
    source_abs: str
    object_rel: str
    ok: bool
    returncode: int
    elapsed_ms: int
    first_error: str | None
    stderr_log: str


def parse_vcxproj_sources(vcxproj: Path) -> list[Path]:
    root = ET.parse(vcxproj).getroot()
    out: list[Path] = []
    for node in root.findall(".//msb:ClCompile", NS):
        include = node.attrib.get("Include")
        if include:
            out.append((vcxproj.parent / include).resolve())
    return out


def first_error(stderr: str) -> str | None:
    for line in stderr.splitlines():
        if "error:" in line:
            m = FIRST_ERROR_RE.search(line)
            return m.group(1).strip() if m else line.strip()
    return None


def run_compile(
    src: Path,
    repo_root: Path,
    compat_include: Path,
    directx_include: Path,
    tmproject_include: Path,
    preinclude: Path,
    obj_root: Path,
    logs_dir: Path,
    extra_defines: Sequence[str],
) -> CompileResult:
    rel = src.relative_to(repo_root).as_posix()
    obj_rel = rel.replace(".cpp", ".o")
    obj_path = obj_root / obj_rel
    obj_path.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        "em++",
        "-std=c++17",
        "-c",
        "-fms-extensions",
        "-Wno-microsoft-cast",
        "-Wno-microsoft-anon-tag",
        "-Wno-unknown-pragmas",
        "-include",
        str(preinclude),
        f"-I{compat_include}",
        f"-I{tmproject_include}",
        f"-I{directx_include}",
        *extra_defines,
        str(src),
        "-o",
        str(obj_path),
    ]

    start = time.perf_counter()
    proc = subprocess.run(cmd, cwd=repo_root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    elapsed_ms = int((time.perf_counter() - start) * 1000)

    stderr = proc.stderr or ""
    log_path = logs_dir / (rel.replace("/", "__") + ".stderr.txt")
    log_path.write_text(stderr, encoding="utf-8")

    return CompileResult(
        source_rel=rel,
        source_abs=str(src),
        object_rel=str(obj_path.relative_to(repo_root).as_posix()),
        ok=proc.returncode == 0,
        returncode=proc.returncode,
        elapsed_ms=elapsed_ms,
        first_error=first_error(stderr),
        stderr_log=str(log_path.relative_to(repo_root).as_posix()),
    )


def to_markdown(total: int, ok_count: int, fail_count: int, first_errors: Counter, slowest: list[CompileResult], fails: list[CompileResult]) -> str:
    lines: list[str] = []
    lines.append("# TMProject WASM Object Build")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append(f"- total TUs: **{total}**")
    lines.append(f"- objects generated: **{ok_count}**")
    lines.append(f"- failed: **{fail_count}**")
    lines.append("")
    lines.append("## Top Error Signatures")
    lines.append("")
    if first_errors:
        for err, count in first_errors.most_common(20):
            lines.append(f"- `{err}`: {count}")
    else:
        lines.append("- none")
    lines.append("")
    lines.append("## Slowest Compiles")
    lines.append("")
    for item in slowest:
        status = "ok" if item.ok else "fail"
        lines.append(f"- `{item.source_rel}` -> `{item.object_rel}`: {item.elapsed_ms} ms ({status})")
    lines.append("")
    lines.append("## Failed Examples")
    lines.append("")
    if fails:
        for item in fails[:30]:
            lines.append(f"- `{item.source_rel}`")
            if item.first_error:
                lines.append(f"  first error: `{item.first_error}`")
            lines.append(f"  stderr log: `{item.stderr_log}`")
    else:
        lines.append("- none")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--vcxproj", type=Path, default=Path("Projects/TMProject/TMProject.vcxproj"))
    parser.add_argument("--obj-root", type=Path, default=Path("webclient/client-wasm/build/obj"))
    parser.add_argument("--report-json", type=Path, default=Path("webclient/client-wasm/build/reports/tmproject-wasm-objects.json"))
    parser.add_argument("--report-md", type=Path, default=Path("webclient/client-wasm/build/reports/tmproject-wasm-objects.md"))
    parser.add_argument("--jobs", type=int, default=max(1, min(8, (os.cpu_count() or 4) // 2)))
    parser.add_argument("--limit", type=int, default=0, help="Optional TU limit for faster dry-runs.")
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    vcxproj = (repo_root / args.vcxproj).resolve()
    obj_root = (repo_root / args.obj_root).resolve()
    report_json = (repo_root / args.report_json).resolve()
    report_md = (repo_root / args.report_md).resolve()
    logs_dir = report_json.parent / "logs-objects"
    logs_dir.mkdir(parents=True, exist_ok=True)
    obj_root.mkdir(parents=True, exist_ok=True)

    compat_include = (repo_root / "webclient/client-wasm/compat/include").resolve()
    directx_include = (repo_root / "Dependencies/Directx/Include").resolve()
    tmproject_include = (repo_root / "Projects/TMProject").resolve()
    preinclude = (compat_include / "tm_emscripten_prelude.h").resolve()

    sources = parse_vcxproj_sources(vcxproj)
    if args.limit > 0:
        sources = sources[: args.limit]

    defines = [
        "-DWIN32",
        "-D_WINDOWS",
        "-DNDEBUG",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-D_WINSOCK_DEPRECATED_NO_WARNINGS",
    ]

    print(f"[obj] vcxproj={vcxproj}")
    print(f"[obj] tus={len(sources)} jobs={args.jobs}")

    started = time.perf_counter()
    results: list[CompileResult] = []

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        future_map = {
            executor.submit(
                run_compile,
                src,
                repo_root,
                compat_include,
                directx_include,
                tmproject_include,
                preinclude,
                obj_root,
                logs_dir,
                defines,
            ): src
            for src in sources
        }

        done = 0
        for future in concurrent.futures.as_completed(future_map):
            results.append(future.result())
            done += 1
            if done % 10 == 0 or done == len(sources):
                print(f"[obj] progress={done}/{len(sources)}")

    elapsed_ms = int((time.perf_counter() - started) * 1000)

    results.sort(key=lambda r: r.source_rel)
    ok_count = sum(1 for r in results if r.ok)
    fail_count = len(results) - ok_count
    first_errors = Counter(r.first_error for r in results if r.first_error and not r.ok)
    slowest = sorted(results, key=lambda r: r.elapsed_ms, reverse=True)[:20]
    fails = [r for r in results if not r.ok]

    report = {
        "vcxproj": str(vcxproj.relative_to(repo_root)),
        "jobs": args.jobs,
        "elapsed_ms": elapsed_ms,
        "summary": {"total": len(results), "ok": ok_count, "failed": fail_count},
        "top_first_errors": first_errors.most_common(50),
        "results": [r.__dict__ for r in results],
    }

    report_json.parent.mkdir(parents=True, exist_ok=True)
    report_json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    report_md.write_text(to_markdown(len(results), ok_count, fail_count, first_errors, slowest, fails), encoding="utf-8")

    print(f"[obj] done in {elapsed_ms} ms")
    print(f"[obj] ok={ok_count} failed={fail_count}")
    print(f"[obj] report_json={report_json.relative_to(repo_root)}")
    print(f"[obj] report_md={report_md.relative_to(repo_root)}")

    return 0 if fail_count == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())

