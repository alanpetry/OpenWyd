#!/usr/bin/env python3
"""Link TMProject object set into a startup-callable WASM artifact exporting _wyd_start_client.

This script compiles compatibility sources, links with strict undefined-symbol checks,
and emits reports for unresolved symbols when link fails.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from collections import Counter
from pathlib import Path
from typing import Any, Sequence

TOOLS_DIRECTORY = Path(__file__).resolve().parent
if str(TOOLS_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIRECTORY))

from tmproject_wasm_object_contract import (
    compile_arguments,
    compiler_identity,
    expected_objects,
    make_stamp,
    parse_vcxproj_sources,
    read_stamp,
    resolve_emxx,
    stamp_matches,
)
from preload_manifest import read_preload_entries
from build_gdi_font_atlas import (
    build_gdi_font_atlas,
    validate_gdi_font_atlas_manifest,
)
from build_tmproject_wasm_incremental import (
    build_incremental,
    compile_source_incremental,
)

UNDEF_RE = re.compile(r"undefined symbol: (.+)$")
VALID_OPT_LEVELS = {"O0", "O1", "O2", "O3", "Os", "Oz"}
STARTUP_OUTPUT_NAMES = (
    "tmproject_startup.js",
    "tmproject_startup.wasm",
    "tmproject_startup.data",
)
STARTUP_LINK_CONTRACT_SCHEMA = "openwyd.tmproject-wasm-startup-link-contract"
STARTUP_LINK_CONTRACT_VERSION = 1
_PRESERVE_STARTUP_OUTPUTS_ON_FAILURE = False


def compile_source(repo_root: Path, src: Path, out_obj: Path, optimization_flag: str) -> None:
    out_obj.parent.mkdir(parents=True, exist_ok=True)

    cmd = [
        *compile_arguments(repo_root, optimization_flag),
        str(src),
        "-o",
        str(out_obj),
    ]

    subprocess.run(cmd, cwd=repo_root, check=True)


def build_gdi_font_atlas_preload(repo_root: Path) -> list[str]:
    output_dir = (
        repo_root
        / "webclient/client-wasm/build/generated/gdi-font"
    )
    atlas, manifest = build_gdi_font_atlas(repo_root, output_dir)
    atlas_source = atlas.relative_to(repo_root).as_posix()
    manifest_source = manifest.relative_to(repo_root).as_posix()
    return [
        f"{atlas_source}@/OpenWydGdiFontAtlas.bin",
        f"{manifest_source}@/OpenWydGdiFontAtlas.json",
    ]


def all_tmproject_objects(obj_root: Path) -> list[Path]:
    base = obj_root / "Projects" / "TMProject"
    objs = sorted(base.glob("*.o"))
    return objs


def ensure_tmproject_object_contract(
    repo_root: Path,
    obj_root: Path,
    optimization_flag: str,
) -> tuple[list[Path], bool]:
    """Return an exact, content-addressed TMProject object set.

    The fingerprint covers every project TU, transitive header search tree,
    preinclude, compile arguments, optimization level, and the vcxproj. A
    mismatch invokes the parallel full-object builder; individual mtime checks
    are deliberately insufficient for C++.
    """

    vcxproj = repo_root / "Projects/TMProject/TMProject.vcxproj"
    sources = parse_vcxproj_sources(vcxproj)
    expected = expected_objects(repo_root, obj_root, sources)
    expected_set = {item.resolve() for item in expected}
    actual = all_tmproject_objects(obj_root)
    orphaned = [
        item for item in actual if item.resolve() not in expected_set
    ]
    if orphaned:
        names = ", ".join(
            item.relative_to(repo_root).as_posix() for item in orphaned
        )
        raise RuntimeError(
            "orphaned TMProject objects are forbidden; clean the object "
            f"directory before linking: {names}"
        )

    expected_contract = make_stamp(
        repo_root,
        obj_root,
        vcxproj,
        optimization_flag,
        sources,
    )
    current_contract = read_stamp(obj_root)
    objects_exist = all(item.is_file() for item in expected)
    if (
        stamp_matches(current_contract, expected_contract, repo_root)
        and objects_exist
    ):
        return expected, False

    builder = TOOLS_DIRECTORY / "build_tmproject_wasm_objects.py"
    print(
        "[startup-link] TMProject object contract changed or is missing; "
        "running a full dependency-aware parallel rebuild"
    )
    subprocess.run(
        [
            sys.executable,
            str(builder),
            "--repo-root",
            str(repo_root),
            "--vcxproj",
            str(vcxproj),
            "--obj-root",
            str(obj_root),
        ],
        cwd=repo_root,
        check=True,
    )

    actual = all_tmproject_objects(obj_root)
    orphaned = [
        item for item in actual if item.resolve() not in expected_set
    ]
    if orphaned:
        names = ", ".join(
            item.relative_to(repo_root).as_posix() for item in orphaned
        )
        raise RuntimeError(
            "full rebuild left orphaned TMProject objects: " + names
        )
    if not all(item.is_file() for item in expected):
        missing = [
            item.relative_to(repo_root).as_posix()
            for item in expected
            if not item.is_file()
        ]
        raise RuntimeError(
            "full rebuild did not produce the expected objects: "
            + ", ".join(missing)
        )
    rebuilt_contract = read_stamp(obj_root)
    if not stamp_matches(rebuilt_contract, expected_contract, repo_root):
        raise RuntimeError(
            "full rebuild did not publish the expected object contract"
        )
    return expected, True


def ensure_tmproject_objects_incremental(
    repo_root: Path,
    obj_root: Path,
    optimization_flag: str,
    *,
    jobs: int,
    force: bool,
) -> tuple[list[Path], bool]:
    """Update only stale translation units and return the exact project set."""

    vcxproj = repo_root / "Projects/TMProject/TMProject.vcxproj"
    sources = parse_vcxproj_sources(vcxproj)
    report = build_incremental(
        repo_root=repo_root,
        vcxproj=vcxproj,
        obj_root=obj_root,
        report_json=(
            repo_root
            / "webclient/client-wasm/build/reports/"
            "tmproject-wasm-incremental.json"
        ),
        jobs=jobs,
        optimization_flag=optimization_flag,
        force=force,
        use_pch=True,
    )
    summary = report["summary"]
    if summary["failed"]:
        raise RuntimeError(
            f"incremental object build failed for {summary['failed']} TUs"
        )
    objects = expected_objects(repo_root, obj_root, sources)
    missing = [
        path.relative_to(repo_root).as_posix()
        for path in objects
        if not path.is_file()
    ]
    if missing:
        raise RuntimeError(
            "incremental build did not produce expected objects: "
            + ", ".join(missing)
        )
    return objects, bool(summary["compiled"])


def parse_undefined(stderr_text: str) -> Counter[str]:
    counter: Counter[str] = Counter()
    for line in stderr_text.splitlines():
        m = UNDEF_RE.search(line)
        if m:
            counter[m.group(1).strip()] += 1
    return counter


def startup_output_paths(link_dir: Path) -> tuple[Path, ...]:
    """Return only the generated artifacts this script is allowed to remove."""

    return tuple(link_dir / name for name in STARTUP_OUTPUT_NAMES)


def invalidate_startup_outputs(link_dir: Path) -> None:
    """Prevent a web server from serving an artifact from an older build."""

    for output in startup_output_paths(link_dir):
        output.unlink(missing_ok=True)


def _atomic_write_text(path: Path, text: str) -> None:
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


def _validate_javascript_syntax(path: Path) -> None:
    """Reject malformed EM_JS output before switching the atomic bootstrap."""

    candidates: list[Path] = []
    configured = os.environ.get("EMSDK_NODE")
    if configured:
        candidates.append(Path(configured))
    discovered = shutil.which("node")
    if discovered:
        candidates.append(Path(discovered))
    tools_root = TOOLS_DIRECTORY.parents[3] / ".tools" / "emsdk" / "node"
    if tools_root.is_dir():
        candidates.extend(sorted(tools_root.glob("*/bin/node.exe"), reverse=True))
        candidates.extend(sorted(tools_root.glob("*/bin/node"), reverse=True))

    node = next((candidate for candidate in candidates if candidate.is_file()), None)
    if node is None:
        raise RuntimeError("Node.js is required to validate generated JavaScript")
    subprocess.run([str(node), "--check", str(path)], check=True)


def publish_incremental_code(
    staging_dir: Path,
    link_dir: Path,
    build_name: str,
    link_signature: dict[str, Any],
) -> tuple[Path, Path]:
    """Publish a complete versioned JS/WASM pair, then switch one bootstrap."""

    staged_js = staging_dir / f"{build_name}.js"
    staged_wasm = staging_dir / f"{build_name}.wasm"
    _validate_javascript_syntax(staged_js)
    published_js = link_dir / staged_js.name
    published_wasm = link_dir / staged_wasm.name
    os.replace(staged_wasm, published_wasm)
    os.replace(staged_js, published_js)
    bootstrap = (
        "/* Generated atomically by link_tmproject_wasm_startup.py. */\n"
        "document.write("
        f"'<script src=\"./{published_js.name}\"><\\/script>'"
        ");\n"
    )
    _atomic_write_text(link_dir / "tmproject_startup.js", bootstrap)
    _atomic_write_text(
        link_dir / "tmproject_startup.state.json",
        json.dumps(
            {
                "schema_version": 1,
                "javascript": published_js.name,
                "wasm": published_wasm.name,
                "link_signature": link_signature,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
    )
    return published_js, published_wasm


def incremental_link_signature(
    link_cmd: Sequence[str],
    objects: Sequence[Path],
) -> dict[str, Any]:
    normalized_command = list(link_cmd)
    try:
        output_index = normalized_command.index("-o") + 1
        normalized_command[output_index] = "<VERSIONED_OUTPUT>"
    except (ValueError, IndexError):
        pass
    inputs = []
    for path in (*objects, Path(__file__)):
        stat = path.stat()
        inputs.append(
            {
                "path": str(path.resolve()),
                "size": stat.st_size,
                "mtime_ns": stat.st_mtime_ns,
            }
        )
    return {
        "schema_version": 1,
        "command": normalized_command,
        "inputs": inputs,
    }


def reusable_incremental_code(
    link_dir: Path,
    signature: dict[str, Any],
) -> bool:
    try:
        state = json.loads(
            (link_dir / "tmproject_startup.state.json").read_text(
                encoding="utf-8"
            )
        )
    except (OSError, json.JSONDecodeError):
        return False
    javascript = state.get("javascript")
    wasm = state.get("wasm")
    return (
        state.get("link_signature") == signature
        and isinstance(javascript, str)
        and isinstance(wasm, str)
        and (link_dir / javascript).is_file()
        and (link_dir / wasm).is_file()
        and (link_dir / "tmproject_startup.js").is_file()
    )


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _contract_path(repo_root: Path, path: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return str(resolved)


def _add_contract_file(
    records: dict[str, dict[str, Any]],
    repo_root: Path,
    path: Path,
    role: str,
) -> None:
    resolved = path.resolve()
    if not resolved.is_file():
        raise RuntimeError(f"startup link input is missing ({role}): {resolved}")
    label = _contract_path(repo_root, resolved)
    record = records.get(label)
    if record is None:
        stat = resolved.stat()
        record = {
            "path": label,
            "roles": [],
            "size": stat.st_size,
            "sha256": _sha256_file(resolved),
        }
        records[label] = record
    if role not in record["roles"]:
        record["roles"].append(role)


def _preload_source_path(repo_root: Path, entry: str) -> Path:
    source, _, _ = entry.partition("@")
    source = source.strip()
    if not source:
        raise RuntimeError(f"invalid empty preload source in entry: {entry!r}")
    path = Path(source)
    return path.resolve() if path.is_absolute() else (repo_root / path).resolve()


def capture_startup_link_contract(
    *,
    repo_root: Path,
    obj_root: Path,
    optimization_flag: str,
    tm_objects: Sequence[Path],
    entry_src: Path,
    stubs_src: Path,
    entry_obj: Path,
    stubs_obj: Path,
    preload_manifest: Path,
    preload_entries: Sequence[str],
    link_cmd: Sequence[str],
    response_files: Sequence[Path],
    generated_preload_entries: Sequence[str] = (),
) -> dict[str, Any]:
    """Capture every input that can affect the public startup artifact.

    The existing TMProject stamp proves the source/header/compiler contract and
    binds every expected object by content. This outer contract additionally
    binds the compatibility translation units and objects, the freshly expanded
    preload manifest and payload, the exact linker arguments/response files,
    and the linker identity. Calling this function both immediately before and
    immediately after the link closes the concurrent-edit freshness gap.
    """

    repo_root = repo_root.resolve()
    obj_root = obj_root.resolve()
    vcxproj = repo_root / "Projects/TMProject/TMProject.vcxproj"
    sources = parse_vcxproj_sources(vcxproj)
    expected_tm_objects = expected_objects(repo_root, obj_root, sources)
    actual_tm_objects = [Path(item).resolve() for item in tm_objects]
    if actual_tm_objects != [item.resolve() for item in expected_tm_objects]:
        raise RuntimeError(
            "startup link TMProject object list does not match the project contract"
        )

    # make_stamp() caches compiler_identity() for performance. Force a fresh
    # observation on both sides of the link so an emsdk/config/toolchain change
    # cannot compare equal merely because this Python process stayed alive.
    clear_compiler_identity = getattr(compiler_identity, "cache_clear", None)
    if callable(clear_compiler_identity):
        clear_compiler_identity()
    linker_identity = compiler_identity()
    expected_tm_contract = make_stamp(
        repo_root,
        obj_root,
        vcxproj,
        optimization_flag,
        sources,
    )
    stored_tm_contract = read_stamp(obj_root)
    if not stamp_matches(stored_tm_contract, expected_tm_contract, repo_root):
        raise RuntimeError(
            "TMProject source/toolchain/object contract changed before startup "
            "link contract capture"
        )

    current_manifest_preload_entries = read_preload_entries(
        repo_root,
        preload_manifest,
    )
    if generated_preload_entries:
        generated_manifests = [
            _preload_source_path(repo_root, entry)
            for entry in generated_preload_entries
            if entry.partition("@")[2].strip()
            == "/OpenWydGdiFontAtlas.json"
        ]
        generated_atlases = [
            _preload_source_path(repo_root, entry)
            for entry in generated_preload_entries
            if entry.partition("@")[2].strip()
            == "/OpenWydGdiFontAtlas.bin"
        ]
        if len(generated_manifests) != 1 or len(generated_atlases) != 1:
            raise RuntimeError(
                "generated GDI font preload must contain exactly one certified "
                "manifest and binary"
            )
        validate_gdi_font_atlas_manifest(
            repo_root,
            generated_manifests[0],
            atlas=generated_atlases[0],
        )
    current_preload_entries = [
        *current_manifest_preload_entries,
        *generated_preload_entries,
    ]
    expected_preload_entries = list(preload_entries)
    if current_preload_entries != expected_preload_entries:
        raise RuntimeError(
            "preload manifest expansion changed before startup link contract capture"
        )

    records: dict[str, dict[str, Any]] = {}
    for object_path in expected_tm_objects:
        _add_contract_file(
            records,
            repo_root,
            object_path,
            "tmproject-object",
        )
    for path, role in (
        (vcxproj, "tmproject-project"),
        (entry_src, "compat-entry-source"),
        (stubs_src, "compat-stubs-source"),
        (entry_obj, "compat-entry-object"),
        (stubs_obj, "compat-stubs-object"),
        (Path(__file__), "startup-link-script"),
        (
            Path(__file__).with_name("tmproject_wasm_object_contract.py"),
            "tmproject-contract-script",
        ),
    ):
        _add_contract_file(records, repo_root, path, role)
    if generated_preload_entries:
        for path, role in (
            (
                repo_root
                / "webclient/client-wasm/tools/generate_gdi_font_atlas.cpp",
                "gdi-font-atlas-generator-source",
            ),
            (
                repo_root
                / "webclient/client-wasm/tools/build_gdi_font_atlas.py",
                "gdi-font-atlas-build-script",
            ),
        ):
            _add_contract_file(records, repo_root, path, role)

    manifest_record: dict[str, Any]
    if preload_manifest.is_file():
        _add_contract_file(
            records,
            repo_root,
            preload_manifest,
            "preload-manifest",
        )
        manifest_record = {
            "exists": True,
            "path": _contract_path(repo_root, preload_manifest),
        }
    else:
        manifest_record = {
            "exists": False,
            "path": _contract_path(repo_root, preload_manifest),
        }

    for entry in current_preload_entries:
        preload_source = _preload_source_path(repo_root, entry)
        if preload_source.is_dir():
            descendants = sorted(
                (
                    candidate
                    for candidate in preload_source.rglob("*")
                    if candidate.is_file()
                ),
                key=lambda candidate: candidate.as_posix().lower(),
            )
            for descendant in descendants:
                _add_contract_file(
                    records,
                    repo_root,
                    descendant,
                    "preload-asset",
                )
        else:
            _add_contract_file(
                records,
                repo_root,
                preload_source,
                "preload-asset",
            )

    for response_file in response_files:
        _add_contract_file(
            records,
            repo_root,
            response_file,
            "link-response-file",
        )

    ordered_records = []
    for label in sorted(records):
        record = dict(records[label])
        record["roles"] = sorted(record["roles"])
        ordered_records.append(record)

    payload: dict[str, Any] = {
        "schema": STARTUP_LINK_CONTRACT_SCHEMA,
        "schema_version": STARTUP_LINK_CONTRACT_VERSION,
        "optimization": optimization_flag,
        "tmproject": {
            "fingerprint": expected_tm_contract["fingerprint"],
            "sources": expected_tm_contract["sources"],
            "objects": expected_tm_contract["objects"],
            "object_sha256": stored_tm_contract["object_sha256"],
        },
        "linker_identity": linker_identity,
        "link_command": list(link_cmd),
        "preload": {
            "manifest": manifest_record,
            "entries": current_preload_entries,
            "generated_entries": list(generated_preload_entries),
        },
        "files": ordered_records,
    }
    fingerprint_payload = json.dumps(
        payload,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    payload["fingerprint"] = hashlib.sha256(fingerprint_payload).hexdigest()
    return payload


def write_response_file(path: Path, args: list[str]) -> None:
    """Write arguments in the format consumed by Emscripten response files.

    Emscripten expands response files with ``shlex.split`` on every host.
    ``shlex.join`` therefore gives us a reversible representation while keeping
    the CreateProcess command line short enough for Windows.
    """

    path.write_text(shlex.join(args) + "\n", encoding="utf-8")


def write_reports(
    report_json: Path,
    report_md: Path,
    link_ok: bool,
    returncode: int,
    cmd: list[str],
    undef: Counter[str],
    *,
    phase: str = "link",
    error: str | None = None,
    input_contract: dict[str, Any] | None = None,
) -> None:
    report_json.parent.mkdir(parents=True, exist_ok=True)

    payload = {
        "ok": link_ok,
        "returncode": returncode,
        "phase": phase,
        "error": error,
        "input_contract": input_contract,
        "command": cmd,
        "undefined_total": sum(undef.values()),
        "undefined_unique": len(undef),
        "top_undefined": undef.most_common(300),
    }
    report_json.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    lines: list[str] = []
    lines.append("# TMProject WASM Startup Link")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append(f"- link ok: **{str(link_ok).lower()}**")
    lines.append(f"- return code: **{returncode}**")
    lines.append(f"- phase: **{phase}**")
    if error:
        lines.append(f"- error: `{error}`")
    if input_contract:
        lines.append(
            "- input contract before: "
            f"`{input_contract.get('before_fingerprint')}`"
        )
        lines.append(
            "- input contract after: "
            f"`{input_contract.get('after_fingerprint')}`"
        )
        lines.append(
            "- input contract unchanged: "
            f"**{str(bool(input_contract.get('unchanged'))).lower()}**"
        )
    lines.append(f"- undefined references: **{sum(undef.values())}**")
    lines.append(f"- unique undefined symbols: **{len(undef)}**")
    lines.append("")
    lines.append("## Top Undefined Symbols")
    lines.append("")
    if undef:
        for sym, count in undef.most_common(200):
            lines.append(f"- `{sym}`: {count}")
    else:
        lines.append("- none")
    lines.append("")

    report_md.write_text("\n".join(lines), encoding="utf-8")


def fail_startup_build(
    link_dir: Path,
    report_json: Path,
    report_md: Path,
    *,
    phase: str,
    error: BaseException | str,
    returncode: int = 2,
    cmd: list[str] | None = None,
    undef: Counter[str] | None = None,
    input_contract: dict[str, Any] | None = None,
) -> int:
    """Publish a failure report, preserving the last good incremental build."""

    error_text = str(error)
    if not _PRESERVE_STARTUP_OUTPUTS_ON_FAILURE:
        try:
            invalidate_startup_outputs(link_dir)
        except OSError as invalidation_error:
            error_text = (
                f"{error_text}; could not invalidate startup outputs: "
                f"{invalidation_error}"
            )
            returncode = 2
    write_reports(
        report_json,
        report_md,
        False,
        returncode,
        cmd or [],
        undef or Counter(),
        phase=phase,
        error=error_text,
        input_contract=input_contract,
    )
    print(f"[startup-link] failed phase={phase}: {error_text}")
    return returncode


def main() -> int:
    global _PRESERVE_STARTUP_OUTPUTS_ON_FAILURE

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--obj-root", type=Path, default=Path("webclient/client-wasm/build/obj"))
    parser.add_argument("--link-dir", type=Path, default=Path("webclient/client-wasm/build/link"))
    parser.add_argument("--report-json", type=Path, default=Path("webclient/client-wasm/build/reports/startup-link.json"))
    parser.add_argument("--report-md", type=Path, default=Path("webclient/client-wasm/build/reports/startup-link.md"))
    parser.add_argument(
        "--preload-manifest",
        type=Path,
        default=Path("webclient/client-wasm/config/startup-preload-manifest.txt"),
    )
    parser.add_argument(
        "--dev",
        action="store_true",
        help=(
            "Use per-TU incremental objects, external assets, cached compat "
            "objects, and atomic publication."
        ),
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=max(1, min(8, (os.cpu_count() or 4) // 2)),
    )
    parser.add_argument(
        "--rebuild",
        action="store_true",
        help="Force all objects to rebuild (still leaves assets external).",
    )
    parser.add_argument(
        "--link-opt-level",
        choices=sorted(VALID_OPT_LEVELS),
        help=(
            "Override only the final link optimization. Dev defaults to O0 "
            "while translation units remain at OPENWYD_WASM_OPT_LEVEL."
        ),
    )
    args = parser.parse_args()
    _PRESERVE_STARTUP_OUTPUTS_ON_FAILURE = args.dev

    repo_root = args.repo_root.resolve()
    opt_level = os.environ.get("OPENWYD_WASM_OPT_LEVEL", "O2")
    if opt_level not in VALID_OPT_LEVELS:
        parser.error(
            "OPENWYD_WASM_OPT_LEVEL must be one of: "
            + ", ".join(sorted(VALID_OPT_LEVELS))
        )
    optimization_flag = f"-{opt_level}"
    link_optimization_flag = (
        f"-{args.link_opt_level}"
        if args.link_opt_level
        else "-O0"
        if args.dev
        else optimization_flag
    )
    obj_root = (repo_root / args.obj_root).resolve()
    link_dir = (repo_root / args.link_dir).resolve()
    report_json = (repo_root / args.report_json).resolve()
    report_md = (repo_root / args.report_md).resolve()
    preload_manifest = (repo_root / args.preload_manifest).resolve()

    link_dir.mkdir(parents=True, exist_ok=True)
    staging_context: tempfile.TemporaryDirectory[str] | None = None
    build_name = "tmproject_startup"
    build_output_dir = link_dir
    if args.dev:
        build_name = f"tmproject_startup.{time.time_ns()}"
        staging_context = tempfile.TemporaryDirectory(
            prefix=".openwyd-code-",
            dir=link_dir,
        )
        build_output_dir = Path(staging_context.name)
    out_js = build_output_dir / f"{build_name}.js"
    stdout_path = link_dir / "startup-strict-all.stdout.txt"
    stderr_path = link_dir / "startup-strict-all.stderr.txt"

    if not args.dev:
        try:
            invalidate_startup_outputs(link_dir)
        except OSError as error:
            write_reports(
                report_json,
                report_md,
                False,
                2,
                [],
                Counter(),
                phase="invalidate-before-build",
                error=str(error),
            )
            print(f"[startup-link] could not invalidate old outputs: {error}")
            return 2
    write_reports(
        report_json,
        report_md,
        False,
        2,
        [],
        Counter(),
        phase="build-started",
        error=(
            "incremental build in progress; last good artifact preserved"
            if args.dev
            else "startup outputs invalidated; build has not completed"
        ),
    )

    entry_src = repo_root / "webclient/client-wasm/compat/src/wyd_client_entry.cpp"
    stubs_src = repo_root / "webclient/client-wasm/compat/src/win32_emscripten_stubs.cpp"
    entry_obj = obj_root / "webclient/client-wasm/compat/src/wyd_client_entry.o"
    stubs_obj = obj_root / "webclient/client-wasm/compat/src/win32_emscripten_stubs.o"

    try:
        if args.dev:
            tm_objs, rebuilt_tmproject = ensure_tmproject_objects_incremental(
                repo_root,
                obj_root,
                optimization_flag,
                jobs=max(1, args.jobs),
                force=args.rebuild,
            )
        else:
            tm_objs, rebuilt_tmproject = ensure_tmproject_object_contract(
                repo_root,
                obj_root,
                optimization_flag,
            )
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        return fail_startup_build(
            link_dir,
            report_json,
            report_md,
            phase="tmproject-object-contract",
            error=error,
            returncode=(
                error.returncode
                if isinstance(error, subprocess.CalledProcessError)
                else 2
            ),
            cmd=(
                list(error.cmd)
                if isinstance(error, subprocess.CalledProcessError)
                and not isinstance(error.cmd, str)
                else [str(error.cmd)]
                if isinstance(error, subprocess.CalledProcessError)
                else []
            ),
        )
    if rebuilt_tmproject:
        print(
            f"[startup-link] rebuilt and verified {len(tm_objs)} "
            "TMProject objects"
        )
    else:
        print(
            f"[startup-link] verified reusable TMProject object contract "
            f"({len(tm_objs)} objects)"
        )

    print(
        f"[startup-link] compiling: {entry_src.relative_to(repo_root)} "
        f"({optimization_flag})"
    )
    try:
        if args.dev:
            result = compile_source_incremental(
                source=entry_src,
                repo_root=repo_root,
                object_path=entry_obj,
                logs_dir=(
                    repo_root
                    / "webclient/client-wasm/build/reports/"
                    "logs-compat-incremental"
                ),
                optimization_flag=optimization_flag,
                force=args.rebuild,
            )
            print(
                f"[startup-link] entry={result.action} "
                f"reason={result.reason} elapsed_ms={result.elapsed_ms}"
            )
            if not result.ok:
                raise subprocess.CalledProcessError(
                    result.returncode,
                    [str(entry_src)],
                )
        else:
            compile_source(repo_root, entry_src, entry_obj, optimization_flag)
    except (OSError, subprocess.CalledProcessError) as error:
        return fail_startup_build(
            link_dir,
            report_json,
            report_md,
            phase="compile-entry",
            error=error,
            returncode=(
                error.returncode
                if isinstance(error, subprocess.CalledProcessError)
                else 2
            ),
            cmd=(
                list(error.cmd)
                if isinstance(error, subprocess.CalledProcessError)
                and not isinstance(error.cmd, str)
                else [str(error.cmd)]
                if isinstance(error, subprocess.CalledProcessError)
                else []
            ),
        )

    print(
        f"[startup-link] compiling: {stubs_src.relative_to(repo_root)} "
        f"({optimization_flag})"
    )
    try:
        if args.dev:
            result = compile_source_incremental(
                source=stubs_src,
                repo_root=repo_root,
                object_path=stubs_obj,
                logs_dir=(
                    repo_root
                    / "webclient/client-wasm/build/reports/"
                    "logs-compat-incremental"
                ),
                optimization_flag=optimization_flag,
                force=args.rebuild,
            )
            print(
                f"[startup-link] stubs={result.action} "
                f"reason={result.reason} elapsed_ms={result.elapsed_ms}"
            )
            if not result.ok:
                raise subprocess.CalledProcessError(
                    result.returncode,
                    [str(stubs_src)],
                )
        else:
            compile_source(repo_root, stubs_src, stubs_obj, optimization_flag)
    except (OSError, subprocess.CalledProcessError) as error:
        return fail_startup_build(
            link_dir,
            report_json,
            report_md,
            phase="compile-stubs",
            error=error,
            returncode=(
                error.returncode
                if isinstance(error, subprocess.CalledProcessError)
                else 2
            ),
            cmd=(
                list(error.cmd)
                if isinstance(error, subprocess.CalledProcessError)
                and not isinstance(error.cmd, str)
                else [str(error.cmd)]
                if isinstance(error, subprocess.CalledProcessError)
                else []
            ),
        )

    if args.dev:
        generated_preload_entries = []
    else:
        try:
            generated_preload_entries = build_gdi_font_atlas_preload(repo_root)
        except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
            return fail_startup_build(
                link_dir,
                report_json,
                report_md,
                phase="generate-gdi-font-atlas",
                error=error,
                returncode=(
                    error.returncode
                    if isinstance(error, subprocess.CalledProcessError)
                    else 2
                ),
                cmd=(
                    list(error.cmd)
                    if isinstance(error, subprocess.CalledProcessError)
                    and not isinstance(error.cmd, str)
                    else [str(error.cmd)]
                    if isinstance(error, subprocess.CalledProcessError)
                    else []
                ),
            )

    all_objs = [*tm_objs, entry_obj, stubs_obj]
    rsp_path = link_dir / "startup-objects.rsp"
    write_response_file(
        rsp_path,
        [p.relative_to(repo_root).as_posix() for p in all_objs],
    )

    preload_entries = (
        []
        if args.dev
        else [
            *read_preload_entries(repo_root, preload_manifest),
            *generated_preload_entries,
        ]
    )

    link_cmd = [
        resolve_emxx(),
        link_optimization_flag,
        f"@{rsp_path.relative_to(repo_root)}",
        "--no-entry",
        "--profiling-funcs",
        "-sWASM=1",
        "-sALLOW_MEMORY_GROWTH=1",
        "-sNO_EXIT_RUNTIME=1",
        "-sFORCE_FILESYSTEM=1",
        "-sERROR_ON_UNDEFINED_SYMBOLS=1",
        "-sEMULATE_FUNCTION_POINTER_CASTS=1",
        "-sMIN_WEBGL_VERSION=1",
        "-sMAX_WEBGL_VERSION=2",
        "-lwebsocket.js",
        "-sEXPORTED_FUNCTIONS=['_wyd_start_client','_wyd_boot_client','_wyd_tick_client','_wyd_shutdown_client','_wyd_debug_set_fake_time','_wyd_debug_advance_fake_time','_wyd_debug_clear_fake_time','_wyd_debug_get_time','_wyd_debug_set_demo_camera_offset','_wyd_debug_clear_demo_camera_offset','_wyd_debug_demo_camera_offset_enabled','_wyd_debug_demo_camera_offset_x','_wyd_debug_demo_camera_offset_y','_wyd_debug_demo_camera_offset_z','_wyd_debug_demo_camera_offset_h','_wyd_debug_demo_camera_offset_v','_wyd_debug_camera_valid','_wyd_debug_camera_standalone','_wyd_debug_camera_x','_wyd_debug_camera_y','_wyd_debug_camera_z','_wyd_debug_camera_h','_wyd_debug_camera_v','_wyd_d3d9_set_draw_scope','_wyd_d3d9_clear_draw_scope','_wyd_d3d9_set_draw_label','_wyd_d3d9_trace_set_enabled','_wyd_d3d9_trace_get_enabled','_wyd_d3d9_trace_reset','_wyd_d3d9_trace_clear_probes','_wyd_d3d9_trace_set_probe','_wyd_d3d9_trace_probe_capacity','_wyd_d3d9_trace_probe_enabled','_wyd_d3d9_trace_probe_draw','_wyd_d3d9_trace_probe_result','_wyd_d3d9_trace_probe_hit_count','_wyd_d3d9_trace_probe_first_hit_draw','_wyd_d3d9_trace_probe_first_hit_result','_wyd_d3d9_trace_probe_nearest_hit_draw','_wyd_d3d9_trace_probe_nearest_hit_result','_wyd_d3d9_trace_probe_nearest_zwrite_draw','_wyd_d3d9_trace_probe_nearest_zwrite_result','_wyd_d3d9_trace_top_count','_wyd_d3d9_trace_top_sample','_wyd_d3d9_draw_calls','_wyd_d3d9_primitives','_wyd_d3d9_tex_decode_success','_wyd_d3d9_tex_decode_fail','_wyd_d3d9_tex_uploads','_wyd_d3d9_textured_draws','_wyd_d3d9_tex_alpha_promoted_opaque','_wyd_d3d9_shader_draws_skipped','_wyd_d3d9_vs_unique_shaders','_wyd_d3d9_ps_unique_shaders','_wyd_d3d9_vs_bind_calls','_wyd_d3d9_ps_bind_calls','_wyd_d3d9_draws_with_vs','_wyd_d3d9_draws_with_ps','_wyd_d3d9_active_vs_hash_lo','_wyd_d3d9_active_vs_hash_hi','_wyd_d3d9_active_ps_hash_lo','_wyd_d3d9_active_ps_hash_hi','_wyd_d3d9_vs_top_hash_lo','_wyd_d3d9_vs_top_hash_hi','_wyd_d3d9_vs_top_binds','_wyd_d3d9_vs_top_uses','_wyd_d3d9_vs_top_skips','_wyd_d3d9_vs_top_version','_wyd_d3d9_ps_top_hash_lo','_wyd_d3d9_ps_top_hash_hi','_wyd_d3d9_ps_top_binds','_wyd_d3d9_ps_top_uses','_wyd_d3d9_ps_top_skips','_wyd_d3d9_ps_top_version','_wyd_d3d9_shader_file_open_attempts','_wyd_d3d9_shader_file_open_success','_wyd_d3d9_shader_file_open_fail','_wyd_d3d9_shader_file_open_skinmesh','_wyd_d3d9_shader_file_open_vseffect','_wyd_d3d9_shader_file_open_pseffect','_wyd_d3d9_asset_file_open_attempts','_wyd_d3d9_asset_file_open_success','_wyd_d3d9_asset_file_open_fail','_wyd_d3d9_asset_file_open_fail_mesh','_wyd_d3d9_asset_file_open_fail_env','_wyd_d3d9_asset_file_open_fail_ui','_wyd_d3d9_asset_file_open_fail_texture','_wyd_d3d9_asset_file_open_fail_sound','_wyd_d3d9_asset_file_open_fail_sample_count','_wyd_d3d9_asset_file_open_fail_sample','_wyd_d3d9_asset_path_fallback_attempts','_wyd_d3d9_asset_path_fallback_hits','_wyd_d3d9_asset_path_fallback_or010_hits','_wyd_d3d9_asset_path_fallback_sample_count','_wyd_d3d9_asset_path_fallback_sample','_wyd_d3d9_fvf_xyzrhw_draws','_wyd_d3d9_fvf_weighted_draws','_wyd_d3d9_fvf_tex2plus_draws','_wyd_d3d9_fvf_3d_vertices_total','_wyd_d3d9_fvf_3d_vertices_in_clip','_wyd_d3d9_decl_draw_calls','_wyd_d3d9_decl_skinned_draw_calls','_wyd_d3d9_decl_vertices_total','_wyd_d3d9_decl_vertices_in_clip','_wyd_d3d9_decl_rgba_index_order_draws','_wyd_d3d9_decl_bgra_index_order_draws','_wyd_d3d9_invalid_indexed_draws','_wyd_d3d9_invalid_indices_total','_wyd_d3d9_clip_w_reject_draws','_wyd_d3d9_clip_w_reject_triangles','_wyd_d3d9_clip_w_keep_triangles','_wyd_d3d9_stage1_generated_tci_draws','_wyd_d3d9_stage1_textransform_draws','_wyd_d3d9_stage1_tci0_draws','_wyd_d3d9_stage1_tci1_draws','_wyd_d3d9_stage1_tci_other_draws','_wyd_d3d9_alpha_test_enabled_draws','_wyd_d3d9_alpha_test_disabled_draws','_wyd_d3d9_blend_enabled_draws','_wyd_d3d9_depth_test_disabled_draws','_wyd_d3d9_depth_write_disabled_draws','_wyd_d3d9_depth_write_guard_forced_draws','_wyd_d3d9_lighting_enabled_draws','_wyd_d3d9_fog_enabled_draws','_wyd_d3d9_wireframe_draws','_wyd_d3d9_material_set_calls','_wyd_d3d9_light_set_calls','_wyd_d3d9_light_enable_calls','_wyd_d3d9_lighted_vertices','_wyd_d3d9_directional_lighted_vertices','_wyd_d3d9_point_lighted_vertices','_wyd_d3d9_spot_lighted_vertices','_wyd_d3d9_specular_lighted_vertices','_wyd_d3d9_stage0_color_selectarg1_draws','_wyd_d3d9_stage0_color_modulate_draws','_wyd_d3d9_stage0_alpha_selectarg1_draws','_wyd_d3d9_stage0_alpha_modulate_draws','_wyd_d3d9_cull_none_draws','_wyd_d3d9_cull_cw_draws','_wyd_d3d9_cull_ccw_draws','_wyd_d3d9_cull_mirror_worldview_draws','_wyd_d3d9_cull_frontface_flipped_draws','_wyd_d3d9_gl_error_total','_wyd_d3d9_gl_error_draw_calls','_wyd_d3d9_gl_error_last','_wyd_d3d9_clear_calls','_wyd_d3d9_present_calls','_wyd_d3d9_begin_scene_calls','_wyd_d3d9_end_scene_calls','_wyd_d3d9_last_clear_color','_wyd_d3d9_last_clear_flags','_wyd_d3d9_last_clear_time_ms','_wyd_d3d9_last_present_time_ms','_wyd_d3d9_last_present_blend_enabled','_wyd_d3d9_last_present_depth_enabled','_wyd_d3d9_last_present_depth_write','_wyd_d3d9_last_present_alpha_test','_wyd_d3d9_last_present_src_blend','_wyd_d3d9_last_present_dst_blend','_wyd_d3d9_fvf_top_code','_wyd_d3d9_fvf_top_count','_wyd_d3d9_fvf_depth_write_enabled_top_code','_wyd_d3d9_fvf_depth_write_enabled_top_count','_wyd_d3d9_fvf_depth_write_disabled_top_code','_wyd_d3d9_fvf_depth_write_disabled_top_count','_wyd_d3d9_debug_skip_fvf_draws','_wyd_d3d9_fvf322_draw_primitive_up','_wyd_d3d9_fvf322_draw_indexed_primitive_up','_wyd_d3d9_fvf322_draw_indexed_primitive','_wyd_d3d9_fvf322_with_stage0_texture','_wyd_d3d9_fvf322_without_stage0_texture','_wyd_d3d9_fvf322_screenlike_vertices','_wyd_d3d9_fvf322_screenlike_draws','_wyd_d3d9_fvf322_screenlike_replay_draws','_wyd_d3d9_fvf322_screenlike_replay_suppressed','_wyd_d3d9_texture_draws_sky','_wyd_d3d9_texture_draws_water','_wyd_d3d9_texture_draws_bright','_wyd_d3d9_fvf322_bright_draws','_wyd_d3d9_stage0_colorop8_draws','_wyd_d3d9_stage0_colorop8_with_texture','_wyd_d3d9_stage0_colorop8_without_texture','_wyd_d3d9_stage0_colorop8_pathless_texture','_wyd_d3d9_stage0_colorop8_last_fvf','_wyd_d3d9_stage0_colorop8_last_width','_wyd_d3d9_stage0_colorop8_last_height','_wyd_d3d9_stage0_colorop8_last_path_len','_wyd_d3d9_set_stage0_colorop8_calls','_wyd_d3d9_set_stage0_colorop4_calls','_wyd_d3d9_set_stage0_colorop_last_value','_wyd_d3d9_set_texture_stage0_sky_calls','_wyd_d3d9_set_texture_stage1_sky_calls','_wyd_d3d9_draw_attempts_with_sky_texture','_wyd_d3d9_draw_attempts_with_sky_texture_indexed','_wyd_d3d9_draw_attempts_with_sky_texture_up','_wyd_d3d9_draw_attempts_with_sky_last_fvf','_wyd_d3d9_sky_clip_draws','_wyd_d3d9_sky_clip_last_vertex_count','_wyd_d3d9_sky_clip_last_index_count','_wyd_d3d9_sky_clip_last_stable_w_vertices','_wyd_d3d9_sky_clip_last_negative_w_vertices','_wyd_d3d9_sky_clip_last_near_w_vertices','_wyd_d3d9_sky_clip_last_inside_vertices','_wyd_d3d9_sky_clip_last_large_ndc_vertices','_wyd_d3d9_sky_clip_last_triangle_count','_wyd_d3d9_sky_clip_last_triangles_all_stable_w','_wyd_d3d9_sky_clip_last_triangles_any_unstable_w','_wyd_d3d9_sky_clip_last_triangles_any_outside','_wyd_d3d9_sky_clip_last_min_w','_wyd_d3d9_sky_clip_last_max_w','_wyd_d3d9_sky_clip_last_min_ndc_x','_wyd_d3d9_sky_clip_last_max_ndc_x','_wyd_d3d9_sky_clip_last_min_ndc_y','_wyd_d3d9_sky_clip_last_max_ndc_y','_wyd_d3d9_sky_clip_last_min_ndc_z','_wyd_d3d9_sky_clip_last_max_ndc_z','_wyd_d3d9_mark_next_draw_sky','_wyd_d3d9_fvf322_auto_clipw_draws','_wyd_d3d9_fvf322_auto_clipw_reject_draws','_wyd_d3d9_fvf530_auto_clipw_draws','_wyd_d3d9_fvf530_auto_clipw_reject_draws','_wyd_d3d9_fvf530_draws','_wyd_d3d9_fvf530_large_bounds_draws','_wyd_d3d9_fvf530_large_bounds_skip_draws','_wyd_d3d9_fvf530_large_bound_sample_count','_wyd_d3d9_fvf530_large_bound_sample','_wyd_d3d9_fvf530_unstable_w_draws','_wyd_d3d9_fvf530_vertices','_wyd_d3d9_fvf530_inside_vertices','_wyd_d3d9_fvf530_last_vertex_count','_wyd_d3d9_fvf530_last_index_count','_wyd_d3d9_fvf530_last_stable_w_vertices','_wyd_d3d9_fvf530_last_inside_vertices','_wyd_d3d9_fvf530_last_large_ndc_vertices','_wyd_d3d9_fvf530_last_min_ndc_x','_wyd_d3d9_fvf530_last_max_ndc_x','_wyd_d3d9_fvf530_last_min_ndc_y','_wyd_d3d9_fvf530_last_max_ndc_y','_wyd_d3d9_fvf530_last_min_ndc_z','_wyd_d3d9_fvf530_last_max_ndc_z','_wyd_d3d9_fvf594_auto_clipw_draws','_wyd_d3d9_fvf594_auto_clipw_reject_draws','_wyd_d3d9_up_reset_stream0_calls','_wyd_d3d9_up_reset_indices_calls','_wyd_d3d9_set_debug_flags','_wyd_d3d9_get_debug_flags','_wyd_d3d9_set_debug_skip_fvf','_wyd_d3d9_get_debug_skip_fvf','_wyd_d3d9_reset_debug_counters','_wyd_sky_render_calls','_wyd_sky_hidden_returns','_wyd_sky_eligible_calls','_wyd_sky_branch_skipped','_wyd_sky_mesh_null','_wyd_sky_mesh_draws','_wyd_sky_last_dungeon','_wyd_sky_last_state','_wyd_sky_last_texture_index','_wyd_sky_last_mesh_texture_index','_wyd_sky_last_mesh_has_vb','_wyd_sky_last_mesh_has_ib','_wyd_sky_last_mesh_fvf','_wyd_sky_last_mesh_att_count','_wyd_sky_last_mesh_face_count','_wyd_sky_last_mesh_vertex_count','_wyd_sky_last_mesh_render_result','_wyd_sky_reset_debug_counters','_wyd_font2_set_text_calls','_wyd_font2_set_text_nonempty','_wyd_font2_render_calls','_wyd_font2_render_nonempty','_wyd_font2_texture_create_fail','_wyd_font2_lock_calls','_wyd_font2_last_text_len','_wyd_font2_last_line_count','_wyd_font2_last_size0','_wyd_font2_last_size1','_wyd_font2_last_size2','_wyd_font2_last_alpha_pixels','_wyd_font2_last_has_bitmap','_wyd_font2_last_render_x','_wyd_font2_last_render_y','_wyd_font2_last_render_type','_wyd_font2_last_text','_wyd_get_game_state','_wyd_set_game_state']",
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','HEAPU8']",
        *[f"--preload-file={entry}" for entry in preload_entries],
        "-o",
        str(out_js.relative_to(repo_root)),
    ]

    extra_exports = [
        "_malloc",
        "_free",
        "_wyd_render_client",
        "_wyd_configure_optimized_view",
        "_wyd_optimized_view_enabled",
        "_wyd_optimized_quality_profile",
        "_wyd_optimized_css_width",
        "_wyd_optimized_css_height",
        "_wyd_optimized_backing_width",
        "_wyd_optimized_backing_height",
        "_wyd_optimized_ui_scale_percent",
        "_wyd_optimized_ui_scale",
        "_wyd_optimized_world_scale",
        "_wyd_d3d9_is_webgl2",
        "_wyd_d3d9_optimized_world_samples",
        "_wyd_lab_load_scenario",
        "_wyd_lab_show",
        "_wyd_lab_is_enabled",
        "_wyd_lab_is_pending",
        "_wyd_lab_last_result",
        "_wyd_lab_current_frame",
        "_wyd_lab_clock_ms",
        "_wyd_lab_packet_hash",
        "_wyd_lab_scenario_hash",
        "_wyd_lab_scene_type",
        "_wyd_lab_screen_width",
        "_wyd_lab_screen_height",
        "_wyd_lab_player_x",
        "_wyd_lab_player_y",
        "_wyd_lab_player_height",
        "_wyd_lab_player_visible",
        "_wyd_lab_player_hidden",
        "_wyd_lab_player_has_skin",
        "_wyd_lab_player_familiar_item",
        "_wyd_lab_player_has_familiar",
        "_wyd_lab_player_familiar_visible",
        "_wyd_lab_player_familiar_has_skin",
        "_wyd_lab_player_familiar_visibility_reason",
        "_wyd_lab_player_class",
        "_wyd_lab_player_motion",
        "_wyd_lab_player_skin_type",
        "_wyd_lab_player_speed",
        "_wyd_lab_player_progress",
        "_wyd_lab_player_moving",
        "_wyd_lab_player_last_route",
        "_wyd_lab_player_max_route",
        "_wyd_lab_player_move_started_ms",
        "_wyd_lab_player_animation_started_ms",
        "_wyd_lab_player_animation_index",
        "_wyd_lab_player_animation_last_index",
        "_wyd_lab_player_skin_fps",
        "_wyd_lab_player_skin_offset",
        "_wyd_lab_player_skin_start_offset",
        "_wyd_lab_player_skin_tick_last",
        "_wyd_lab_player_skin_animation_base",
        "_wyd_lab_player_pose_hash",
        "_wyd_lab_render_fps",
        "_wyd_lab_camera_x",
        "_wyd_lab_camera_y",
        "_wyd_lab_camera_z",
        "_wyd_lab_camera_horizon",
        "_wyd_lab_camera_vertical",
        "_wyd_lab_camera_length",
        "_wyd_lab_camera_height",
        "_wyd_lab_status",
        "_wyd_compare_latch_3d_state",
        "_wyd_compare_3d_state_sequence",
        "_wyd_compare_3d_state_valid",
        "_wyd_compare_3d_state_frame_serial",
        "_wyd_compare_3d_state_draw_serial",
        "_wyd_compare_3d_state_matrices",
        "_wyd_compare_3d_state_matrix_value",
        "_wyd_font2_last_nonempty_alpha_pixels",
        "_wyd_font2_last_nonempty_has_bitmap",
        "_wyd_font2_last_nonempty_size0",
        "_wyd_font2_max_alpha_pixels",
        "_wyd_font2_max_size0",
        "_wyd_font2_last_nonempty_render_x",
        "_wyd_font2_last_nonempty_render_y",
        "_wyd_font2_last_nonempty_render_type",
        "_wyd_font2_last_nonempty_text",
        "_wyd_d3d9_draw_order_first_sky",
        "_wyd_d3d9_draw_order_first_skin",
        "_wyd_d3d9_draw_order_first_terrain594",
        "_wyd_d3d9_draw_order_first_water578",
        "_wyd_d3d9_draw_order_first_fvf530",
        "_wyd_d3d9_draw_order_first_fvf322",
        "_wyd_d3d9_draw_order_count_sky",
        "_wyd_d3d9_draw_order_count_skin",
        "_wyd_d3d9_draw_order_count_terrain594",
        "_wyd_d3d9_draw_order_count_water578",
        "_wyd_d3d9_draw_order_count_fvf530",
        "_wyd_d3d9_draw_order_count_fvf322",
        "_wyd_d3d9_vegetation_alpha_mask_draws",
        "_wyd_d3d9_vegetation_alpha_blend_draws",
        "_wyd_d3d9_vegetation_draws",
        "_wyd_d3d9_last_clear_z",
        "_wyd_d3d9_current_z_func",
        "_wyd_d3d9_draw_order_frame_first_sky",
        "_wyd_d3d9_draw_order_frame_first_skin",
        "_wyd_d3d9_draw_order_frame_first_terrain594",
        "_wyd_d3d9_draw_order_frame_first_water578",
        "_wyd_d3d9_draw_order_frame_first_fvf530",
        "_wyd_d3d9_draw_order_frame_first_fvf322",
        "_wyd_d3d9_draw_order_frame_count_sky",
        "_wyd_d3d9_draw_order_frame_count_skin",
        "_wyd_d3d9_draw_order_frame_count_terrain594",
        "_wyd_d3d9_draw_order_frame_count_water578",
        "_wyd_d3d9_draw_order_frame_count_fvf530",
        "_wyd_d3d9_draw_order_frame_count_fvf322",
        "_wyd_d3d9_water_stage1_disable_draws",
        "_wyd_d3d9_water_stage1_modulate_draws",
        "_wyd_d3d9_water_stage1_modulate2x_draws",
        "_wyd_d3d9_water_stage1_add_draws",
        "_wyd_d3d9_water_blend_enabled_draws",
        "_wyd_d3d9_water_blend_disabled_draws",
        "_wyd_d3d9_water_depth_write_disabled_draws",
        "_wyd_d3d9_water_fog_disabled_draws",
        "_wyd_d3d9_fvf322_class_count",
        "_wyd_d3d9_fvf322_class_max",
        "_wyd_d3d9_fvf322_class_name",
        "_wyd_d3d9_fvf322_requested_depth_write_enabled",
        "_wyd_d3d9_fvf322_requested_depth_write_disabled",
        "_wyd_d3d9_fvf322_forced_depth_write_disabled",
        "_wyd_d3d9_fvf322_requested_depth_write_enabled_class",
        "_wyd_d3d9_fvf322_requested_depth_write_disabled_class",
        "_wyd_d3d9_clipw_empty_signature_count",
        "_wyd_d3d9_clipw_empty_signature_sample",
        "_wyd_d3d9_skin_suspicious_texture_draws",
        "_wyd_d3d9_skin_suspicious_texture_sample_count",
        "_wyd_d3d9_skin_suspicious_texture_sample",
        "_wyd_d3d9_terrain_stage1_modulate_draws",
        "_wyd_d3d9_terrain_stage1_modulate2x_draws",
        "_wyd_d3d9_terrain_stage1_disable_draws",
        "_wyd_d3d9_fvf322_lightmap_heuristic_draws",
        "_wyd_selserver_set_demo_type_override",
        "_wyd_cursor_visible",
        "_wyd_selserver_demo_type",
        "_wyd_selserver_start_run",
        "_wyd_selserver_demo_elapsed",
        "_wyd_selserver_human_version",
        "_wyd_selserver_human_count",
        "_wyd_selserver_human_present",
        "_wyd_selserver_human_pos_x",
        "_wyd_selserver_human_pos_y",
        "_wyd_selserver_human_want_height",
        "_wyd_selserver_human_height",
        "_wyd_selserver_human_ground_mask",
        "_wyd_selserver_human_ground_height",
        "_wyd_selserver_human_skin_x",
        "_wyd_selserver_human_skin_y",
        "_wyd_selserver_human_skin_z",
        "_wyd_selserver_human_skin_mesh_type",
        "_wyd_selserver_human_obj_type",
        "_wyd_selserver_human_visible",
        "_wyd_selserver_human_mount",
        "_wyd_selserver_human_mount_skin_mesh_type",
        "_wyd_selserver_human_motion",
        "_wyd_selserver_human_sent_motion",
        "_wyd_selserver_human_loop",
        "_wyd_selserver_human_skin_ani",
        "_wyd_selserver_human_skin_fps",
        "_wyd_selserver_human_skin_offset",
        "_wyd_selserver_human_skin_bone_ani",
        "_wyd_selserver_human_skin_base_mat",
        "_wyd_selserver_human_skin_ani_base_index",
        "_wyd_selserver_human_skin_ani_cut",
        "_wyd_selserver_human_skin_generated",
        "_wyd_selserver_human_skin_frame_meshes",
        "_wyd_selserver_human_mount_present",
        "_wyd_selserver_human_mount_ani",
        "_wyd_selserver_human_mount_fps",
        "_wyd_selserver_human_mount_offset",
        "_wyd_selserver_human_mount_ani_cut",
        "_wyd_selserver_human_mount_generated",
        "_wyd_selserver_human_mount_frame_meshes",
        "_wyd_selserver_human_weapon_type_index",
        "_wyd_selserver_human_head_index",
        "_wyd_selserver_human_body_current_table",
        "_wyd_selserver_human_body_current_resolved_clip",
        "_wyd_selserver_human_body_mounted_current_table",
        "_wyd_selserver_human_body_mounted_current_resolved_clip",
        "_wyd_selserver_human_body_seating_table",
        "_wyd_selserver_human_body_seating_resolved_clip",
        "_wyd_selserver_human_body_mounted_seating_table",
        "_wyd_selserver_human_body_mounted_seating_resolved_clip",
        "_wyd_selserver_human_demo_ani",
        "_wyd_selserver_human_moving",
        "_wyd_selserver_human_progress_rate",
        "_wyd_selserver_human_max_speed",
        "_wyd_selserver_human_sliding",
        "_wyd_selserver_human_last_route_index",
        "_wyd_selserver_human_max_route_index",
        "_wyd_selserver_human_target_x",
        "_wyd_selserver_human_target_y",
        "_wyd_selserver_human_delta_x",
        "_wyd_selserver_human_delta_y",
        "_wyd_selserver_set_animation_version",
        "_wyd_selserver_set_animation_count",
        "_wyd_selserver_set_animation_attack_count",
        "_wyd_selserver_set_animation_last_motion",
        "_wyd_selserver_set_animation_last_loop",
        "_wyd_selserver_set_animation_last_skin_mesh_type",
        "_wyd_selserver_set_animation_last_weapon_type_index",
        "_wyd_selserver_set_animation_last_mount",
        "_wyd_selserver_set_animation_last_mount_present",
        "_wyd_selserver_set_animation_last_route_index",
        "_wyd_selserver_set_animation_last_max_route_index",
        "_wyd_selserver_move_packet_version",
        "_wyd_selserver_human_route_out_count",
        "_wyd_selserver_human_packet_in_count",
        "_wyd_selserver_human_route_out_speed",
        "_wyd_selserver_human_route_out_target_x",
        "_wyd_selserver_human_route_out_target_y",
        "_wyd_selserver_human_route_out_route_len",
        "_wyd_selserver_human_packet_in_speed",
        "_wyd_selserver_human_packet_in_target_x",
        "_wyd_selserver_human_packet_in_target_y",
        "_wyd_selserver_human_packet_before_speed",
        "_wyd_selserver_human_packet_after_speed",
        "_wyd_d3d9_effect_draws",
        "_wyd_d3d9_fvf322_effect_draws",
        "_wyd_d3d9_fog_enabled",
        "_wyd_d3d9_fog_start",
        "_wyd_d3d9_fog_end",
        "_wyd_d3d9_fog_color",
        "_wyd_d3d9_last_present_fog",
        "_wyd_d3d9_fog_skin_draws",
        "_wyd_debug_get_weather_mode",
        "_wyd_debug_set_weather_mode",
        "_wyd_selchar_initialized",
        "_wyd_selchar_char_count",
        "_wyd_selchar_human_present",
        "_wyd_selchar_name",
        "_wyd_selchar_sample_present",
        "_wyd_selchar_sample_skin_present",
        "_wyd_selchar_sample_visible",
        "_wyd_selchar_sample_x",
        "_wyd_selchar_sample_y",
        "_wyd_selchar_sample_height",
        "_wyd_selchar_sample_animation",
        "_wyd_selchar_sample_mesh_type",
        "_wyd_selchar_sample_mesh_generated",
        "_wyd_selchar_sample_frame_meshes",
        "_wyd_selchar_sample_bone_animation",
        "_wyd_selchar_sample_look_mesh",
        "_wyd_selchar_sample_look_skin",
        "_wyd_selchar_skin_restore_calls",
        "_wyd_selchar_skin_restore_loads",
        "_wyd_selchar_skin_restore_parents",
        "_wyd_selchar_skin_restore_last",
        "_wyd_skin_animation_num_parts",
        "_wyd_skin_animation_num_bones",
        "_wyd_serverlist_entry",
        "_wyd_get_scene_type",
        "_wyd_state_is_placeholder",
        "_wyd_get_state_debug_label",
        "_wyd_get_state_name",
        "_wyd_debug_camera_sight_length",
        "_wyd_debug_camera_want_length",
        "_wyd_mouse_event",
        "_wyd_key_event",
        "_wyd_text_input_active",
        "_wyd_text_input_value",
        "_wyd_input_key_event_count",
        "_wyd_input_key_last_msg",
        "_wyd_input_key_last_key",
        "_wyd_input_mouse_x",
        "_wyd_input_mouse_y",
        "_wyd_input_mouse_left_down",
        "_wyd_input_mouse_right_down",
        "_wyd_input_mouse_middle_down",
        "_wyd_input_mouse_event_count",
        "_wyd_input_mouse_last_msg",
        "_wyd_input_mouse_last_wparam",
        "_wyd_control_last_event_id",
        "_wyd_control_last_event_type",
        "_wyd_control_event_count",
        "_wyd_control_last_mouse_processed_id",
        "_wyd_control_last_mouse_processed_flags",
        "_wyd_control_last_mouse_processed_type",
        "_wyd_control_last_mouse_processed_x",
        "_wyd_control_last_mouse_processed_y",
        "_wyd_control_exists",
        "_wyd_control_visible",
        "_wyd_control_enabled",
        "_wyd_control_select_enabled",
        "_wyd_control_selected",
        "_wyd_control_pressed",
        "_wyd_control_type",
        "_wyd_control_abs_x",
        "_wyd_control_abs_y",
        "_wyd_control_width",
        "_wyd_control_height",
        "_wyd_control_visible_text_count",
        "_wyd_control_visible_text_id",
        "_wyd_control_visible_text_type",
        "_wyd_control_visible_text_x",
        "_wyd_control_visible_text_y",
        "_wyd_control_visible_text_width",
        "_wyd_control_visible_text_height",
        "_wyd_control_visible_text_color",
        "_wyd_control_visible_text_value",
        "_wyd_set_field_mode",
        "_wyd_get_field_mode",
        "_wyd_field_debug_fixture_used",
        "_wyd_field_initialized",
        "_wyd_field_has_ground",
        "_wyd_field_has_my_human",
        "_wyd_field_critical_error",
        "_wyd_field_message_box_visible",
        "_wyd_field_message_box_message",
        "_wyd_field_map_x",
        "_wyd_field_map_y",
        "_wyd_field_myhuman_x",
        "_wyd_field_myhuman_y",
        "_wyd_field_myhuman_id",
        "_wyd_field_myhuman_name",
        "_wyd_field_myhuman_hp",
        "_wyd_field_myhuman_max_hp",
        "_wyd_field_myhuman_class_id",
        "_wyd_field_myhuman_attack_dest_id",
        "_wyd_field_myhuman_title_progress_visible",
        "_wyd_field_mouse_over_human_id",
        "_wyd_field_visible_human_count",
        "_wyd_field_visible_human_total",
        "_wyd_field_visible_human_limit",
        "_wyd_field_visible_human_id",
        "_wyd_field_visible_human_x",
        "_wyd_field_visible_human_y",
        "_wyd_field_visible_human_hp",
        "_wyd_field_visible_human_max_hp",
        "_wyd_field_visible_human_motion",
        "_wyd_field_visible_human_class_id",
        "_wyd_field_visible_human_title_progress_visible",
        "_wyd_field_myhuman_motion",
        "_wyd_field_myhuman_sent_motion",
        "_wyd_field_myhuman_moving",
        "_wyd_field_myhuman_progress_rate",
        "_wyd_field_myhuman_last_route_index",
        "_wyd_field_myhuman_max_route_index",
        "_wyd_field_myhuman_target_x",
        "_wyd_field_myhuman_target_y",
        "_wyd_field_myhuman_move_to_x",
        "_wyd_field_myhuman_move_to_y",
        "_wyd_field_pick_at",
        "_wyd_field_last_pick_valid",
        "_wyd_field_last_pick_x",
        "_wyd_field_last_pick_y",
        "_wyd_field_last_pick_z",
        "_wyd_field_ground_mask_at",
        "_wyd_field_ground_height_at",
        "_wyd_field_ground_water_at",
        "_wyd_field_weather_active",
        "_wyd_field_rain_visible",
        "_wyd_field_snow_visible",
        "_wyd_field_snow2_visible",
        "_wyd_field_visual_total_draws",
        "_wyd_field_visual_terrain_draws",
        "_wyd_field_visual_ground_draws",
        "_wyd_field_visual_water_draws",
        "_wyd_field_visual_sky_draws",
        "_wyd_field_visual_human_draws",
        "_wyd_field_visual_object_draws",
        "_wyd_field_visual_effect_draws",
        "_wyd_field_visual_hud_draws",
        "_wyd_field_visual_hud_art_draws",
        "_wyd_field_visual_reset",
        "_wyd_field_visual_tex_bucket_env",
        "_wyd_field_visual_tex_bucket_effect",
        "_wyd_field_visual_tex_bucket_ui",
        "_wyd_field_visual_tex_bucket_char",
        "_wyd_field_visual_tex_bucket_sky",
        "_wyd_field_visual_tex_bucket_water",
        "_wyd_field_visual_tex_bucket_other",
        "_wyd_field_visual_fvf_bucket_code",
        "_wyd_field_visual_fvf_bucket_count",
        "_wyd_field_visual_fvf_bucket_size",
        "_wyd_field_ground_height_under_player",
        "_wyd_field_myhuman_height",
        "_wyd_field_myhuman_want_height",
        "_wyd_field_ground_mask_under_player",
        "_wyd_field_myhuman_height_delta",
        "_wyd_field_ground_normal_under_player_x",
        "_wyd_field_ground_normal_under_player_y",
        "_wyd_field_ground_normal_under_player_z",
        "_wyd_debug_last_critical_type",
        "_wyd_debug_last_critical_id",
        "_wyd_debug_last_critical_mesh",
        "_wyd_debug_last_critical_x",
        "_wyd_debug_last_critical_y",
        "_wyd_debug_last_critical_mob_x",
        "_wyd_debug_last_critical_mob_y",
        "_wyd_debug_selectserver_login",
        "_wyd_public_demo_unlock_select_character",
        "_wyd_socket_last_host",
        "_wyd_socket_last_proxy_url",
        "_wyd_socket_last_port",
        "_wyd_socket_last_connect_result",
        "_wyd_socket_last_error",
        "_wyd_socket_bytes_sent",
        "_wyd_socket_bytes_received",
        "_wyd_socket_last_sent_opcode",
        "_wyd_socket_last_recv_opcode",
        "_wyd_socket_wasm_message_callbacks",
        "_wyd_socket_wasm_select_post_attempts",
        "_wyd_socket_wasm_select_post_success",
        "_wyd_socket_wasm_last_select_event",
        "_wyd_socket_wasm_async_message",
        "_wyd_socket_wasm_async_events",
        "_wyd_socket_wasm_recv_buffered",
        "_wyd_socket_wasm_read_notification_pending",
        "_wyd_compare_random_arm",
        "_wyd_compare_random_disarm",
        "_wyd_compare_random_is_armed",
        "_wyd_compare_random_configured_seed",
        "_wyd_compare_random_state",
        "_wyd_compare_random_rand_calls",
        "_wyd_compare_random_srand_calls",
        "_wyd_compare_random_last_requested_seed",
        "_wyd_compare_random_next_for_test",
        "_wyd_compare_random_srand_for_test",
        "_wyd_compare_present_state_sequence",
        "_wyd_compare_present_game_state_valid",
        "_wyd_compare_present_game_state",
        "_wyd_compare_present_scene_type_valid",
        "_wyd_compare_present_scene_type",
        "_wyd_field_object_count",
        "_wyd_field_static_object_draws",
        "_wyd_field_object_failed",
        "_wyd_field_object_checksum_failed",
        "_wyd_field_object_sea_count",
        "_wyd_field_object_tree_count",
        "_wyd_field_object_house_count",
        "_wyd_field_object_light_count",
        "_wyd_field_object_generic_count",
        "_wyd_field_object_last_mask_index",
        "_wyd_d3d9_indexed_compact_draws",
        "_wyd_d3d9_indexed_compact_source_vertices",
        "_wyd_d3d9_indexed_compact_vertices_decoded",
        "_wyd_d3d9_indexed_compact_vertices_saved",
        "_wyd_d3d9_decl_indexed_compact_draws",
        "_wyd_d3d9_decl_indexed_compact_source_vertices",
        "_wyd_d3d9_decl_indexed_compact_vertices_decoded",
        "_wyd_d3d9_decl_indexed_compact_vertices_saved",
        "_wyd_d3d9_fvf594_auto_clipw_draws",
        "_wyd_d3d9_fvf594_auto_clipw_reject_draws",
        "_wyd_d3d9_fvf530_auto_clipw_draws",
        "_wyd_d3d9_fvf530_auto_clipw_reject_draws",
        "_wyd_d3d9_fvf530_indexed_compact_draws",
        "_wyd_d3d9_fvf530_indexed_compact_source_vertices",
        "_wyd_d3d9_fvf530_indexed_compact_vertices_decoded",
        "_wyd_d3d9_fvf530_indexed_compact_vertices_saved",
        "_wyd_d3d9_fvf322_auto_clipw_draws",
        "_wyd_d3d9_fvf322_auto_clipw_reject_draws",
        "_wyd_d3d9_depth_write_guard_forced_draws",
        "_wyd_d3d9_depth_write_disabled_draws",
        "_wyd_d3d9_clip_w_reject_draws",
        "_wyd_d3d9_clip_w_reject_triangles",
        "_wyd_d3d9_clip_w_keep_triangles",
        "_wyd_sun_render_calls",
        "_wyd_sun_hidden_returns_bg",
        "_wyd_sun_hidden_returns_self",
        "_wyd_sun_out_of_viewport",
        "_wyd_sun_flare_draws",
        "_wyd_sun_last_hide",
        "_wyd_sun_last_def_size",
        "_wyd_sun_last_in_viewport",
        "_wyd_sun_last_flare_count",
        "_wyd_sun_reset_debug_counters",
        "_wyd_sun_last_screen_x",
        "_wyd_sun_last_screen_y",
        "_wyd_sun_last_screen_z",
        "_wyd_audio_resume",
        "_wyd_audio_buffers_created",
        "_wyd_audio_uploads",
        "_wyd_audio_play_calls",
        "_wyd_audio_stop_calls",
        "_wyd_audio_music_play_calls",
        "_wyd_audio_music_stop_calls",
        "_wyd_audio_music_state",
        "_wyd_audio_get_music_volume",
        "_wyd_d3d9_set_detailed_telemetry",
        "_wyd_d3d9_get_detailed_telemetry",
    ]
    for index, arg in enumerate(link_cmd):
        if arg.startswith("-sEXPORTED_FUNCTIONS=["):
            link_cmd[index] = arg[:-1] + "," + ",".join(repr(name) for name in extra_exports) + "]"
            break

    link_rsp_path = link_dir / "startup-link.rsp.utf-8"
    write_response_file(link_rsp_path, link_cmd[1:])
    invoke_cmd = [
        link_cmd[0],
        f"@{link_rsp_path.relative_to(repo_root)}",
    ]
    dev_link_signature: dict[str, Any] | None = None
    if args.dev:
        dev_link_signature = incremental_link_signature(
            link_cmd,
            all_objs,
        )
        if (
            not args.rebuild
            and reusable_incremental_code(
                link_dir,
                dev_link_signature,
            )
        ):
            if staging_context is not None:
                staging_context.cleanup()
            write_reports(
                report_json,
                report_md,
                True,
                0,
                link_cmd,
                Counter(),
                phase="up-to-date",
            )
            print("[startup-link] code=up-to-date; link skipped")
            print("[startup-link] undefined_total=0 unique=0")
            return 0

    link_contract_before: dict[str, Any] | None = None
    if not args.dev:
        try:
            link_contract_before = capture_startup_link_contract(
                repo_root=repo_root,
                obj_root=obj_root,
                optimization_flag=optimization_flag,
                tm_objects=tm_objs,
                entry_src=entry_src,
                stubs_src=stubs_src,
                entry_obj=entry_obj,
                stubs_obj=stubs_obj,
                preload_manifest=preload_manifest,
                preload_entries=preload_entries,
                link_cmd=link_cmd,
                response_files=(rsp_path, link_rsp_path),
                generated_preload_entries=generated_preload_entries,
            )
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            return fail_startup_build(
                link_dir,
                report_json,
                report_md,
                phase="link-input-contract-before-link",
                error=error,
                cmd=link_cmd,
            )

    print("[startup-link] linking strict artifact")
    try:
        proc = subprocess.run(
            invoke_cmd,
            cwd=repo_root,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except OSError as error:
        stderr_path.write_text(str(error) + "\n", encoding="utf-8")
        return fail_startup_build(
            link_dir,
            report_json,
            report_md,
            phase="link-invocation",
            error=error,
            cmd=invoke_cmd,
        )

    stdout_path.write_text(proc.stdout or "", encoding="utf-8")
    stderr_path.write_text(proc.stderr or "", encoding="utf-8")

    undef = parse_undefined(proc.stderr or "")
    if proc.returncode != 0:
        return fail_startup_build(
            link_dir,
            report_json,
            report_md,
            phase="link",
            error=f"em++ returned {proc.returncode}",
            returncode=proc.returncode,
            cmd=link_cmd,
            undef=undef,
        )

    missing_outputs = [
        output.name
        for output in (out_js, out_js.with_suffix(".wasm"))
        if not output.is_file()
    ]
    if missing_outputs:
        return fail_startup_build(
            link_dir,
            report_json,
            report_md,
            phase="link-output-validation",
            error=(
                "successful linker invocation did not produce: "
                + ", ".join(missing_outputs)
            ),
            cmd=link_cmd,
            undef=undef,
        )

    input_contract_report: dict[str, Any] | None = None
    if args.dev:
        assert dev_link_signature is not None
        try:
            published_js, published_wasm = publish_incremental_code(
                build_output_dir,
                link_dir,
                build_name,
                dev_link_signature,
            )
        except OSError as error:
            return fail_startup_build(
                link_dir,
                report_json,
                report_md,
                phase="publish-incremental-code",
                error=error,
                cmd=link_cmd,
                undef=undef,
            )
        if staging_context is not None:
            staging_context.cleanup()
        print(
            "[startup-link] published="
            f"{published_js.name},{published_wasm.name}"
        )
    else:
        assert link_contract_before is not None
        try:
            link_contract_after = capture_startup_link_contract(
                repo_root=repo_root,
                obj_root=obj_root,
                optimization_flag=optimization_flag,
                tm_objects=tm_objs,
                entry_src=entry_src,
                stubs_src=stubs_src,
                entry_obj=entry_obj,
                stubs_obj=stubs_obj,
                preload_manifest=preload_manifest,
                preload_entries=preload_entries,
                link_cmd=link_cmd,
                response_files=(rsp_path, link_rsp_path),
                generated_preload_entries=generated_preload_entries,
            )
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            return fail_startup_build(
                link_dir,
                report_json,
                report_md,
                phase="link-input-contract-changed",
                error=f"post-link input revalidation failed: {error}",
                cmd=link_cmd,
                undef=undef,
                input_contract={
                    "schema": STARTUP_LINK_CONTRACT_SCHEMA,
                    "schema_version": STARTUP_LINK_CONTRACT_VERSION,
                    "before_fingerprint": (
                        link_contract_before["fingerprint"]
                    ),
                    "after_fingerprint": None,
                    "unchanged": False,
                },
            )

        link_contract_unchanged = (
            link_contract_after == link_contract_before
        )
        input_contract_report = {
            "schema": STARTUP_LINK_CONTRACT_SCHEMA,
            "schema_version": STARTUP_LINK_CONTRACT_VERSION,
            "before_fingerprint": link_contract_before["fingerprint"],
            "after_fingerprint": link_contract_after["fingerprint"],
            "unchanged": link_contract_unchanged,
        }
        if not link_contract_unchanged:
            return fail_startup_build(
                link_dir,
                report_json,
                report_md,
                phase="link-input-contract-changed",
                error=(
                    "startup link source, toolchain, object, preload, or "
                    "argument contract changed while em++ was linking"
                ),
                cmd=link_cmd,
                undef=undef,
                input_contract=input_contract_report,
            )

    write_reports(
        report_json,
        report_md,
        True,
        proc.returncode,
        link_cmd,
        undef,
        phase="complete",
        input_contract=input_contract_report,
    )

    print(f"[startup-link] returncode={proc.returncode}")
    print(f"[startup-link] undefined_total={sum(undef.values())} unique={len(undef)}")
    print(f"[startup-link] report_json={report_json.relative_to(repo_root)}")
    print(f"[startup-link] report_md={report_md.relative_to(repo_root)}")

    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
