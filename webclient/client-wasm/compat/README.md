# WASM Win32 compatibility notes

## Socket event delivery

`CPSock` keeps the original `WSAAsyncSelect` contract in browser builds. Each
WebSocket-backed socket stores its registered window, message ID, and event
mask. Browser callbacks only transport bytes and publish the corresponding
WinSock-style notification:

- `open` publishes `FD_CONNECT` when the caller requested it;
- a binary WebSocket message appends every byte unchanged and publishes
  `FD_READ`;
- an error or close publishes `FD_CONNECT` failure (while connecting) and/or
  `FD_CLOSE`.

Notifications use the Win32 layout expected by the original client:
`wParam` is the socket handle and `lParam` is
`WSAMAKESELECTREPLY(event, error)`. `CPSock::ConnectServer` requests the same
`FD_READ | FD_CLOSE` mask (`33`) as the native path, so `NewApp::MsgProc`
continues to receive `WM_USER + 1` and `WM_USER + 100` without a browser-only
protocol path.

The compatibility message queue retains the `WNDPROC` supplied to
`RegisterClass` and calls it from `DispatchMessage`. This is required for
posted socket notifications to follow the same queue and `MsgProc` path as
Windows.

Run the focused contract tests with:

```powershell
python webclient/client-wasm/tools/test_wasm_socket_bridge.py
```
