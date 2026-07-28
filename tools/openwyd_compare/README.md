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

This first controller version does not freeze, advance, or synchronize native
and WASM simulation ticks by itself. A capture's `frame_id` and debug metadata
come from configuration/the producer. Deterministic tick coordination is a
later protocol layer; callers must currently arrange matching states before
declaring two frames paired.

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
```

Tests use only dummy Python child processes and synthetic PNGs. They cover all
three readiness modes, reverse teardown after success/failure, manifest data,
the shared frame schema, exact-canvas helper constraints, comparison
integration, thresholds, alpha normalization, orientation, size policy, SSIM,
and deterministic JSON. They do not start the real OpenWyd servers or client.
