# OpenWyd source-driven comparison controller

This debug-only tool starts the components built from the current source tree,
captures native/WebGL frames, and compares explicitly paired frame IDs. It
never discovers or reuses old game/server executables. Commands for DBSrv,
TMSrv, proxies, HTTP servers, the freshly built native client, and a browser
controller are explicit JSON arrays.

The original deterministic PNG comparator remains available as the `compare`
subcommand and through the historical positional CLI.

## Setup

Python 3.10 or newer is required. Pillow is the only Python runtime dependency
and is pinned in `requirements.txt`:

```powershell
python -m venv ../.tools/openwyd-compare-venv
../.tools/openwyd-compare-venv/Scripts/python.exe -m pip install -r tools/openwyd_compare/requirements.txt
```

The sibling `.tools` directory is local tooling outside the checkout, matching
the repository's existing development setup.

WebGL capture also uses the Playwright dependency pinned by
`webclient/package-lock.json` and a Chromium-family browser. The helper honors
the repository's portable-browser resolver and
`PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH`; `doctor` checks both the package and the
resolved browser before a capture run.

## Usage

Run from the repository root:

```powershell
python -m tools.openwyd_compare doctor `
  --config tools/openwyd_compare/controller.example.json

python -m tools.openwyd_compare run `
  --config path/to/local-controller.json

python -m tools.openwyd_compare compare `
  artifacts/captures/directx/frame-42.png `
  artifacts/captures/webgl/frame-42.png `
  --frame-id 42
```

`run` creates a unique directory below
`artifacts/openwyd_compare/runs/`. The repository ignores the complete
`artifacts/` tree. `run.json` is updated throughout the run with resolved
commands, explicitly configured environment variables, working directories,
PIDs, readiness evidence, action outputs, errors, and reverse shutdown order.
Each process writes a dedicated log.

`controller.example.json` lists disabled slots for DBSrv, TMSrv, proxy, HTTP,
native client, and browser controller. Copy it to a local ignored path, replace
the command placeholders with current source-build outputs/scripts, then
enable only the components needed for a run. Commands are arrays and execute
without a shell.

Build and prepare the real Windows DBSrv/TMSrv stack with
`tools/build_windows_servers_from_source.ps1`; its input must be an external or
ignored source bundle, and its runtime contains only the newly built server
EXEs plus filtered data. See `docs/windows-server-stack.md` for ports, startup
order, clean shutdown, and flat-file snapshot/restore. For a standalone local
stack, `tools/run_windows_servers.ps1` validates generated hashes, starts
DBSrv/TMSrv in order, records PID/readiness evidence, and drives their official
clean shutdown paths.

Readiness can be one object or an array of objects, all of which must pass:

```json
{
  "readiness": [
    {
      "type": "process",
      "min_uptime_seconds": 0.5,
      "timeout_seconds": 20
    },
    {
      "type": "tcp",
      "host": "127.0.0.1",
      "port": 7514,
      "timeout_seconds": 20
    },
    {
      "type": "log",
      "pattern": "READY",
      "timeout_seconds": 20
    }
  ]
}
```

After every configured process is ready, captures and comparisons run in
configuration order. Processes are then terminated in reverse startup order,
including on readiness/action failure or Ctrl+C.

The generic Python controller does not itself assign simulation ticks. Use its
explicit browser-controller process slot to run the paired driver below when
the native client was built with `OPENWYD_COMPARE`.

Build and prepare that native client only through the source-build outputs:

```powershell
powershell -ExecutionPolicy Bypass `
  -File tools/build_windows_source.ps1 -OpenWydCompare
powershell -ExecutionPolicy Bypass `
  -File tools/prepare_windows_client_runtime.ps1 `
  -BuiltClient artifacts/native-build/TMProject/Debug-compare/bin/WYD.exe `
  -RuntimeRoot artifacts/openwyd_compare/native-runtime
```

The runtime preparer requires the adjacent `build-metadata.json` and verifies
its executable path, SHA-256, size, PE machine, object count, checkout,
project, Debug/Win32 configuration, and `OPENWYD_COMPARE` flag. Existing game
EXEs/DLLs in the asset tree are excluded; the completed runtime contains
exactly the verified source-built `WYD.exe`.

## Paired tick and official input driver

`paired_tick_runner.mjs` connects to the source-built native client's named
pipe, boots WASM through `_wyd_boot_client(0)`, and advances both clients under
the same monotonically increasing frame ID and controlled millisecond clock.
It never calls `_wyd_set_game_state`. Every scheduled input traverses the
native Win32 message path and the corresponding original WASM
`_wyd_mouse_event` or `_wyd_key_event` export.
The runner sets the current controlled clock before applying WASM inputs, so
both input handlers observe the same frame time. `--native-artifacts` and
`--output` must name distinct, non-overlapping directories that are new or
empty. The runner never deletes their contents; it writes a run UUID before
the first step and rejects stale native PNG/JSON files by timestamp, frame ID,
compare tick, and controlled time.

Run the script with `webclient` as the working directory so Node resolves its
pinned Playwright dependency:

```powershell
Push-Location webclient
node ..\tools\openwyd_compare\paired_tick_runner.mjs `
  --url "http://127.0.0.1:8877/webclient/client-wasm/build/link/startup_harness.html" `
  --pipe OpenWyd.Compare.Native.1 `
  --native-artifacts ..\artifacts\openwyd_compare\native `
  --output ..\artifacts\openwyd_compare\paired `
  --frame-start 1 `
  --frame-count 40 `
  --time-start-ms 0 `
  --tick-ms 16 `
  --random-seed 123456789 `
  --max-wasm-pumps 4096 `
  --actions-json ..\artifacts\openwyd_compare\login-actions.json
Pop-Location
```

