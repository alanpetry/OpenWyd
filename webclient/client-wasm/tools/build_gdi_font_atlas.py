#!/usr/bin/env python3
"""Compile and run the Win32 GDI atlas generator used by the WASM build."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path
from typing import Any


ATLAS_NAME = "openwyd_gdi_tahoma12_a4.bin"
MANIFEST_NAME = "openwyd_gdi_tahoma12_a4.json"
BUILD_CONTRACT_SCHEMA = "openwyd.gdi-font-atlas-build-contract"
BUILD_CONTRACT_VERSION = 1
COMPILE_FLAGS = (
    "/nologo",
    "/std:c++17",
    "/EHsc",
    "/O2",
    "/MT",
    "/utf-8",
)


def _version_key(path: Path) -> tuple[int, ...]:
    try:
        return tuple(int(part) for part in path.name.split("."))
    except ValueError:
        return ()


def _latest_directory(parent: Path, predicate) -> Path:
    candidates = sorted(
        (item for item in parent.iterdir() if item.is_dir() and predicate(item)),
        key=_version_key,
        reverse=True,
    )
    if not candidates:
        raise RuntimeError(f"no compatible toolchain directory below {parent}")
    return candidates[0]


def discover_toolchain(tools_root: Path) -> dict[str, Path]:
    portable = tools_root / "portable-msvc-v142-x86/msvc"
    msvc = _latest_directory(
        portable / "VC/Tools/MSVC",
        lambda item: item.name.startswith("14.29."),
    )
    windows_kit = portable / "Windows Kits/10"
    sdk = _latest_directory(
        windows_kit / "Include",
        lambda item: (
            (item / "shared/sdkddkver.h").is_file()
            and (
                windows_kit
                / "Lib"
                / item.name
                / "um/x86/gdi32.lib"
            ).is_file()
        ),
    )
    paths = {
        "compiler": msvc / "bin/Hostx64/x86/cl.exe",
        "compiler_bin": msvc / "bin/Hostx64/x86",
        "msvc_include": msvc / "include",
        "msvc_lib": msvc / "lib/x86",
        "sdk_root": windows_kit,
        "sdk_include": windows_kit / "Include" / sdk.name,
        "sdk_lib": windows_kit / "Lib" / sdk.name,
        "sdk_bin": windows_kit / "bin" / sdk.name / "x64",
    }
    missing = [str(path) for path in paths.values() if not path.exists()]
    if missing:
        raise RuntimeError("GDI atlas toolchain path missing: " + ", ".join(missing))
    return paths


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _contract_path(path: Path, repo_root: Path, tools_root: Path) -> str:
    resolved = path.resolve()
    for root, token in (
        (repo_root.resolve(), "<REPO_ROOT>"),
        (tools_root.resolve(), "<TOOLS_ROOT>"),
    ):
        try:
            relative = resolved.relative_to(root)
            return f"{token}/{relative.as_posix()}"
        except ValueError:
            pass
    return resolved.as_posix()


def _compiler_identity(
    compiler: Path,
    repo_root: Path,
    tools_root: Path,
) -> dict[str, Any]:
    process = subprocess.run(
        [str(compiler), "/Bv"],
        cwd=repo_root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    output = (process.stdout or "") + (process.stderr or "")
    output = output.replace(str(repo_root), "<REPO_ROOT>")
    output = output.replace(str(tools_root), "<TOOLS_ROOT>")
    return {
        "returncode": process.returncode,
        "outputSha256": hashlib.sha256(
            output.encode("utf-8", errors="replace")
        ).hexdigest(),
    }


def capture_build_contract(
    repo_root: Path,
    tools_root: Path,
) -> dict[str, Any]:
    repo_root = repo_root.resolve()
    tools_root = tools_root.resolve()
    toolchain = discover_toolchain(tools_root)
    source = (
        repo_root
        / "webclient/client-wasm/tools/generate_gdi_font_atlas.cpp"
    )
    files: list[tuple[str, Path]] = [
        ("generator-source", source),
        ("generator-build-script", Path(__file__).resolve()),
        ("msvc-cl", toolchain["compiler"]),
        ("msvc-c1xx", toolchain["compiler_bin"] / "c1xx.dll"),
        ("msvc-c2", toolchain["compiler_bin"] / "c2.dll"),
        ("msvc-link", toolchain["compiler_bin"] / "link.exe"),
        ("windows-sdk-windows-h", toolchain["sdk_include"] / "um/Windows.h"),
        ("windows-sdk-wingdi-h", toolchain["sdk_include"] / "um/wingdi.h"),
        (
            "windows-sdk-bcrypt-h",
            toolchain["sdk_include"] / "shared/bcrypt.h",
        ),
        ("windows-sdk-gdi32-lib", toolchain["sdk_lib"] / "um/x86/gdi32.lib"),
        ("windows-sdk-user32-lib", toolchain["sdk_lib"] / "um/x86/user32.lib"),
        ("windows-sdk-bcrypt-lib", toolchain["sdk_lib"] / "um/x86/bcrypt.lib"),
        ("windows-sdk-ucrt-lib", toolchain["sdk_lib"] / "ucrt/x86/libucrt.lib"),
        ("msvc-libcmt", toolchain["msvc_lib"] / "libcmt.lib"),
        ("msvc-libcpmt", toolchain["msvc_lib"] / "libcpmt.lib"),
    ]
    missing = [str(path) for _, path in files if not path.is_file()]
    if missing:
        raise RuntimeError(
            "GDI atlas build-contract input missing: " + ", ".join(missing)
        )
    records = []
    for role, path in files:
        records.append(
            {
                "role": role,
                "path": _contract_path(path, repo_root, tools_root),
                "size": path.stat().st_size,
                "sha256": _sha256_file(path),
            }
        )
    payload: dict[str, Any] = {
        "schema": BUILD_CONTRACT_SCHEMA,
        "schemaVersion": BUILD_CONTRACT_VERSION,
        "compileFlags": list(COMPILE_FLAGS),
        "compilerIdentity": _compiler_identity(
            toolchain["compiler"],
            repo_root,
            tools_root,
        ),
        "toolchainPaths": {
            key: _contract_path(value, repo_root, tools_root)
            for key, value in sorted(toolchain.items())
        },
        "files": records,
    }
    encoded = json.dumps(
        payload,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    payload["digest"] = hashlib.sha256(encoded).hexdigest()
    return payload


def validate_gdi_font_atlas_manifest(
    repo_root: Path,
    manifest: Path,
    tools_root: Path | None = None,
    atlas: Path | None = None,
) -> dict[str, Any]:
    repo_root = repo_root.resolve()
    manifest = manifest.resolve()
    atlas = (
        atlas.resolve()
        if atlas is not None
        else manifest.with_name(ATLAS_NAME).resolve()
    )
    tools_root = (
        tools_root.resolve()
        if tools_root is not None
        else (repo_root.parent / ".tools").resolve()
    )
    payload = json.loads(manifest.read_text(encoding="utf-8"))
    declared_atlas = payload.get("atlas")
    if not isinstance(declared_atlas, dict):
        raise RuntimeError("GDI atlas manifest has no atlas declaration")
    declared_size = declared_atlas.get("byteSize")
    declared_hash = declared_atlas.get("sha256")
    if type(declared_size) is not int or declared_size <= 0:
        raise RuntimeError("GDI atlas manifest has an invalid byteSize")
    if (
        not isinstance(declared_hash, str)
        or len(declared_hash) != 64
        or any(character not in "0123456789abcdef" for character in declared_hash)
    ):
        raise RuntimeError("GDI atlas manifest has an invalid sha256")
    try:
        atlas_bytes = atlas.read_bytes()
    except OSError as error:
        raise RuntimeError(
            f"GDI atlas certified binary is unavailable: {atlas}"
        ) from error
    actual_size = len(atlas_bytes)
    actual_hash = hashlib.sha256(atlas_bytes).hexdigest()
    if actual_size != declared_size or actual_hash != declared_hash:
        raise RuntimeError(
            "GDI atlas certified binary does not match its manifest: "
            f"declaredSize={declared_size} actualSize={actual_size} "
            f"declaredSha256={declared_hash} actualSha256={actual_hash}"
        )
    declared = payload.get("buildContract")
    if not isinstance(declared, dict) or not declared.get("digest"):
        raise RuntimeError("GDI atlas manifest has no build contract")
    current = capture_build_contract(repo_root, tools_root)
    if current != declared:
        raise RuntimeError(
            "GDI atlas generator source/toolchain contract no longer "
            "matches the certified manifest"
        )
    return current


def _compile_and_run_generator(
    *,
    compile_command: list[str],
    executable: Path,
    atlas: Path,
    manifest: Path,
    repo_root: Path,
    environment: dict[str, str],
) -> None:
    subprocess.run(
        compile_command,
        cwd=repo_root,
        env=environment,
        check=True,
    )
    subprocess.run(
        [str(executable), str(atlas), str(manifest)],
        cwd=repo_root,
        env=environment,
        check=True,
    )


def build_gdi_font_atlas(
    repo_root: Path,
    output_dir: Path,
    tools_root: Path | None = None,
) -> tuple[Path, Path]:
    repo_root = repo_root.resolve()
    output_dir = output_dir.resolve()
    tools_root = (
        tools_root.resolve()
        if tools_root is not None
        else (repo_root.parent / ".tools").resolve()
    )
    source = (
        repo_root
        / "webclient/client-wasm/tools/generate_gdi_font_atlas.cpp"
    )
    if not source.is_file():
        raise RuntimeError(f"GDI atlas generator source is missing: {source}")
    output_dir.mkdir(parents=True, exist_ok=True)

    toolchain = discover_toolchain(tools_root)
    executable = output_dir / "generate_gdi_font_atlas.exe"
    object_file = output_dir / "generate_gdi_font_atlas.obj"
    atlas = output_dir / ATLAS_NAME
    manifest = output_dir / MANIFEST_NAME
    for stale_certified_output in (atlas, manifest):
        stale_certified_output.unlink(missing_ok=True)
    environment = os.environ.copy()
    environment["PATH"] = os.pathsep.join(
        (
            str(toolchain["compiler_bin"]),
            str(toolchain["sdk_bin"]),
            environment.get("PATH", ""),
        )
    )
    environment["INCLUDE"] = os.pathsep.join(
        (
            str(toolchain["msvc_include"]),
            str(toolchain["sdk_include"] / "ucrt"),
            str(toolchain["sdk_include"] / "shared"),
            str(toolchain["sdk_include"] / "um"),
        )
    )
    environment["LIB"] = os.pathsep.join(
        (
            str(toolchain["msvc_lib"]),
            str(toolchain["sdk_lib"] / "ucrt/x86"),
            str(toolchain["sdk_lib"] / "um/x86"),
        )
    )
    compile_command = [
        str(toolchain["compiler"]),
        *COMPILE_FLAGS,
        f"/Fo{object_file}",
        f"/Fe{executable}",
        str(source),
    ]
    contract_before = capture_build_contract(repo_root, tools_root)
    try:
        _compile_and_run_generator(
            compile_command=compile_command,
            executable=executable,
            atlas=atlas,
            manifest=manifest,
            repo_root=repo_root,
            environment=environment,
        )
        contract_after = capture_build_contract(repo_root, tools_root)
        if contract_after != contract_before:
            raise RuntimeError(
                "GDI atlas generator source/toolchain changed while the "
                "utility was compiling or running"
            )

        payload = json.loads(manifest.read_text(encoding="utf-8"))
        if payload.get("selfTest", {}).get("passed") is not True:
            raise RuntimeError(
                "GDI atlas generator did not certify its self-test"
            )
        actual_hash = _sha256_file(atlas)
        declared_hash = payload.get("atlas", {}).get("sha256")
        if actual_hash != declared_hash:
            raise RuntimeError(
                "GDI atlas manifest hash mismatch: "
                f"declared={declared_hash} actual={actual_hash}"
            )
        payload["buildContract"] = contract_before
        manifest.write_text(
            json.dumps(payload, ensure_ascii=True, indent=2) + "\n",
            encoding="utf-8",
        )
        contract_final = capture_build_contract(repo_root, tools_root)
        if contract_final != contract_before:
            raise RuntimeError(
                "GDI atlas generator source/toolchain changed while its "
                "certified manifest was being written"
            )
        validate_gdi_font_atlas_manifest(
            repo_root,
            manifest,
            tools_root=tools_root,
            atlas=atlas,
        )
        return atlas, manifest
    except Exception:
        atlas.unlink(missing_ok=True)
        manifest.unlink(missing_ok=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("webclient/client-wasm/build/generated/gdi-font"),
    )
    parser.add_argument("--tools-root", type=Path)
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()
    output_dir = (
        args.output_dir.resolve()
        if args.output_dir.is_absolute()
        else (repo_root / args.output_dir).resolve()
    )
    atlas, manifest = build_gdi_font_atlas(
        repo_root,
        output_dir,
        args.tools_root,
    )
    print(f"[gdi-font-atlas] atlas={atlas}")
    print(f"[gdi-font-atlas] manifest={manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
