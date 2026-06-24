#!/usr/bin/env python3
"""Link TMProject object set into a startup-callable WASM artifact exporting _wyd_start_client.

This script compiles compatibility sources, links with strict undefined-symbol checks,
and emits reports for unresolved symbols when link fails.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from collections import Counter
from pathlib import Path

UNDEF_RE = re.compile(r"undefined symbol: (.+)$")


def compile_source(repo_root: Path, src: Path, out_obj: Path) -> None:
    compat_include = repo_root / "webclient/client-wasm/compat/include"
    tmproject_include = repo_root / "Projects/TMProject"
    directx_include = repo_root / "Dependencies/Directx/Include"
    preinclude = compat_include / "tm_emscripten_prelude.h"

    out_obj.parent.mkdir(parents=True, exist_ok=True)

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
        "-DWIN32",
        "-D_WINDOWS",
        "-DNDEBUG",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-D_WINSOCK_DEPRECATED_NO_WARNINGS",
        str(src),
        "-o",
        str(out_obj),
    ]

    subprocess.run(cmd, cwd=repo_root, check=True)


def all_tmproject_objects(obj_root: Path) -> list[Path]:
    base = obj_root / "Projects" / "TMProject"
    objs = sorted(base.glob("*.o"))
    return objs


def parse_undefined(stderr_text: str) -> Counter[str]:
    counter: Counter[str] = Counter()
    for line in stderr_text.splitlines():
        m = UNDEF_RE.search(line)
        if m:
            counter[m.group(1).strip()] += 1
    return counter


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
        wildcard_root_abs = (repo_root / wildcard_root).resolve() if wildcard_root else None

        for item in expanded:
            if not item.exists() or not item.is_file():
                continue
            src_rel = str(item.relative_to(repo_root)).replace("\\", "/")
            if dst_spec:
                if dst_spec.endswith("/"):
                    rel_tail = item.name
                    if wildcard_root_abs:
                        try:
                            rel_tail = item.resolve().relative_to(wildcard_root_abs).as_posix()
                        except ValueError:
                            rel_tail = item.name
                    dst = f"{dst_spec}{rel_tail}"
                else:
                    dst = dst_spec if len(expanded) == 1 else f"{dst_spec}/{item.name}"
                entries.append(f"{src_rel}@{dst}")
            else:
                entries.append(src_rel)

    return entries


def write_reports(report_json: Path, report_md: Path, link_ok: bool, returncode: int, cmd: list[str], undef: Counter[str]) -> None:
    report_json.parent.mkdir(parents=True, exist_ok=True)

    payload = {
        "ok": link_ok,
        "returncode": returncode,
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


def main() -> int:
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
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    obj_root = (repo_root / args.obj_root).resolve()
    link_dir = (repo_root / args.link_dir).resolve()
    report_json = (repo_root / args.report_json).resolve()
    report_md = (repo_root / args.report_md).resolve()
    preload_manifest = (repo_root / args.preload_manifest).resolve()

    link_dir.mkdir(parents=True, exist_ok=True)

    entry_src = repo_root / "webclient/client-wasm/compat/src/wyd_client_entry.cpp"
    stubs_src = repo_root / "webclient/client-wasm/compat/src/win32_emscripten_stubs.cpp"
    entry_obj = obj_root / "webclient/client-wasm/compat/src/wyd_client_entry.o"
    stubs_obj = obj_root / "webclient/client-wasm/compat/src/win32_emscripten_stubs.o"

    print(f"[startup-link] compiling: {entry_src.relative_to(repo_root)}")
    compile_source(repo_root, entry_src, entry_obj)

    print(f"[startup-link] compiling: {stubs_src.relative_to(repo_root)}")
    compile_source(repo_root, stubs_src, stubs_obj)

    tm_objs = all_tmproject_objects(obj_root)
    if not tm_objs:
        print("[startup-link] no TMProject objects found; run object build first")
        return 2

    all_objs = [*tm_objs, entry_obj, stubs_obj]
    rsp_path = link_dir / "startup-objects.rsp"
    rsp_path.write_text("\n".join(str(p.relative_to(repo_root)) for p in all_objs) + "\n", encoding="utf-8")

    out_js = link_dir / "tmproject_startup.js"
    stdout_path = link_dir / "startup-strict-all.stdout.txt"
    stderr_path = link_dir / "startup-strict-all.stderr.txt"
    preload_entries = read_preload_entries(repo_root, preload_manifest)

    link_cmd = [
        "em++",
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
        "-sEXPORTED_FUNCTIONS=['_wyd_start_client','_wyd_boot_client','_wyd_tick_client','_wyd_shutdown_client','_wyd_debug_set_fake_time','_wyd_debug_advance_fake_time','_wyd_debug_clear_fake_time','_wyd_debug_get_time','_wyd_debug_set_demo_camera_offset','_wyd_debug_clear_demo_camera_offset','_wyd_debug_demo_camera_offset_enabled','_wyd_debug_demo_camera_offset_x','_wyd_debug_demo_camera_offset_y','_wyd_debug_demo_camera_offset_z','_wyd_debug_demo_camera_offset_h','_wyd_debug_demo_camera_offset_v','_wyd_debug_camera_valid','_wyd_debug_camera_standalone','_wyd_debug_camera_x','_wyd_debug_camera_y','_wyd_debug_camera_z','_wyd_debug_camera_h','_wyd_debug_camera_v','_wyd_d3d9_set_draw_scope','_wyd_d3d9_clear_draw_scope','_wyd_d3d9_set_draw_label','_wyd_d3d9_trace_set_enabled','_wyd_d3d9_trace_get_enabled','_wyd_d3d9_trace_reset','_wyd_d3d9_trace_clear_probes','_wyd_d3d9_trace_set_probe','_wyd_d3d9_trace_probe_capacity','_wyd_d3d9_trace_probe_enabled','_wyd_d3d9_trace_probe_draw','_wyd_d3d9_trace_probe_result','_wyd_d3d9_trace_probe_hit_count','_wyd_d3d9_trace_probe_first_hit_draw','_wyd_d3d9_trace_probe_first_hit_result','_wyd_d3d9_trace_probe_nearest_hit_draw','_wyd_d3d9_trace_probe_nearest_hit_result','_wyd_d3d9_trace_probe_nearest_zwrite_draw','_wyd_d3d9_trace_probe_nearest_zwrite_result','_wyd_d3d9_trace_top_count','_wyd_d3d9_trace_top_sample','_wyd_d3d9_draw_calls','_wyd_d3d9_primitives','_wyd_d3d9_tex_decode_success','_wyd_d3d9_tex_decode_fail','_wyd_d3d9_tex_uploads','_wyd_d3d9_textured_draws','_wyd_d3d9_tex_alpha_promoted_opaque','_wyd_d3d9_shader_draws_skipped','_wyd_d3d9_vs_unique_shaders','_wyd_d3d9_ps_unique_shaders','_wyd_d3d9_vs_bind_calls','_wyd_d3d9_ps_bind_calls','_wyd_d3d9_draws_with_vs','_wyd_d3d9_draws_with_ps','_wyd_d3d9_active_vs_hash_lo','_wyd_d3d9_active_vs_hash_hi','_wyd_d3d9_active_ps_hash_lo','_wyd_d3d9_active_ps_hash_hi','_wyd_d3d9_vs_top_hash_lo','_wyd_d3d9_vs_top_hash_hi','_wyd_d3d9_vs_top_binds','_wyd_d3d9_vs_top_uses','_wyd_d3d9_vs_top_skips','_wyd_d3d9_vs_top_version','_wyd_d3d9_ps_top_hash_lo','_wyd_d3d9_ps_top_hash_hi','_wyd_d3d9_ps_top_binds','_wyd_d3d9_ps_top_uses','_wyd_d3d9_ps_top_skips','_wyd_d3d9_ps_top_version','_wyd_d3d9_shader_file_open_attempts','_wyd_d3d9_shader_file_open_success','_wyd_d3d9_shader_file_open_fail','_wyd_d3d9_shader_file_open_skinmesh','_wyd_d3d9_shader_file_open_vseffect','_wyd_d3d9_shader_file_open_pseffect','_wyd_d3d9_asset_file_open_attempts','_wyd_d3d9_asset_file_open_success','_wyd_d3d9_asset_file_open_fail','_wyd_d3d9_asset_file_open_fail_mesh','_wyd_d3d9_asset_file_open_fail_env','_wyd_d3d9_asset_file_open_fail_ui','_wyd_d3d9_asset_file_open_fail_texture','_wyd_d3d9_asset_file_open_fail_sound','_wyd_d3d9_asset_file_open_fail_sample_count','_wyd_d3d9_asset_file_open_fail_sample','_wyd_d3d9_asset_path_fallback_attempts','_wyd_d3d9_asset_path_fallback_hits','_wyd_d3d9_asset_path_fallback_or010_hits','_wyd_d3d9_asset_path_fallback_sample_count','_wyd_d3d9_asset_path_fallback_sample','_wyd_d3d9_fvf_xyzrhw_draws','_wyd_d3d9_fvf_weighted_draws','_wyd_d3d9_fvf_tex2plus_draws','_wyd_d3d9_fvf_3d_vertices_total','_wyd_d3d9_fvf_3d_vertices_in_clip','_wyd_d3d9_decl_draw_calls','_wyd_d3d9_decl_skinned_draw_calls','_wyd_d3d9_decl_vertices_total','_wyd_d3d9_decl_vertices_in_clip','_wyd_d3d9_decl_rgba_index_order_draws','_wyd_d3d9_decl_bgra_index_order_draws','_wyd_d3d9_invalid_indexed_draws','_wyd_d3d9_invalid_indices_total','_wyd_d3d9_clip_w_reject_draws','_wyd_d3d9_clip_w_reject_triangles','_wyd_d3d9_clip_w_keep_triangles','_wyd_d3d9_stage1_generated_tci_draws','_wyd_d3d9_stage1_textransform_draws','_wyd_d3d9_stage1_tci0_draws','_wyd_d3d9_stage1_tci1_draws','_wyd_d3d9_stage1_tci_other_draws','_wyd_d3d9_alpha_test_enabled_draws','_wyd_d3d9_alpha_test_disabled_draws','_wyd_d3d9_blend_enabled_draws','_wyd_d3d9_depth_test_disabled_draws','_wyd_d3d9_depth_write_disabled_draws','_wyd_d3d9_depth_write_guard_forced_draws','_wyd_d3d9_lighting_enabled_draws','_wyd_d3d9_fog_enabled_draws','_wyd_d3d9_wireframe_draws','_wyd_d3d9_material_set_calls','_wyd_d3d9_light_set_calls','_wyd_d3d9_light_enable_calls','_wyd_d3d9_lighted_vertices','_wyd_d3d9_stage0_color_selectarg1_draws','_wyd_d3d9_stage0_color_modulate_draws','_wyd_d3d9_stage0_alpha_selectarg1_draws','_wyd_d3d9_stage0_alpha_modulate_draws','_wyd_d3d9_cull_none_draws','_wyd_d3d9_cull_cw_draws','_wyd_d3d9_cull_ccw_draws','_wyd_d3d9_cull_mirror_worldview_draws','_wyd_d3d9_cull_frontface_flipped_draws','_wyd_d3d9_gl_error_total','_wyd_d3d9_gl_error_draw_calls','_wyd_d3d9_gl_error_last','_wyd_d3d9_clear_calls','_wyd_d3d9_present_calls','_wyd_d3d9_begin_scene_calls','_wyd_d3d9_end_scene_calls','_wyd_d3d9_last_clear_color','_wyd_d3d9_last_clear_flags','_wyd_d3d9_last_clear_time_ms','_wyd_d3d9_last_present_time_ms','_wyd_d3d9_last_present_blend_enabled','_wyd_d3d9_last_present_depth_enabled','_wyd_d3d9_last_present_depth_write','_wyd_d3d9_last_present_alpha_test','_wyd_d3d9_last_present_src_blend','_wyd_d3d9_last_present_dst_blend','_wyd_d3d9_fvf_top_code','_wyd_d3d9_fvf_top_count','_wyd_d3d9_fvf_depth_write_enabled_top_code','_wyd_d3d9_fvf_depth_write_enabled_top_count','_wyd_d3d9_fvf_depth_write_disabled_top_code','_wyd_d3d9_fvf_depth_write_disabled_top_count','_wyd_d3d9_debug_skip_fvf_draws','_wyd_d3d9_fvf322_draw_primitive_up','_wyd_d3d9_fvf322_draw_indexed_primitive_up','_wyd_d3d9_fvf322_draw_indexed_primitive','_wyd_d3d9_fvf322_with_stage0_texture','_wyd_d3d9_fvf322_without_stage0_texture','_wyd_d3d9_fvf322_screenlike_vertices','_wyd_d3d9_fvf322_screenlike_draws','_wyd_d3d9_fvf322_screenlike_replay_draws','_wyd_d3d9_fvf322_screenlike_replay_suppressed','_wyd_d3d9_texture_draws_sky','_wyd_d3d9_texture_draws_water','_wyd_d3d9_texture_draws_bright','_wyd_d3d9_fvf322_bright_draws','_wyd_d3d9_stage0_colorop8_draws','_wyd_d3d9_stage0_colorop8_with_texture','_wyd_d3d9_stage0_colorop8_without_texture','_wyd_d3d9_stage0_colorop8_pathless_texture','_wyd_d3d9_stage0_colorop8_last_fvf','_wyd_d3d9_stage0_colorop8_last_width','_wyd_d3d9_stage0_colorop8_last_height','_wyd_d3d9_stage0_colorop8_last_path_len','_wyd_d3d9_set_stage0_colorop8_calls','_wyd_d3d9_set_stage0_colorop4_calls','_wyd_d3d9_set_stage0_colorop_last_value','_wyd_d3d9_set_texture_stage0_sky_calls','_wyd_d3d9_set_texture_stage1_sky_calls','_wyd_d3d9_draw_attempts_with_sky_texture','_wyd_d3d9_draw_attempts_with_sky_texture_indexed','_wyd_d3d9_draw_attempts_with_sky_texture_up','_wyd_d3d9_draw_attempts_with_sky_last_fvf','_wyd_d3d9_sky_clip_draws','_wyd_d3d9_sky_clip_last_vertex_count','_wyd_d3d9_sky_clip_last_index_count','_wyd_d3d9_sky_clip_last_stable_w_vertices','_wyd_d3d9_sky_clip_last_negative_w_vertices','_wyd_d3d9_sky_clip_last_near_w_vertices','_wyd_d3d9_sky_clip_last_inside_vertices','_wyd_d3d9_sky_clip_last_large_ndc_vertices','_wyd_d3d9_sky_clip_last_triangle_count','_wyd_d3d9_sky_clip_last_triangles_all_stable_w','_wyd_d3d9_sky_clip_last_triangles_any_unstable_w','_wyd_d3d9_sky_clip_last_triangles_any_outside','_wyd_d3d9_sky_clip_last_min_w','_wyd_d3d9_sky_clip_last_max_w','_wyd_d3d9_sky_clip_last_min_ndc_x','_wyd_d3d9_sky_clip_last_max_ndc_x','_wyd_d3d9_sky_clip_last_min_ndc_y','_wyd_d3d9_sky_clip_last_max_ndc_y','_wyd_d3d9_sky_clip_last_min_ndc_z','_wyd_d3d9_sky_clip_last_max_ndc_z','_wyd_d3d9_mark_next_draw_sky','_wyd_d3d9_fvf322_auto_clipw_draws','_wyd_d3d9_fvf322_auto_clipw_reject_draws','_wyd_d3d9_fvf530_auto_clipw_draws','_wyd_d3d9_fvf530_auto_clipw_reject_draws','_wyd_d3d9_fvf530_draws','_wyd_d3d9_fvf530_large_bounds_draws','_wyd_d3d9_fvf530_large_bounds_skip_draws','_wyd_d3d9_fvf530_large_bound_sample_count','_wyd_d3d9_fvf530_large_bound_sample','_wyd_d3d9_fvf530_unstable_w_draws','_wyd_d3d9_fvf530_vertices','_wyd_d3d9_fvf530_inside_vertices','_wyd_d3d9_fvf530_last_vertex_count','_wyd_d3d9_fvf530_last_index_count','_wyd_d3d9_fvf530_last_stable_w_vertices','_wyd_d3d9_fvf530_last_inside_vertices','_wyd_d3d9_fvf530_last_large_ndc_vertices','_wyd_d3d9_fvf530_last_min_ndc_x','_wyd_d3d9_fvf530_last_max_ndc_x','_wyd_d3d9_fvf530_last_min_ndc_y','_wyd_d3d9_fvf530_last_max_ndc_y','_wyd_d3d9_fvf530_last_min_ndc_z','_wyd_d3d9_fvf530_last_max_ndc_z','_wyd_d3d9_fvf594_auto_clipw_draws','_wyd_d3d9_fvf594_auto_clipw_reject_draws','_wyd_d3d9_up_reset_stream0_calls','_wyd_d3d9_up_reset_indices_calls','_wyd_d3d9_set_debug_flags','_wyd_d3d9_get_debug_flags','_wyd_d3d9_set_debug_skip_fvf','_wyd_d3d9_get_debug_skip_fvf','_wyd_d3d9_reset_debug_counters','_wyd_sky_render_calls','_wyd_sky_hidden_returns','_wyd_sky_eligible_calls','_wyd_sky_branch_skipped','_wyd_sky_mesh_null','_wyd_sky_mesh_draws','_wyd_sky_last_dungeon','_wyd_sky_last_state','_wyd_sky_last_texture_index','_wyd_sky_last_mesh_texture_index','_wyd_sky_last_mesh_has_vb','_wyd_sky_last_mesh_has_ib','_wyd_sky_last_mesh_fvf','_wyd_sky_last_mesh_att_count','_wyd_sky_last_mesh_face_count','_wyd_sky_last_mesh_vertex_count','_wyd_sky_last_mesh_render_result','_wyd_sky_reset_debug_counters','_wyd_font2_set_text_calls','_wyd_font2_set_text_nonempty','_wyd_font2_render_calls','_wyd_font2_render_nonempty','_wyd_font2_texture_create_fail','_wyd_font2_lock_calls','_wyd_font2_last_text_len','_wyd_font2_last_line_count','_wyd_font2_last_size0','_wyd_font2_last_size1','_wyd_font2_last_size2','_wyd_font2_last_alpha_pixels','_wyd_font2_last_has_bitmap','_wyd_font2_last_render_x','_wyd_font2_last_render_y','_wyd_font2_last_render_type','_wyd_font2_last_text','_wyd_get_game_state','_wyd_set_game_state']",
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString']",
        *[f"--preload-file={entry}" for entry in preload_entries],
        "-o",
        str(out_js.relative_to(repo_root)),
    ]

    extra_exports = [
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
        "_wyd_d3d9_fvf322_class_count",
        "_wyd_d3d9_fvf322_class_name",
        "_wyd_d3d9_clipw_empty_signature_count",
        "_wyd_d3d9_clipw_empty_signature_sample",
        "_wyd_d3d9_skin_suspicious_texture_draws",
        "_wyd_d3d9_skin_suspicious_texture_sample_count",
        "_wyd_d3d9_skin_suspicious_texture_sample",
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
        "_wyd_d3d9_fog_enabled",
        "_wyd_d3d9_fog_start",
        "_wyd_d3d9_fog_end",
        "_wyd_d3d9_fog_color",
        "_wyd_selchar_initialized",
        "_wyd_selchar_char_count",
        "_wyd_selchar_human_present",
        "_wyd_selchar_name",
        "_wyd_serverlist_entry",
        "_wyd_debug_selectserver_login",
        "_wyd_socket_last_host",
        "_wyd_socket_last_proxy_url",
        "_wyd_socket_last_port",
        "_wyd_socket_last_connect_result",
        "_wyd_socket_last_error",
        "_wyd_socket_bytes_sent",
        "_wyd_socket_bytes_received",
        "_wyd_socket_last_sent_opcode",
        "_wyd_socket_last_recv_opcode",
    ]
    for index, arg in enumerate(link_cmd):
        if arg.startswith("-sEXPORTED_FUNCTIONS=["):
            link_cmd[index] = arg[:-1] + "," + ",".join(repr(name) for name in extra_exports) + "]"
            break

    print("[startup-link] linking strict artifact")
    proc = subprocess.run(link_cmd, cwd=repo_root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    stdout_path.write_text(proc.stdout or "", encoding="utf-8")
    stderr_path.write_text(proc.stderr or "", encoding="utf-8")

    undef = parse_undefined(proc.stderr or "")
    write_reports(report_json, report_md, proc.returncode == 0, proc.returncode, link_cmd, undef)

    print(f"[startup-link] returncode={proc.returncode}")
    print(f"[startup-link] undefined_total={sum(undef.values())} unique={len(undef)}")
    print(f"[startup-link] report_json={report_json.relative_to(repo_root)}")
    print(f"[startup-link] report_md={report_md.relative_to(repo_root)}")

    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