`--random-seed` is optional. When present, launch the native process with the
same value in `OPENWYD_COMPARE_RANDOM_SEED`; the compare build arms it at the
start of `wWinMain`, before `NewApp` construction. The runner arms WASM before
calling `_wyd_boot_client`, then uses `RANDOM_SEED`/`RANDOM_SEEDED` to verify
that the native process was pre-armed with the identical uint32 value. The
handshake never resets counters or hides pre-boot consumption. The comparison
generator implements the official MSVCRT sequence. An original
`srand(GetServerTime())` made while armed resets to the shared external seed
on both clients. When the option/environment variable is absent, both wrappers
remain disarmed and fall through to their platform CRTs. RNG seed, state, and
call counters are stored in both frame snapshots.

The action document is versioned. Actions are grouped by `frame_id` and keep
their JSON order within that frame:

```json
{
  "schema": "openwyd.paired-input-actions",
  "schema_version": 1,
  "actions": [
    {
      "frame_id": 3,
      "type": "mouse_move",
      "x": 510,
      "y": 440
    },
    {
      "frame_id": 4,
      "type": "mouse_down",
      "button": "LEFT",
      "x": 510,
      "y": 440
    },
    {
      "frame_id": 4,
      "type": "mouse_up",
      "button": "LEFT",
      "x": 510,
      "y": 440
    },
    {
      "frame_id": 8,
      "type": "text",
      "native_text": "CMPNATIVE",
      "wasm_text": "CMPWASM"
    },
    {
      "frame_id": 12,
      "type": "text",
      "text": "compare123"
    },
    {
      "frame_id": 13,
      "type": "key_down",
      "key": 13
    },
    {
      "frame_id": 13,
      "type": "key_up",
      "key": 13
    }
  ]
}
```

Supported types are `mouse_move`, `mouse_down`, `mouse_up`, `key_down`,
`key_up`, `char`, and `text`; mouse buttons are `LEFT` and `RIGHT`. A `char`
uses `char`, or the pair `native_char`/`wasm_char`. A `text` similarly uses
`text`, or `native_text`/`wasm_text`, which permits equivalent test accounts
when the server rejects simultaneous use of one login. Text conversion is
strict CP1252: an unrepresentable code point fails the run instead of silently
substituting a character.

Before each `STEP`, the runner waits for every native `INPUT_QUEUED` response
and checks its frame and strictly monotonic input sequence. The WASM side then
pumps `_wyd_tick_client` as many times as needed to drain queued Win32/socket
messages, keeping the controlled time fixed, until exactly one `Present` is
observed. Zero Presents at the configured pump limit or more than one Present
is a hard error. `paired-run.json` and every WASM snapshot record the pump and
input counts.

## Reporting a completed paired run

Turn the runner's manifest into a complete report from the repository root:

```powershell
$python = "..\.tools\openwyd-compare-venv\Scripts\python.exe"
$pairedRun = "artifacts\openwyd_compare\paired\paired-run.json"
$reportDir = "artifacts\openwyd_compare\reports\login-001"

& $python -m tools.openwyd_compare report-paired `
  $pairedRun `
  --output-dir $reportDir
```

The output directory must be new or empty, which prevents an earlier frame
from being mistaken for part of the current run. `report.json` validates the
paired-run schema, strictly increasing frame IDs, both shared snapshot
schemas, matching snapshot/frame IDs, matching `ticks.compare_frame` and
`clock.controlled_time_ms` on both clients, declared resolution, and the
existence of all source artifacts. It provides per-frame and aggregate RMS,
changed pixel count/percentage, and SSIM, plus the first divergent frame.
For seeded runs it also requires and compares the normalized RNG seed, state,
`rand`/`srand` counters, and last requested seed, reporting the first internal
state or RNG mismatch independently of pixel differences.

Each `frames/frame-<20-digit-id>/` directory contains:

