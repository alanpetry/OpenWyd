# Native `OPENWYD_COMPARE` bridge

This bridge exists only in a client compiled from the current source tree with
`OPENWYD_COMPARE=1`, `_DEBUG`, and without `__EMSCRIPTEN__`. A normal Debug or
Release build compiles out the initialization, gate, clock redirects, capture,
and `Present` hook.

Build it from the repository root:

```powershell
powershell -ExecutionPolicy Bypass `
  -File tools/build_windows_source.ps1 `
  -OpenWydCompare
```

The build script writes the new executable below
`artifacts/native-build/TMProject/Debug-compare/bin/`; it never uses a
previously compiled game executable. Its compare-only property sheet selects
the static debug CRT (`/MTd`), so the instrumented client does not depend on a
machine-installed `ucrtbased.dll`. The compare build also suppresses the
legacy `Change.exe` launcher side effect; no precompiled updater is executed.

## Environment

The bridge is armed only when `OPENWYD_COMPARE_PIPE` is non-empty:

```text
OPENWYD_COMPARE_PIPE=OpenWyd.Compare.Native.1
OPENWYD_COMPARE_ARTIFACTS=C:\work\OpenWyd\artifacts\openwyd_compare\native
OPENWYD_COMPARE_START_TIME_MS=0
OPENWYD_COMPARE_RANDOM_SEED=12345
OPENWYD_COMPARE_SERVER_HOST=127.0.0.1
OPENWYD_COMPARE_SERVER_PORT=8281
```

`OPENWYD_COMPARE_PIPE` may be a short local pipe name or a full
`\\.\pipe\...` path. Remote pipe clients are rejected.
`OPENWYD_COMPARE_ARTIFACTS` is optional at protocol level but is required to
produce the paired PNG and JSON files. The bridge creates the directory.
`OPENWYD_COMPARE_START_TIME_MS` is an optional unsigned 32-bit value.
The host and port are optional compare-only endpoint overrides. When present,
they are applied inside the original `CPSock` connection path immediately
before address conversion and `connect()`. They neither inspect nor alter any
network payload. With both variables absent, the server selected by the
original `serverlist.bin` path and port 8281 remain untouched. The resolved
host, port, and whether an override was active are recorded under `network` in
each snapshot.

When the pipe variable is absent, the bridge is disabled and the compare build
runs frames normally. In that case `timeGetTime` and `GetTickCount` fall
through to Windows.

## Version 1 line protocol

The client creates one duplex byte-mode named-pipe instance. Both directions
are ASCII, newline-delimited records. The controller connects as a pipe client.
Commands are:

```text
PING
INPUT 1 <frame_id> MOUSE_MOVE <x> <y>
INPUT 1 <frame_id> MOUSE_DOWN <LEFT|RIGHT> <x> <y>
INPUT 1 <frame_id> MOUSE_UP <LEFT|RIGHT> <x> <y>
INPUT 1 <frame_id> KEY_DOWN <virtual_key>
INPUT 1 <frame_id> KEY_UP <virtual_key>
INPUT 1 <frame_id> CHAR <cp1252_byte>
RANDOM_SEED <seed:uint32>
STEP <frame_id:uint64> <time_ms:uint32>
CLOSE
```

Responses are:

```text
READY 1 <pid> <width> <height> <capture_enabled:0|1>
PONG
INPUT_QUEUED <input_sequence> <frame_id>
RANDOM_SEEDED <seed:uint32>
STEP_ACCEPTED <frame_id> <time_ms>
PRESENT <frame_id> <time_ms> <capture_hresult:0xHHHHHHHH> <snapshot_written:0|1>
ERROR <stable_code>
CLOSING
BYE
```

Frame IDs must increase strictly across accepted `STEP` commands. A second
step is rejected while one is pending or active. `time_ms` is the exact DWORD
returned by both controlled clock wrappers after that command. The Python
encoder/parser and its bounds checks are in `native_pipe_protocol.py`.

`OPENWYD_COMPARE_RANDOM_SEED` is optional. When present, it is parsed and armed
at the start of `wWinMain`, before `NewApp` construction, so pre-boot random
consumption is visible and deterministic. `RANDOM_SEED` is then a verification
handshake accepted only while no frame is pending or active: it returns
`RANDOM_SEEDED` only when the process was pre-armed with that exact seed, and
otherwise returns `random_seed_not_prearmed` or `random_seed_conflict`. While
armed, client `rand()` calls use the official MSVCRT transition and every
original `srand(...)` call resets to the externally selected seed, independent
of the local server clock. Without the environment variable the wrappers call
the platform CRT unchanged. Each snapshot records armed state, configured and
requested seeds, current RNG state, and `rand`/`srand` call counts.

Every `INPUT` names the future frame that will consume it. Inputs are accepted
only before a step, must target a frame greater than the last accepted one,
and all queued inputs must target the same frame. The following `STEP` must
use that exact ID. A command sent after `STEP_ACCEPTED` is rejected instead of
silently slipping into the following frame.

Mouse coordinates are unsigned 16-bit values and must also fall inside the
actual client dimensions reported by `READY`. Buttons and virtual keys have
balanced down/up state; duplicate down and unmatched up commands are rejected.
Virtual-key values are 1..254. `CHAR` accepts one nonzero byte (1..255), passed
unchanged as `WM_CHAR`; `text_commands()` performs strict CP1252 encoding.

Accepted inputs remain stored inside the bridge until the matching `STEP`.
That command posts the stored messages, in wire order, to the game HWND and
commits its controlled clock before returning to the Windows message pump.
Immediately after polling the pipe, `NewApp::RunTick` opens the accepted frame
gate before it removes any game message from the Win32 queue. It then drains
that queue while the frame remains active, so every input and socket handler
observes the clock assigned to that step and traverses the original `MsgProc`,
`EventTranslator`, controls, and scene handlers before the frame.

