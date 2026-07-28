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
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

from tmproject_wasm_object_contract import (
    compile_arguments,
    compiler_identity,
    make_stamp,
    parse_vcxproj_sources,
    publish_stamp,
    stamp_with_object_hashes,
    stamp_path,
)

FIRST_ERROR_RE = re.compile(r"error: (.*)")
VALID_OPT_LEVELS = {"O0", "O1", "O2", "O3", "Os", "Oz"}


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


def first_error(stderr: str) -> str | None:
    for line in stderr.splitlines():
        if "error:" in line:
            m = FIRST_ERROR_RE.search(line)
            return m.group(1).strip() if m else line.strip()
    return None


def capture_contract(
    repo_root: Path,
    obj_root: Path,
    vcxproj: Path,
    optimization_flag: str,
    sources: list[Path],
) -> dict:
    """Capture inputs with a fresh compiler/toolchain identity."""

    compiler_identity.cache_clear()
    return make_stamp(
        repo_root,
        obj_root,
        vcxproj,
        optimization_flag,
        sources,
    )


def run_compile(
    src: Path,
    repo_root: Path,
    obj_root: Path,
    logs_dir: Path,
    optimization_flag: str,
) -> CompileResult:
    rel = src.relative_to(repo_root).as_posix()
    obj_rel = rel.replace(".cpp", ".o")
    obj_path = obj_root / obj_rel
    obj_path.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        *compile_arguments(repo_root, optimization_flag),
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
    opt_level = os.environ.get("OPENWYD_WASM_OPT_LEVEL", "O2")
    if opt_level not in VALID_OPT_LEVELS:
        parser.error(
            "OPENWYD_WASM_OPT_LEVEL must be one of: "
            + ", ".join(sorted(VALID_OPT_LEVELS))
        )
    optimization_flag = f"-{opt_level}"
    vcxproj = (repo_root / args.vcxproj).resolve()
    obj_root = (repo_root / args.obj_root).resolve()
    report_json = (repo_root / args.report_json).resolve()
    report_md = (repo_root / args.report_md).resolve()
    logs_dir = report_json.parent / "logs-objects"
    logs_dir.mkdir(parents=True, exist_ok=True)
    obj_root.mkdir(parents=True, exist_ok=True)

    contract_sources = parse_vcxproj_sources(vcxproj)
    sources = contract_sources
    if args.limit > 0:
        sources = sources[: args.limit]

    contract_path = stamp_path(obj_root)
    contract_path.unlink(missing_ok=True)
    contract_before = capture_contract(
        repo_root,
        obj_root,
        vcxproj,
        optimization_flag,
        contract_sources,
    )

    print(f"[obj] vcxproj={vcxproj}")
    print(f"[obj] tus={len(sources)} jobs={args.jobs} optimization={optimization_flag}")

    started = time.perf_counter()
    results: list[CompileResult] = []

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        future_map = {
            executor.submit(
                run_compile,
                src,
                repo_root,
                obj_root,
                logs_dir,
                optimization_flag,
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

    contract_after = capture_contract(
        repo_root,
        obj_root,
        vcxproj,
        optimization_flag,
        contract_sources,
    )
    contract_unchanged = contract_after == contract_before
    contract_error: str | None = None
    certified = False

    if fail_count == 0 and args.limit == 0 and contract_unchanged:
        try:
            stamp_candidate = stamp_with_object_hashes(
                repo_root,
                obj_root,
                contract_sources,
                contract_before,
            )
            contract_after_hash = capture_contract(
                repo_root,
                obj_root,
                vcxproj,
                optimization_flag,
                contract_sources,
            )
            if contract_after_hash != contract_before:
                contract_unchanged = False
                contract_after = contract_after_hash
                contract_error = (
                    "source or toolchain contract changed while object hashes "
                    "were being captured"
                )
            else:
                publish_stamp(obj_root, stamp_candidate)
                certified = True
        except OSError as error:
            contract_error = f"could not certify object set: {error}"
    elif not contract_unchanged:
        contract_error = (
            "source or toolchain contract changed while compilation was in "
            "progress"
        )

    if not certified:
        contract_path.unlink(missing_ok=True)

    report = {
        "vcxproj": str(vcxproj.relative_to(repo_root)),
        "jobs": args.jobs,
        "optimization": optimization_flag,
        "elapsed_ms": elapsed_ms,
        "summary": {"total": len(results), "ok": ok_count, "failed": fail_count},
        "contract": {
            "before_fingerprint": contract_before.get("fingerprint"),
            "after_fingerprint": contract_after.get("fingerprint"),
            "unchanged": contract_unchanged,
            "certified": certified,
            "error": contract_error,
        },
        "top_first_errors": first_errors.most_common(50),
        "results": [r.__dict__ for r in results],
    }

    report_json.parent.mkdir(parents=True, exist_ok=True)
    report_json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    report_md.write_text(to_markdown(len(results), ok_count, fail_count, first_errors, slowest, fails), encoding="utf-8")

    print(f"[obj] done in {elapsed_ms} ms")
    print(f"[obj] ok={ok_count} failed={fail_count}")
    print(
        f"[obj] contract_unchanged={str(contract_unchanged).lower()} "
        f"certified={str(certified).lower()}"
    )
    if contract_error:
        print(f"[obj] contract_error={contract_error}")
    print(f"[obj] report_json={report_json.relative_to(repo_root)}")
    print(f"[obj] report_md={report_md.relative_to(repo_root)}")

    if not contract_unchanged or contract_error:
        return 2
    return 0 if fail_count == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
