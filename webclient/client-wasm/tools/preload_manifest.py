"""Expand OpenWyd's preload manifest into deterministic file mappings."""

from __future__ import annotations

from pathlib import Path


def read_preload_entries(repo_root: Path, manifest_path: Path) -> list[str]:
    if not manifest_path.exists():
        return []

    entries: list[str] = []

    def wildcard_base(src_spec: str) -> Path | None:
        wildcard_pos = None
        for token in ("*", "?", "["):
            pos = src_spec.find(token)
            if pos != -1 and (wildcard_pos is None or pos < wildcard_pos):
                wildcard_pos = pos
        if wildcard_pos is None:
            return None

        base = src_spec[:wildcard_pos]
        slash = base.rfind("/")
        if slash == -1:
            return Path(".")

        base_dir = base[:slash].rstrip("/")
        if not base_dir:
            return Path(".")
        return Path(base_dir)

    for raw in manifest_path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue

        src_spec, _, dst_spec = line.partition("@")
        src_spec = src_spec.strip()
        dst_spec = dst_spec.strip()
        if not src_spec:
            continue

        has_glob = any(ch in src_spec for ch in ("*", "?", "["))
        if not has_glob:
            src_path = (repo_root / src_spec).resolve()
            if not src_path.exists():
                print(f"[startup-link] preload missing, skipping: {src_spec}")
                continue
            entries.append(line)
            continue

        expanded = sorted((repo_root / ".").glob(src_spec))
        if not expanded:
            print(f"[startup-link] preload glob empty, skipping: {src_spec}")
            continue

        wildcard_root = wildcard_base(src_spec)
        wildcard_root_abs = (
            (repo_root / wildcard_root).resolve() if wildcard_root else None
        )

        for item in expanded:
            if not item.exists() or not item.is_file():
                continue
            src_rel = str(item.relative_to(repo_root)).replace("\\", "/")
            if dst_spec:
                if dst_spec.endswith("/"):
                    rel_tail = item.name
                    if wildcard_root_abs:
                        try:
                            rel_tail = item.resolve().relative_to(
                                wildcard_root_abs
                            ).as_posix()
                        except ValueError:
                            rel_tail = item.name
                    dst = f"{dst_spec}{rel_tail}"
                else:
                    dst = (
                        dst_spec
                        if len(expanded) == 1
                        else f"{dst_spec}/{item.name}"
                    )
                entries.append(f"{src_rel}@{dst}")
            else:
                entries.append(src_rel)

    return entries