With no accepted `STEP`, the pump removes only `WM_CLOSE` for the game window
or the thread's `WM_QUIT`. Input, timer, window, and socket notifications
(including the login and game `WM_USER + 1`/`WM_USER + 100` events) remain
queued until the next controlled frame. This keeps shutdown functional without
letting network state advance between logical ticks.

The bridge commits the frame ID, controlled clock, and `STEP_ACCEPTED` only
after every Win32 message was posted successfully. `PostMessage` cannot roll
back a prefix if a later post fails, so that exceptional condition returns
`ERROR input_post_failed_fatal`, makes the protocol terminal, and requests the
client's normal `WM_CLOSE` path. The controller must discard that process and
its artifacts instead of retrying the frame; subsequent commands other than
`CLOSE` receive `ERROR fatal_protocol_state`.

The historical client handles mouse-down through DirectInput rather than
forwarding its `WM_*BUTTONDOWN`. Bridge-generated messages therefore update a
compare-only DirectInput mirror and then follow the unchanged `MsgProc`:
move/up uses its existing cursor/control forwarding, while down is consumed
once by `EventTranslator::ReadInputEventData`; the bridge does not add a second
`OnMouseEvent` call. Every event updates the absolute position, relative X/Y
deltas accumulate in event order, and button state remains held across frames.
`ReadInputEventData` consumes those deltas once and zeros them exactly like the
WASM DirectInput bridge. The WASM `_wyd_mouse_event` export now mirrors this
same pairing by updating its DirectInput state and invoking the source
`NewApp::MsgProc`, not `OnMouseEvent` directly. While native compare mode is
armed it does not poll the physical DirectInput device, so host mouse movement
cannot contaminate a controlled frame.

Injected key messages carry an internal marker in a reserved keyboard
`lParam` bit, leaving the virtual key unchanged for accelerator translation.
`MsgProc` strips that marker and derives Control/Shift from the delivered
injected-key state rather than the physical `GetKeyState`. Unmarked physical
mouse, key, character, system-key, and IME-composition messages are removed
without translation or dispatch while compare mode is armed; this also keeps
physical keys from reaching `TranslateAccelerator`. Protocol-generated
`WM_CHAR` records carry the same private marker. Other lifecycle/window
messages retain their source path. No command sets a game state or invokes a
scene transition directly.

`CLOSE` posts the game's normal `WM_CLOSE`; it does not bypass Field logout
rules. `Finalize` sends `BYE`, disconnects the pipe, and releases its handle.

## Tick gate and capture point

Every `NewApp::RunTick` polls the pipe and asks the gate for an accepted step
before inspecting the general Win32 queue. One accepted step keeps the gate
active while queued messages drain and permits exactly one execution of the
original active-frame block. If that branch completes without a successful
`Present` (for example device loss, failed `EndScene`, or an active AVI path),
the bridge sends `ERROR frame_completed_without_present_fatal`, makes the
protocol terminal, and requests the normal close path instead of allowing a
later `RunTick` to continue the same logical frame.

`RenderDevice::Unlock(1)` ends the D3D scene, resolves a multisampled render
target when necessary, reads it into a system-memory surface, encodes that
surface with `D3DXSaveSurfaceToFile(..., D3DXIFF_PNG, ...)`, writes the frame
snapshot, and then invokes the real `IDirect3DDevice9::Present`. Only after
that call succeeds does the bridge send the protocol `PRESENT` response and
release the frame gate. A failed real Present instead sends
`ERROR present_failed_fatal`, makes the protocol terminal, and follows the
normal close path. The PNG and JSON still describe the Direct3D backbuffer
before the Windows compositor. Filenames are stable and lexically ordered:

The compare build still calculates `m_fFPS`, because original animation and
effect code reads it, but suppresses the `_DEBUG` FPS/object-count text overlay
while the bridge is armed. Ordinary Debug, disabled compare, and Release builds
retain their existing behavior.

The source tree's static D3DX encoder can reject PNG on some Windows/D3D9
drivers. In that case the bridge encodes the same locked system-memory BGRA
pixels through Windows Imaging Component. The JSON records
`capture_encoder`, the initial `d3dx_encode_hresult`, and the final
`capture_hresult`; this fallback never captures the window or compositor.

```text
frame_00000000000000000042.png
frame_00000000000000000042.json
```

The JSON follows `frame.schema.json`. It records the actual backbuffer
dimensions and D3D format, viewport, selected render states, game/scene state,
timer fields, controlled clock, camera vectors, world/view/projection
matrices, and the existing socket-buffer counters. Unknown or unavailable
information is not synthesized.

## Current limits

- This is only the native endpoint. It does not yet synchronize a WASM tick or
  prove parity with one.
- Startup code before the native window and bridge initialization still sees
  the Windows clocks. After arming, `timeGetTime` and `GetTickCount` are
  controlled; `time()`, QPC, audio clocks, and server time are not rewritten.
- `draws` is currently an honest empty array. Per-draw interception, packet
  opcode/hash tracing, entity/bone snapshots, and server snapshots remain
  future instrumentation; the native extension flags state that they are
  unavailable.
- If the D3D surface cannot be encoded, `PRESENT` still reports the real
  HRESULT and the JSON reports the same failure. A controller must not treat a
  failed capture as a comparable frame.
- The capture records the actual resolution. The controller/work directory
  must configure the source-built client to 800x600; the bridge does not force
  or visually rescale it.
