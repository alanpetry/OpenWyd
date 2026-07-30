"""Shared, content-addressed contract for TMProject WASM object files."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from functools import lru_cache
from pathlib import Path
from typing import Any, Sequence


SCHEMA = "openwyd.tmproject-wasm-object-contract"
SCHEMA_VERSION = 1
STAMP_NAME = ".openwyd-object-contract.json"
MSBUILD_NS = {"msb": "http://schemas.microsoft.com/developer/msbuild/2003"}
HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".inc", ".inl"}
DEFINES = (
    "-DWIN32",
    "-D_WINDOWS",
    "-DNDEBUG",
    "-DOPENWYD_LAB=1",
    "-D_CRT_SECURE_NO_WARNINGS",
    "-D_WINSOCK_DEPRECATED_NO_WARNINGS",
)


def resolve_emxx() -> str:
    override = os.environ.get("OPENWYD_EMXX")
    if override:
        override_path = Path(override).expanduser()
        if override_path.is_file():
            return str(override_path.resolve())
        override_on_path = shutil.which(override)
        if override_on_path:
            return str(Path(override_on_path).resolve())
        raise FileNotFoundError(
            f"OPENWYD_EMXX does not name an executable: {override}"
        )

    on_path = shutil.which("em++")
    if on_path:
        return str(Path(on_path).resolve())

    emsdk = os.environ.get("EMSDK")
    if emsdk:
        emscripten_root = (
            Path(emsdk).expanduser()
            / "upstream"
            / "emscripten"
        )
        for name in ("em++.exe", "em++.bat", "em++"):
            candidate = emscripten_root / name
            if candidate.is_file():
                return str(candidate.resolve())

    # Keep contract-only tests and diagnostics importable before emsdk is
    # activated. A real compile will fail with the platform's normal
    # executable-not-found error and its full command will remain visible.
    return "em++"


def parse_vcxproj_sources(vcxproj: Path) -> list[Path]:
    root = ET.parse(vcxproj).getroot()
    sources: list[Path] = []
    for node in root.findall(".//msb:ClCompile", MSBUILD_NS):
        include = node.attrib.get("Include")
        if include:
            sources.append((vcxproj.parent / include).resolve())
    return sources


def compile_arguments(repo_root: Path, optimization_flag: str) -> list[str]:
    compat_include = repo_root / "webclient/client-wasm/compat/include"
    return [
        resolve_emxx(),
        "-std=c++17",
        optimization_flag,
        "-c",
        "-fms-extensions",
        "-Wno-microsoft-cast",
        "-Wno-microsoft-anon-tag",
        "-Wno-unknown-pragmas",
        "-include",
        str(compat_include / "tm_emscripten_prelude.h"),
        f"-I{compat_include / 'case_shims'}",
        f"-I{compat_include}",
        f"-I{repo_root / 'Projects/TMProject'}",
        f"-I{repo_root / 'Dependencies/Directx/Include'}",
        *DEFINES,
    ]


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


@lru_cache(maxsize=1)
def compiler_identity() -> dict[str, Any]:
    command = resolve_emxx()
    command_path = Path(command).expanduser()
    executable_text = (
        str(command_path.resolve())
        if command_path.is_file()
        else shutil.which(command)
    )
    executable = (
        Path(executable_text).resolve()
        if executable_text
        else None
    )
    version = ""
    if executable is not None:
        try:
            result = subprocess.run(
                [command, "--version"],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=30,
            )
            version = result.stdout or ""
        except (OSError, subprocess.SubprocessError):
            version = "<unavailable>"
    environment = {
        name: os.environ.get(name)
        for name in (
            "EMSDK",
            "EM_CONFIG",
            "EMSDK_NODE",
            "EMSDK_PYTHON",
            "EMCC_CFLAGS",
        )
    }
    em_config_value = environment["EM_CONFIG"]
    em_config = (
        Path(em_config_value).expanduser().resolve()
        if em_config_value
        else None
    )
    return {
        "resolved_executable": str(executable) if executable else None,
        "executable_sha256": (
            _sha256_file(executable)
            if executable is not None and executable.is_file()
            else None
        ),
        "version": version,
        "environment": environment,
        "em_config_sha256": (
            _sha256_file(em_config)
            if em_config is not None and em_config.is_file()
            else None
        ),
    }


def object_for_source(repo_root: Path, obj_root: Path, source: Path) -> Path:
    relative = source.resolve().relative_to(repo_root.resolve())
    return (obj_root / relative).with_suffix(".o")


def expected_objects(
    repo_root: Path,
    obj_root: Path,
    sources: Sequence[Path],
) -> list[Path]:
    return [
        object_for_source(repo_root, obj_root, source)
        for source in sources
    ]


def _contract_inputs(
    repo_root: Path,
    vcxproj: Path,
    sources: Sequence[Path],
) -> list[Path]:
    inputs = {source.resolve() for source in sources}
    inputs.add(vcxproj.resolve())
    for root in (
        repo_root / "Projects/TMProject",
        repo_root / "webclient/client-wasm/compat/include",
        repo_root / "Dependencies/Directx/Include",
    ):
        for candidate in root.rglob("*"):
            if candidate.is_file() and candidate.suffix.lower() in HEADER_SUFFIXES:
                inputs.add(candidate.resolve())
    return sorted(inputs, key=lambda item: item.as_posix().lower())


def contract_fingerprint(
    repo_root: Path,
    vcxproj: Path,
    optimization_flag: str,
    sources: Sequence[Path],
) -> str:
    digest = hashlib.sha256()
    arguments = compile_arguments(repo_root, optimization_flag)
    digest.update(
        json.dumps(arguments, ensure_ascii=True, separators=(",", ":")).encode(
            "utf-8"
        )
    )
    digest.update(b"\0")
    digest.update(
        json.dumps(
            compiler_identity(),
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
    )
    digest.update(b"\0")
    digest.update(b"tmproject_wasm_object_contract.py\0")
    digest.update(Path(__file__).read_bytes())
    digest.update(b"\0")
    for source in _contract_inputs(repo_root, vcxproj, sources):
        relative = source.relative_to(repo_root).as_posix()
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        with source.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        digest.update(b"\0")
    return digest.hexdigest()


def stamp_path(obj_root: Path) -> Path:
    return obj_root / "Projects/TMProject" / STAMP_NAME


def make_stamp(
    repo_root: Path,
    obj_root: Path,
    vcxproj: Path,
    optimization_flag: str,
    sources: Sequence[Path],
) -> dict[str, Any]:
    objects = expected_objects(repo_root, obj_root, sources)
    return {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "optimization": optimization_flag,
        "fingerprint": contract_fingerprint(
            repo_root,
            vcxproj,
            optimization_flag,
            sources,
        ),
        "sources": [
            source.relative_to(repo_root).as_posix()
            for source in sources
        ],
        "objects": [
            obj.relative_to(repo_root).as_posix()
            for obj in objects
        ],
    }


def write_stamp(
    repo_root: Path,
    obj_root: Path,
    vcxproj: Path,
    optimization_flag: str,
    sources: Sequence[Path],
) -> dict[str, Any]:
    value = stamp_with_object_hashes(
        repo_root,
        obj_root,
        sources,
        make_stamp(
            repo_root,
            obj_root,
            vcxproj,
            optimization_flag,
            sources,
        ),
    )
    publish_stamp(obj_root, value)
    return value


def stamp_with_object_hashes(
    repo_root: Path,
    obj_root: Path,
    sources: Sequence[Path],
    contract: dict[str, Any],
) -> dict[str, Any]:
    """Return a stamp candidate without making it visible to consumers."""

    value = dict(contract)
    object_hashes: dict[str, str] = {}
    for object_path in expected_objects(repo_root, obj_root, sources):
        if not object_path.is_file():
            raise FileNotFoundError(
                f"cannot stamp missing object: {object_path}"
            )
        relative = object_path.relative_to(repo_root).as_posix()
        object_hashes[relative] = _sha256_file(object_path)
    value["object_sha256"] = object_hashes
    return value


def publish_stamp(
    obj_root: Path,
    value: dict[str, Any],
) -> None:
    """Atomically publish a fully validated object-contract stamp."""

    path = stamp_path(obj_root)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{STAMP_NAME}.",
        suffix=".tmp",
        dir=path.parent,
        text=True,
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(json.dumps(value, indent=2, sort_keys=True) + "\n")
        os.replace(temporary_path, path)
    finally:
        temporary_path.unlink(missing_ok=True)


def read_stamp(obj_root: Path) -> dict[str, Any] | None:
    path = stamp_path(obj_root)
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def stamp_matches(
    stamp: dict[str, Any] | None,
    expected: dict[str, Any],
    repo_root: Path,
) -> bool:
    if stamp is None:
        return False
    contract_fields_match = all(
        stamp.get(field) == expected.get(field)
        for field in (
            "schema",
            "schema_version",
            "optimization",
            "fingerprint",
            "sources",
            "objects",
        )
    )
    if not contract_fields_match:
        return False

    object_paths = expected.get("objects")
    object_hashes = stamp.get("object_sha256")
    if not isinstance(object_paths, list) or not isinstance(object_hashes, dict):
        return False
    if set(object_hashes) != set(object_paths):
        return False
    for relative in object_paths:
        if not isinstance(relative, str):
            return False
        path = repo_root / relative
        expected_hash = object_hashes.get(relative)
        if (
            not isinstance(expected_hash, str)
            or len(expected_hash) != 64
            or not path.is_file()
            or _sha256_file(path) != expected_hash
        ):
            return False
    return True
