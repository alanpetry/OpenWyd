#!/usr/bin/env python3
"""Regression tests for the Direct3D 9 to WebGL pixel-centre mapping."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
STUBS = (
    REPO_ROOT / "webclient/client-wasm/compat/src/win32_emscripten_stubs.cpp"
).read_text(encoding="utf-8")


def function_body(name: str) -> str:
    marker = f"{name}("
    start = STUBS.index(marker)
    brace = STUBS.index("{", start)
    depth = 0
    for index in range(brace, len(STUBS)):
        if STUBS[index] == "{":
            depth += 1
        elif STUBS[index] == "}":
            depth -= 1
            if depth == 0:
                return STUBS[brace + 1 : index]
    raise AssertionError(f"unterminated function: {name}")


class WasmPixelCenterTests(unittest.TestCase):
    def test_screen_space_vertices_target_webgl_pixel_centres(self) -> None:
        self.assertIn("const float webgl_x = vx + 0.5f;", STUBS)
        self.assertIn("const float webgl_y = vy + 0.5f;", STUBS)
        self.assertIn(
            "const float ndc_x = ((webgl_x - "
            "static_cast<float>(g_wasm_d3d9_state.viewport.X)) / vp_w) "
            "* 2.0f - 1.0f;",
            STUBS,
        )
        self.assertIn(
            "const float ndc_y = 1.0f - ((webgl_y - "
            "static_cast<float>(g_wasm_d3d9_state.viewport.Y)) / vp_h) "
            "* 2.0f;",
            STUBS,
        )

        width = 800.0
        height = 600.0
        self.assertAlmostEqual(((0.0 + 0.5) / width) * 2.0 - 1.0, -0.99875)
        self.assertAlmostEqual(1.0 - ((0.0 + 0.5) / height) * 2.0, 0.9983333333)

    def test_common_helper_preserves_the_validated_clip_space_signs(self) -> None:
        helper = function_body("ApplyD3D9PixelCenterToClip")
        self.assertIn("*clip_x += clip_w / vp_w;", helper)
        self.assertIn("*clip_y -= clip_w / vp_h;", helper)
        self.assertIn("g_wasm_d3d9_state.viewport.Width", helper)
        self.assertIn("g_wasm_d3d9_state.viewport.Height", helper)
        self.assertAlmostEqual((1.0 / 800.0) * 400.0, 0.5)
        self.assertAlmostEqual((-1.0 / 600.0) * -300.0, 0.5)

    def test_fvf_transformed_vertices_use_the_common_helper(self) -> None:
        body = function_body("DecodeVertexFromFVF")
        clip_w_assignment = "clip_w = SafeClipW(clip.w);"
        helper_call = (
            "ApplyD3D9PixelCenterToClip(clip_w, &clip_x, &clip_y);"
        )
        self.assertIn(clip_w_assignment, body)
        self.assertIn("clip_x = clip.x;", body)
        self.assertIn("clip_y = clip.y;", body)
        self.assertIn(helper_call, body)
        self.assertLess(body.index(clip_w_assignment), body.index(helper_call))

    def test_declaration_vertices_use_the_common_helper(self) -> None:
        body = function_body("DecodeVertexFromDeclaration")
        w_assignment = (
            "out_vertex->w = std::isfinite(clip.w) ? clip.w : 1.0e-5f;"
        )
        helper_call = (
            "ApplyD3D9PixelCenterToClip(\n"
            "      out_vertex->w,\n"
            "      &out_vertex->x,\n"
            "      &out_vertex->y);"
        )
        self.assertIn(w_assignment, body)
        self.assertIn(helper_call, body)
        self.assertLess(body.index(w_assignment), body.index(helper_call))

    def test_sprite_does_not_add_a_second_pixel_center_offset(self) -> None:
        body = function_body("HRESULT Draw")
        self.assertNotIn("ApplyD3D9PixelCenterToClip", body)
        self.assertNotIn("+ 0.5f", body)
        self.assertNotIn("- 0.5f", body)


if __name__ == "__main__":
    unittest.main(verbosity=2)
