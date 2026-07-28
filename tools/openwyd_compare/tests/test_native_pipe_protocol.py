from __future__ import annotations

import unittest
from pathlib import Path

from tools.openwyd_compare.frame_schema import new_frame_record, validate_frame_record
from tools.openwyd_compare.native_pipe_protocol import (
    MAX_FRAME_ID,
    MAX_RANDOM_SEED,
    MAX_TIME_MS,
    NativePipeProtocolError,
    char_command,
    close_command,
    key_down_command,
    key_up_command,
    mouse_down_command,
    mouse_move_command,
    mouse_up_command,
    parse_event,
    ping_command,
    random_seed_command,
    step_command,
    text_commands,
)


REPO_ROOT = Path(__file__).resolve().parents[3]
NATIVE_ROOT = REPO_ROOT / "Projects" / "TMProject"


class NativePipeProtocolTests(unittest.TestCase):
    def test_commands_enforce_wire_integer_ranges(self) -> None:
        self.assertEqual(step_command(42, 1000), b"STEP 42 1000\n")
        self.assertEqual(step_command(MAX_FRAME_ID, MAX_TIME_MS).count(b"\n"), 1)
        self.assertEqual(ping_command(), b"PING\n")
        self.assertEqual(close_command(), b"CLOSE\n")
        self.assertEqual(random_seed_command(0), b"RANDOM_SEED 0\n")
        self.assertEqual(
            random_seed_command(MAX_RANDOM_SEED),
            b"RANDOM_SEED 4294967295\n",
        )

        for frame_id, time_ms in ((-1, 0), (MAX_FRAME_ID + 1, 0), (0, -1), (0, MAX_TIME_MS + 1)):
            with self.subTest(frame_id=frame_id, time_ms=time_ms):
                with self.assertRaises(NativePipeProtocolError):
                    step_command(frame_id, time_ms)

    def test_responses_have_an_unambiguous_versioned_contract(self) -> None:
        ready = parse_event(b"READY 1 1234 800 600 1\r\n")
        self.assertEqual(ready.kind, "READY")
        self.assertEqual(ready.values, (1, 1234, 800, 600, True))

        accepted = parse_event("STEP_ACCEPTED 42 1000\n")
        self.assertEqual(accepted.values, (42, 1000))

        present = parse_event("PRESENT 42 1000 0x00000000 1\n")
        self.assertEqual(present.values, (42, 1000, 0, True))
        self.assertEqual(parse_event("PONG\n").kind, "PONG")
        self.assertEqual(parse_event("CLOSING\n").kind, "CLOSING")
        self.assertEqual(parse_event("BYE\n").kind, "BYE")
        self.assertEqual(parse_event("ERROR invalid_step\n").values, ("invalid_step",))
        self.assertEqual(
            parse_event("INPUT_QUEUED 9 42\n").values,
            (9, 42),
        )
        self.assertEqual(
            parse_event("RANDOM_SEEDED 4294967295\n").values,
            (MAX_RANDOM_SEED,),
        )

        for invalid in ("READY 2 1 800 600 1", "PRESENT 1 0 success 1", "UNKNOWN"):
            with self.subTest(invalid=invalid):
                with self.assertRaises(NativePipeProtocolError):
                    parse_event(invalid)

    def test_input_commands_are_versioned_frame_bound_and_cp1252(self) -> None:
        self.assertEqual(
            mouse_move_command(42, 100, 200),
            b"INPUT 1 42 MOUSE_MOVE 100 200\n",
        )
        self.assertEqual(
            mouse_down_command(42, "left", 100, 200),
            b"INPUT 1 42 MOUSE_DOWN LEFT 100 200\n",
        )
        self.assertEqual(
            mouse_up_command(42, "RIGHT", 100, 200),
            b"INPUT 1 42 MOUSE_UP RIGHT 100 200\n",
        )
        self.assertEqual(
            key_down_command(42, 13),
            b"INPUT 1 42 KEY_DOWN 13\n",
        )
        self.assertEqual(
            key_up_command(42, 13),
            b"INPUT 1 42 KEY_UP 13\n",
        )
        self.assertEqual(char_command(42, "é"), b"INPUT 1 42 CHAR 233\n")
        self.assertEqual(
            text_commands(42, "Aé"),
            (b"INPUT 1 42 CHAR 65\n", b"INPUT 1 42 CHAR 233\n"),
        )

        invalid_calls = (
            lambda: mouse_move_command(1, -1, 0),
            lambda: mouse_down_command(1, "middle", 0, 0),
            lambda: key_down_command(1, 0),
            lambda: key_up_command(1, 255),
            lambda: char_command(1, 0),
            lambda: char_command(1, "🙂"),
            lambda: char_command(1, "ab"),
        )
        for invalid_call in invalid_calls:
            with self.subTest(call=invalid_call):
                with self.assertRaises(NativePipeProtocolError):
                    invalid_call()

    def test_native_snapshot_shape_matches_shared_schema(self) -> None:
        record = new_frame_record(
            42,
            state={"game": 7, "scene": 30004},
            ticks={"compare_frame": 42},
            clock={"controlled_time_ms": 1000},
            camera={},
            matrices={"world": None, "view": None, "projection": None},
            draws=[],
            render={"capture_point": "after_EndScene_before_Present"},
            network={},
            extensions={
                "native": {
                    "draw_capture_available": False,
                    "packet_opcode_hash_available": False,
                }
            },
        )
        validate_frame_record(record)

    def test_source_guard_and_pre_present_order_are_structural(self) -> None:
        guard = (
            "defined(OPENWYD_COMPARE) && defined(_DEBUG) && "
            "!defined(__EMSCRIPTEN__)"
        )
        header = (NATIVE_ROOT / "OpenWydCompare.h").read_text("utf-8")
        implementation = (NATIVE_ROOT / "OpenWydCompare.cpp").read_text("utf-8")
        pch = (NATIVE_ROOT / "pch.h").read_text("utf-8")
        render = (NATIVE_ROOT / "RenderDevice.cpp").read_text("utf-8")
        new_app = (NATIVE_ROOT / "NewApp.cpp").read_text("utf-8")
        event_translator = (NATIVE_ROOT / "EventTranslator.cpp").read_text(
            "utf-8"
        )
        wasm_entry = (
            REPO_ROOT
            / "webclient"
            / "client-wasm"
            / "compat"
            / "src"
            / "wyd_client_entry.cpp"
        ).read_text("utf-8")
        project = (NATIVE_ROOT / "TMProject.vcxproj").read_text("utf-8")
        compare_props = (
            REPO_ROOT / "tools" / "build_windows_source.compare.props"
        ).read_text("utf-8")

        self.assertIn(guard, header)
        self.assertIn(guard, implementation)
        self.assertIn(guard, pch)
        self.assertIn("OPENWYD_COMPARE_PIPE", implementation)
        self.assertIn("OPENWYD_COMPARE_ARTIFACTS", implementation)
        self.assertIn("OPENWYD_COMPARE_SERVER_HOST", implementation)
        self.assertIn("OPENWYD_COMPARE_SERVER_PORT", implementation)
        self.assertIn("D3DXSaveSurfaceToFileA", implementation)
        self.assertIn("INPUT_QUEUED", implementation)
        self.assertIn("PostMessageA(g_compare.window", implementation)
        self.assertIn("step_input_frame_mismatch", implementation)
        self.assertIn("OpenWydCompareTakeInjectedMouseMessage", implementation)
        queue_start = implementation.index("bool QueueInputMessage")
        queue_end = implementation.index("void HandleInputCommand", queue_start)
        self.assertNotIn("PostMessageA(", implementation[queue_start:queue_end])
        staged_input = implementation.index(
            "const unsigned int pendingInputCount"
        )
        posted_input = implementation.index("PostMessageA(", staged_input)
        accepted_frame = implementation.index(
            "g_compare.hasLastAcceptedFrame = true", posted_input
        )
        pending_time = implementation.index(
            "g_compare.pendingTimeMs =", accepted_frame
        )
        controlled_clock = implementation.index("InterlockedExchange(", pending_time)
        step_pending = implementation.index(
            "g_compare.stepPending = true", controlled_clock
        )
        self.assertLess(posted_input, accepted_frame)
        self.assertLess(accepted_frame, controlled_clock)
        self.assertLess(controlled_clock, step_pending)
        self.assertIn("input_post_failed_fatal", implementation)
        self.assertIn("fatal_protocol_state", implementation)
        run_tick = new_app.index("DWORD NewApp::RunTick")
        poll = new_app.index("OpenWydComparePoll();", run_tick)
        begin_before_dispatch = new_app.index(
            "OpenWydCompareTryBeginFrame();", poll
        )
        first_message_read = min(
            new_app.index("PeekMessage", begin_before_dispatch),
            new_app.index("GetMessage", begin_before_dispatch),
        )
        self.assertLess(poll, begin_before_dispatch)
        self.assertLess(begin_before_dispatch, first_message_read)
        self.assertIn(
            "OpenWydCompareTakePausedControlMessage(pWorkMsg)",
            new_app[begin_before_dispatch:first_message_read],
        )
        run_frame_gate = new_app.index(
            "const bool shouldRunFrame =",
            first_message_read,
        )
        run_frame_gate_end = new_app.index(
            "if (shouldRunFrame)",
            run_frame_gate,
        )
        self.assertIn(
            "|| (OpenWydCompareIsEnabled() && compareFrameActive)",
            new_app[run_frame_gate:run_frame_gate_end],
        )
        begin_frame = implementation.index("bool OpenWydCompareTryBeginFrame")
        begin_frame_end = implementation.index(
            "bool OpenWydCompareTakePausedControlMessage", begin_frame
        )
        begin_frame_body = implementation[begin_frame:begin_frame_end]
        self.assertLess(
            begin_frame_body.index("if (g_compare.frameActive)"),
            begin_frame_body.index("if (!g_compare.stepPending)"),
        )
        paused_control_end = implementation.index(
            "bool OpenWydCompareShouldDispatchMessage", begin_frame_end
        )
        paused_control = implementation[begin_frame_end:paused_control_end]
        self.assertEqual(paused_control.count("PeekMessageA("), 2)
        self.assertIn("WM_QUIT,\n\t\tWM_QUIT,", paused_control)
        self.assertIn("WM_CLOSE,\n\t\t\tWM_CLOSE,", paused_control)
        self.assertNotIn("QS_ALLINPUT", paused_control)
        dispatch_filter_end = implementation.index(
            "bool OpenWydCompareTakeInjectedMouseMessage",
            paused_control_end,
        )
        dispatch_filter = implementation[
            paused_control_end:dispatch_filter_end
        ]
        for physical_input in (
            "WM_MOUSEMOVE",
            "WM_MOUSEWHEEL",
            "WM_KEYDOWN",
            "WM_CHAR",
            "WM_SYSKEYDOWN",
            "WM_IME_COMPOSITION",
        ):
            self.assertIn(physical_input, dispatch_filter)
        self.assertIn("kInjectedMouseMessage", dispatch_filter)
        self.assertIn("kInjectedKeyMessage", dispatch_filter)
        filter_call = new_app.index(
            "OpenWydCompareShouldDispatchMessage(pWorkMsg)",
            begin_before_dispatch,
        )
        accelerator = new_app.index("TranslateAccelerator(", filter_call)
        self.assertLess(filter_call, accelerator)
        self.assertIn(
            "static_cast<LPARAM>(1) | kInjectedKeyMessage",
            implementation,
        )
        self.assertEqual(new_app.count("case WM_MOUSEMOVE:"), 1)
        mouse_move = new_app.index("case WM_MOUSEMOVE:")
        strip_marker = new_app.index(
            "OpenWydCompareTakeInjectedMouseMessage", mouse_move
        )
        forward_mouse = new_app.index(
            "m_pEventTranslator->OnMouseEvent", strip_marker
        )
        self.assertLess(strip_marker, forward_mouse)
        mouse_down = new_app.index("case WM_LBUTTONDOWN:")
        mouse_down_end = new_app.index("case WM_USER + 13:", mouse_down)
        injected_mouse_down = new_app[mouse_down:mouse_down_end]
        self.assertIn(
            "OpenWydCompareTakeInjectedMouseMessage",
            injected_mouse_down,
        )
        self.assertNotIn(
            "m_pEventTranslator->OnMouseEvent",
            injected_mouse_down,
        )
        wasm_mouse = wasm_entry.index('extern "C" int wyd_mouse_event')
        wasm_mouse_end = wasm_entry.index(
            'extern "C" int wyd_key_event', wasm_mouse
        )
        wasm_mouse_body = wasm_entry[wasm_mouse:wasm_mouse_end]
        self.assertIn("wyd_dinput_mouse_event(", wasm_mouse_body)
        self.assertIn("g_wyd_app->MsgProc(", wasm_mouse_body)
        self.assertNotIn(
            "g_pEventTranslator->OnMouseEvent",
            wasm_mouse_body,
        )
        self.assertLess(
            render.index("OpenWydCompareOnBeforePresent"),
            render.index("m_pd3dDevice->Present", render.index("int RenderDevice::Unlock")),
        )
        self.assertLess(
            render.index("m_pd3dDevice->Present", render.index("int RenderDevice::Unlock")),
            render.index("OpenWydCompareOnAfterPresent"),
        )
        self.assertIn("present_failed_fatal", implementation)
        self.assertIn(
            "frame_completed_without_present_fatal",
            implementation,
        )
        frame_tick_complete = new_app.index(
            "OpenWydCompareOnFrameTickComplete();", run_tick
        )
        self.assertGreater(
            frame_tick_complete,
            new_app.index("m_pObjectManager->CleanUp();", run_tick),
        )
        self.assertLess(
            frame_tick_complete,
            new_app.index(
                "return (pWorkMsg->message == WM_QUIT)",
                frame_tick_complete,
            ),
        )
        self.assertIn(
            "&& !OpenWydCompareIsEnabled()",
            render[
                render.index("int RenderDevice::Unlock"):
                render.index("OpenWydCompareOnBeforePresent")
            ],
        )
        self.assertIn("void AppendJsonString(", implementation)
        self.assertIn("AppendJsonString(json, wyd_socket_last_host())", implementation)
        self.assertIn("AppendJsonString(json, g_compare.resolvedServerHost)", implementation)
        self.assertIn("AppendJsonString(json, pngFilename)", implementation)
        self.assertIn("OpenWydCompareConsumeMouseState", event_translator)
        compare_mouse = event_translator.index(
            "if (OpenWydCompareIsEnabled())",
            event_translator.index("int EventTranslator::ReadInputEventData"),
        )
        consume_mouse = event_translator.index(
            "OpenWydCompareConsumeMouseState", compare_mouse
        )
        physical_mouse = event_translator.index(
            "m_pMouseDevice->GetDeviceState", consume_mouse
        )
        self.assertLess(compare_mouse, consume_mouse)
        self.assertLess(consume_mouse, physical_mouse)
        consume_state = implementation.index(
            "bool OpenWydCompareConsumeMouseState"
        )
        consume_state_end = implementation.index(
            "bool OpenWydCompareTakeInjectedKeyMessage", consume_state
        )
        consume_body = implementation[consume_state:consume_state_end]
        self.assertIn("g_compare.mouseDeltaX = 0;", consume_body)
        self.assertIn("g_compare.mouseDeltaY = 0;", consume_body)
        self.assertIn("g_compare.mouseWheel = 0;", consume_body)
        self.assertIn("keyLParam | kInjectedKeyMessage", implementation)
        self.assertIn("OpenWydCompareTakeInjectedKeyMessage", new_app)
        injected_key = new_app.index("OpenWydCompareTakeInjectedKeyMessage")
        physical_key = new_app.index("GetKeyState(VK_CONTROL)", injected_key)
        self.assertLess(injected_key, physical_key)
        self.assertIn('ClCompile Include="OpenWydCompare.cpp"', project)
        self.assertIn('ClInclude Include="OpenWydCompare.h"', project)
        self.assertIn("<RuntimeLibrary>MultiThreadedDebug</RuntimeLibrary>", compare_props)

    def test_compare_random_uses_msvcrt_and_has_exact_inactive_fallback(self) -> None:
        header = (NATIVE_ROOT / "OpenWydCompareRandom.h").read_text("utf-8")
        implementation = (NATIVE_ROOT / "OpenWydCompareRandom.cpp").read_text(
            "utf-8"
        )
        pch = (NATIVE_ROOT / "pch.h").read_text("utf-8")
        compare = (NATIVE_ROOT / "OpenWydCompare.cpp").read_text("utf-8")

        state = 1
        values = []
        for _ in range(5):
            state = (state * 214013 + 2531011) & 0xFFFFFFFF
            values.append((state >> 16) & 0x7FFF)
        self.assertEqual(values, [41, 18467, 6334, 26500, 19169])

        self.assertIn("state * 214013u + 2531011u", header)
        self.assertIn("static_assert(", header)
        self.assertIn("return std::rand();", implementation)
        self.assertIn("std::srand(requestedSeed);", implementation)
        self.assertIn(
            "g_compareRandom.state = g_compareRandom.configuredSeed;",
            implementation,
        )
        self.assertIn("#define rand OpenWydCompareRandomRand", pch)
        self.assertIn("#define srand OpenWydCompareRandomSrand", pch)
        self.assertIn('if (command == "RANDOM_SEED")', compare)
        self.assertIn("OPENWYD_COMPARE_RANDOM_SEED", compare)
        self.assertIn("random_seed_not_prearmed", compare)
        self.assertIn("random_seed_conflict", compare)
        random_command = compare.index('if (command == "RANDOM_SEED")')
        random_command_end = compare.index('if (command != "STEP")', random_command)
        self.assertNotIn(
            "OpenWydCompareRandomArm(",
            compare[random_command:random_command_end],
        )
        for field in (
            '\\"bytes_sent\\":',
            '\\"bytes_received\\":',
            '\\"last_sent_opcode\\":',
            '\\"last_received_opcode\\":',
            '\\"random\\":{',
        ):
            self.assertIn(field, compare)


if __name__ == "__main__":
    unittest.main()
