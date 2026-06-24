# Graphics Debugging Workflow

This project is too large to debug graphics only by looking at screenshots. The current workflow records visual output and also traces which draw call touched selected pixels.

## External references

- Spector.js can inspect a full WebGL frame and show the command history: https://spector.babylonjs.com/
- Khronos lists Spector.js and WebGL API wrapping as WebGL debugging approaches: https://wikis.khronos.org/webgl/debugging
- `readPixels` is the standard WebGL API for reading framebuffer pixels from a canvas: https://developer.mozilla.org/en-US/docs/Web/API/WebGLRenderingContext/readPixels

Spector.js remains useful for exact WebGL state/shader inspection. For this port, the faster path is native instrumentation in the D3D9 compatibility layer, because it can attach C++ object, mesh, texture, FVF, and skip reason to each draw.

## What is instrumented

- `win32_emscripten_stubs.cpp` records per-draw FVF, screen bounds, coverage, depth/blend/alpha state, skip reason, stage 0/1 texture, and trace labels.
- `TMMesh.cpp` adds mesh-level labels such as file, effect, FVF, attribute, texture index, face count, and vertex count.
- `TMObject.cpp` adds object-level labels such as object type, position, height, angle, and alpha flags.
- `playwright_startup_smoke.mjs` can enable draw tracing and configure pixel probes.
- `visual_progress_round.mjs` stores screenshots, image stats, diff images, smoke JSON, probe overlay HTML, and trace summary JSON.
- `graphics_trace_summary.mjs` groups trace suspects by object type, mesh, texture, and reason.

## Start local harness

From the repo root:

```bash
python3 -m http.server 8877 --bind 127.0.0.1
```

Harness URL:

```text
http://127.0.0.1:8877/webclient/client-wasm/build/link/startup_harness.html
```

## Run a default trace round

```bash
node webclient/client-wasm/tools/visual_progress_round.mjs \
  --label trace-default \
  --ticks 80 \
  --trace \
  --trace-top 24 \
  --timeout-ms 45000
```

Outputs go to:

```text
webclient/client-wasm/build/reports/progress/round-XXX-trace-default/
```

## Run targeted probes

Use this when a specific bad pixel or region is visible on the screenshot:

```bash
node webclient/client-wasm/tools/visual_progress_round.mjs \
  --label trace-targeted \
  --ticks 80 \
  --trace \
  --trace-top 24 \
  --trace-probe left_silhouette:70:145 \
  --trace-probe far_right_silhouette:945:165 \
  --trace-probe lower_right_artifact:940:555 \
  --timeout-ms 45000
```

Probe coordinates are canvas pixels. Normalized coordinates from `0.0` to `1.0` also work.

## Read the output

- `canvas.png`: current screenshot.
- `diff-previous.png`: diff against the immediately previous round.
- `diff-minus5.png`: diff against five rounds back, when available.
- `diff-stable-20260605.png`: diff against the stable baseline, when available.
- `diff-summary.json`: numeric diff metrics.
- `visual-stats.json`: brightness/color metrics for full image, sky candidate, water candidate, and right characters.
- `smoke.json`: full runtime counters and raw trace data.
- `trace-probes.html`: screenshot with probe markers and decoded draw labels.
- `trace-summary.json`: compact grouped trace report.

Most day-to-day debugging should start with `trace-probes.html` and `trace-summary.json`.

## Known findings from rounds 031 and 032

Top-left and far-right dark sky silhouettes were traced to:

```text
TMMesh file=mesh\\sky001.msa
stage0=mesh/sky02.wys
stage1=mesh/sky02.wys
fvf=322
```

This means the sky is being drawn. The bug is likely in sky texture choice, sky geometry/projection, or render state, not in a missing sky draw.

The highest ranked FVF530 large-bound suspects were traced to:

```text
TMObject type=422 pos=2134.50,2116.50 h=-0.93
TMMesh file=mesh\\etc022.msa
stage0=mesh/hs0037.wys
reason=fvf530-large-bounds
```

```text
TMObject type=446 pos=2152.50,2114.50 h=0.00
TMMesh file=mesh\\etc046.msa
stage0=mesh/hs0045.wys
reason=fvf530-large-bounds
```

Older rounds also showed:

```text
TMObject type=454 pos=2114.50,2116.50 h=0.00
TMMesh file=mesh\\etc054.msa
stage0=mesh/hs0054.wys
```

## Recommended loop

1. Capture a trace round before changing code.
2. Put probes on bad pixels, not just broad regions.
3. Fix one draw family at a time, for example sky FVF322 or FVF530 large bounds.
4. Rerun the same probes and compare with previous and minus-five rounds.
5. If the symptom moves, create new probes and keep the older report for comparison.

## Limitations

The trace is a CPU-side approximation of screen-space triangles and depth. It does not fully reproduce GPU blending, alpha discard, shader side effects, or exact texture sampling. Use Spector.js when the question is exact WebGL command state, shader state, or texture binding. Use the native trace when the question is "which C++ object/mesh/texture produced this visible pixel or suspicious draw?".