- `directx.png` and `webgl.png`, exact byte-for-byte copies of the captures.
- `directx.snapshot.json` and `webgl.snapshot.json`, exact copies validated
  against the shared frame schema.
- `reference.normalized.png` and `candidate.normalized.png`.
- `diff.absolute.png`, `diff.heatmap.png`, and the per-frame `report.json`.

Defaults are deliberately exact: identity orientation, strict dimensions,
RGBA comparison, and threshold zero. If capture evidence proves an explicit
normalization is necessary, record it on the command line; for example:

```powershell
& $python -m tools.openwyd_compare report-paired `
  $pairedRun `
  --output-dir "artifacts\openwyd_compare\reports\login-flip-y" `
  --candidate-orientation flip-y `
  --alpha-mode opaque
```

Every selected normalization is recorded in the aggregate and per-frame JSON.

## Exact WebGL backing-canvas capture

A capture action launches `capture_webgl_canvas.mjs` through Node/Playwright.
It requires the selected `HTMLCanvasElement` backing store to be exactly
800x600. PNG bytes come from `canvas.toBlob("image/png")`, with
`canvas.toDataURL("image/png")` only as a fallback. It never uses a CSS/page
screenshot.

```json
{
  "captures": [
    {
      "name": "select-server",
      "frame_id": 42,
      "url": "http://127.0.0.1:8000/startup_harness.html",
      "selector": "#canvas",
      "wait_expression": "globalThis.__openwydReady === true",
      "metadata_expression": "globalThis.__openwydDebugFrame",
      "reference_png": "captures/native/frame-42.png",
      "timeout_seconds": 30
    }
  ],
  "node_cwd": "../../webclient"
}
```

The optional metadata expression supplies the shared per-frame record. Missing
sections are initialized to empty values. The version-1 contract is in
`frame.schema.json` and requires `frame_id`, `state`, `ticks`, `clock`,
`camera`, `matrices`, `draws`, `render`, `network`, and `extensions`.
Producer-specific/future fields belong below `extensions`.

Synthetic or already captured pairs can be compared during the same run:

```json
{
  "comparisons": [
    {
      "frame_id": 42,
      "reference_png": "captures/native/frame-42.png",
      "candidate_png": "captures/webgl/frame-42.png",
      "options": {
        "threshold": 0,
        "size_policy": "strict"
      }
    }
  ]
}
```

## Comparator controls

The comparator does not alter either capture unless normalization is explicit.
Its defaults are strict: different dimensions fail, every RGBA channel
difference counts, and the changed-pixel threshold is zero. Any normalization
that could conceal a mismatch is recorded in the report.

Useful explicit normalization options:

```powershell
# WebGL readback is vertically inverted.
python -m tools.openwyd_compare compare dx.png webgl.png `
  --frame-id 42 `
  --candidate-orientation flip-y

# Normalize both captures to the official base resolution.
python -m tools.openwyd_compare compare dx.png webgl.png `
  --frame-id 42 `
  --target-size 800x600 `
  --resize-filter nearest
```

Other controls:

- `--size-policy strict|reference|candidate`: fail on dimension mismatch by
  default, or explicitly resize one side.
- `--alpha-mode compare|opaque`: compare alpha exactly by default, or force
  both captures to alpha 255.
- `--threshold N`: a pixel changes only when its maximum compared-channel
  delta is greater than `N`. The default is exact (`0`).
- `--heatmap-gain N`: changes only heatmap visibility, never the metrics or
  absolute diff.
- `--no-ssim`: omit SSIM if only exact metrics are wanted.

Each frame directory contains:

- `reference.normalized.png`
- `candidate.normalized.png`
- `diff.absolute.png` (absolute RGB deltas, opaque for reliable viewing)
- `diff.heatmap.png` (maximum RGBA/RGB delta according to alpha mode)
- `report.json`

The report has no timestamps or machine-specific absolute paths. It records
input and normalized-pixel SHA-256 hashes, all normalization choices, RMS and
mean absolute deltas, per-channel metrics, changed-pixel count/percentage,
maximum delta, and an 8x8 non-overlapping RGB SSIM score. SSIM is supplemental:
exact changed-pixel and RMS metrics remain present and are never relaxed by
SSIM.

## Tests

```powershell
python -m unittest discover -s tools/openwyd_compare/tests -v
node --test tools/openwyd_compare/tests/test_paired_tick_runner.mjs
```

Tests use only dummy Python child processes and synthetic PNGs. They cover all
three readiness modes, reverse teardown after success/failure, manifest data,
the shared frame schema, exact-canvas helper constraints, comparison
integration, thresholds, alpha normalization, orientation, size policy, SSIM,
and deterministic JSON. They do not start the real OpenWyd servers or client.
The Node suite additionally covers native event parsing, action scheduling,
strict CP1252 conversion, distinct client account text, original WASM input
exports, and bounded multi-pump `Present` behavior.
