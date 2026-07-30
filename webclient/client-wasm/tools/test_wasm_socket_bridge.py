#!/usr/bin/env python3
"""Static contract tests for the WASM WSAAsyncSelect compatibility path."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
CPSOCK = (REPO_ROOT / "Projects/TMProject/CPSock.cpp").read_text(encoding="utf-8")
STUBS = (
    REPO_ROOT / "webclient/client-wasm/compat/src/win32_emscripten_stubs.cpp"
).read_text(encoding="utf-8")
WINSOCK = (
    REPO_ROOT / "webclient/client-wasm/compat/include/winsock.h"
).read_text(encoding="utf-8")


def function_body(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]


class WasmSocketBridgeContractTests(unittest.TestCase):
    def test_select_reply_uses_winsock_layout(self) -> None:
        expected_constants = {
            "FD_READ": "0x01",
            "FD_CONNECT": "0x10",
            "FD_CLOSE": "0x20",
        }
        for name, value in expected_constants.items():
            self.assertRegex(WINSOCK, rf"#define\s+{name}\s+{value}\b")

        self.assertIn(
            "#define WSAMAKESELECTREPLY(event, error) MAKELONG(event, error)",
            WINSOCK,
        )
        self.assertIn("#define WSAGETSELECTEVENT(lParam) LOWORD(lParam)", WINSOCK)
        self.assertIn("#define WSAGETSELECTERROR(lParam) HIWORD(lParam)", WINSOCK)

    def test_each_socket_owns_its_async_select_registration(self) -> None:
        for field in (
            "HWND async_window",
            "unsigned int async_message",
            "long async_events",
        ):
            self.assertIn(field, CPSOCK)

        post_event = function_body(
            CPSOCK, "bool WydWasmPostSelectEvent", "void WydWasmNotifyConnect"
        )
        self.assertIn("(sock.async_events & event) == 0", post_event)
        self.assertRegex(
            post_event,
            re.compile(
                r"PostMessageA\(\s*sock\.async_window,\s*"
                r"sock\.async_message,\s*static_cast<WPARAM>\(sock\.handle\),\s*"
                r"static_cast<LPARAM>\(WSAMAKESELECTREPLY\(event, error\)\)",
                re.DOTALL,
            ),
        )

    def test_websocket_payload_is_buffered_verbatim_before_fd_read(self) -> None:
        on_message = function_body(
            CPSOCK, "bool WydWasmOnMessage", "bool WydWasmOnError"
        )
        append = "it->second.recv_buffer.push_back(event->data[i]);"
        notify = "WydWasmNotifyRead(it->second);"
        self.assertIn(append, on_message)
        self.assertIn(notify, on_message)
        self.assertLess(on_message.index(append), on_message.index(notify))

        receive = function_body(
            CPSOCK, "int WydSocketRecvBytes", "void WydSocketCloseHandle"
        )
        self.assertIn(
            "data[i] = static_cast<char>(wasm_sock.recv_buffer.front());",
            receive,
        )
        self.assertIn("wasm_sock.recv_buffer.pop_front();", receive)

    def test_connect_uses_the_original_read_and_close_mask(self) -> None:
        connect = function_body(
            CPSOCK,
            "unsigned int CPSock::ConnectServer",
            "unsigned int CPSock::SingleConnect",
        )
        self.assertRegex(
            connect,
            re.compile(
                r"WSAAsyncSelect\(\s*static_cast<SOCKET>\(tSock\),\s*"
                r"hWndMain,\s*static_cast<unsigned int>\(WSA\),\s*"
                r"FD_READ \| FD_CLOSE\)",
                re.DOTALL,
            ),
        )

    def test_compat_wsa_registration_reaches_cpsock_registry(self) -> None:
        self.assertIn(
            "return wyd_wasm_socket_async_select(socket, window, message, events);",
            STUBS,
        )
        self.assertIn(
            "return WydWasmSocketAsyncSelect(socket, window, message, events);",
            CPSOCK,
        )

    def test_queued_messages_reach_the_registered_window_procedure(self) -> None:
        self.assertIn("WNDPROC wnd_proc = nullptr;", STUBS)
        self.assertIn(
            "g_window_classes[wndClass->lpszClassName] = wndClass->lpfnWndProc;",
            STUBS,
        )
        self.assertIn("st.wnd_proc = class_it->second;", STUBS)

        dispatch = function_body(
            STUBS, "LRESULT DispatchMessageA", "void PostQuitMessage"
        )
        self.assertIn(
            "return wnd_proc(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);",
            dispatch,
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
