from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
TM_ROOT = REPO_ROOT / "Projects" / "TMProject"
WASM_STUBS = (
    REPO_ROOT
    / "webclient"
    / "client-wasm"
    / "compat"
    / "src"
    / "win32_emscripten_stubs.cpp"
)
WASM_LINK = (
    REPO_ROOT
    / "webclient"
    / "client-wasm"
    / "tools"
    / "link_tmproject_wasm_startup.py"
)


class Compare3DStateContractTests(unittest.TestCase):
    def test_common_hook_runs_before_ui_matrices_replace_3d_state(self) -> None:
        render = (TM_ROOT / "RenderDevice.cpp").read_text("utf-8")
        start = render.index("int RenderDevice::SetMatrixForUI()")
        end = render.index("void RenderDevice::GetPickRayVector", start)
        body = render[start:end]

        native_latch = body.index("OpenWydCompareCapture3DState(")
        wasm_latch = body.index("wyd_compare_latch_3d_state();")
        ui_projection = body.index("D3DXMatrixPerspectiveFovLH(")
        first_transform = body.index("SetTransform(")
        self.assertLess(native_latch, ui_projection)
        self.assertLess(wasm_latch, ui_projection)
        self.assertLess(native_latch, first_transform)
        self.assertLess(wasm_latch, first_transform)

    def test_native_snapshot_uses_latched_not_post_scene_transforms(self) -> None:
        compare = (TM_ROOT / "OpenWydCompare.cpp").read_text("utf-8")
        snapshot_start = compare.index("bool WriteSnapshot(")
        snapshot_end = compare.index(
            "bool OpenWydCompareArmRandomFromEnvironment",
            snapshot_start,
        )
        snapshot = compare[snapshot_start:snapshot_end]

        self.assertIn("before_SetMatrixForUI", snapshot)
        self.assertIn("g_compare.capture3DWorld", snapshot)
        self.assertIn("g_compare.capture3DView", snapshot)
        self.assertIn("g_compare.capture3DProjection", snapshot)
        self.assertNotIn("GetTransform(", snapshot)
        self.assertNotIn("AppendTransform(", compare)

    def test_wasm_latch_is_armed_once_per_begin_scene_and_exported(self) -> None:
        stubs = WASM_STUBS.read_text("utf-8")
        begin_start = stubs.index("HRESULT WydD3D9Device_BeginScene")
        begin_end = stubs.index("HRESULT WydD3D9Device_EndScene", begin_start)
        begin = stubs[begin_start:begin_end]
        self.assertIn("BeginCompare3DStateFrame();", begin)

        latch_start = stubs.index(
            'extern "C" void wyd_compare_latch_3d_state()'
        )
        latch_end = stubs.index(
            "HRESULT WydD3D9Device_SetTransform",
            latch_start,
        )
        latch = stubs[latch_start:latch_end]
        self.assertIn("if (!g_compare_3d_state.armed) return;", latch)
        self.assertIn("g_compare_3d_state.armed = false;", latch)
        self.assertIn("g_ffp_state.world[0]", latch)
        self.assertIn("g_ffp_state.view", latch)
        self.assertIn("g_ffp_state.proj", latch)

        link = WASM_LINK.read_text("utf-8")
        for export in (
            "_wyd_compare_3d_state_sequence",
            "_wyd_compare_3d_state_valid",
            "_wyd_compare_3d_state_frame_serial",
            "_wyd_compare_3d_state_draw_serial",
            "_wyd_compare_3d_state_matrices",
            "_wyd_compare_3d_state_matrix_value",
        ):
            self.assertIn(export, link)


if __name__ == "__main__":
    unittest.main()
