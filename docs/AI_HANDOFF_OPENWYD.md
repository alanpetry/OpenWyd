# OpenWyd AI Handoff

## Project

OpenWyd is a migration of the original WYD2 Windows DirectX client to a WebAssembly/WebGL client that runs in the browser. The goal is not to rewrite or redesign the game. The goal is runtime/environment migration: the same client behavior, assets, layout, text, camera, animations, UI, render order, and timing should remain as close as possible to the original DirectX client.

The current focus is the startup/server-selection screen. The official visual reference is:

`/Users/alanpetry/Documents/WYD2/Print Telas Oficiais/Seleção de Servidor.JPG`

Always compare current render output to this reference after visual/rendering changes. This project has many false-positive situations where counters improve but the screen is visually wrong.

## Important rule

Do not manually recreate the screen. Do not replace text, assets, positions, camera, demo data, or UI layout with approximations. Fix the DirectX/WASM/WebGL bridge, asset loading, original client code path, or emulation layer so the original client renders correctly in the browser.

## Repository layout

- `Projects/TMProject/`: original/decompiled C++ game client sources.
- `Projects/TMProject/TMSelectServerScene.cpp`: current main target for server-selection behavior, demo humans, camera, UI, and telemetry.
- `Projects/TMProject/TMHuman.cpp`: original human animation, mount, weapon, skin mesh logic. Avoid broad changes here unless the bug is proven global.
- `webclient/client-wasm/compat/include/`: Win32/D3D/D3DX compatibility headers for Emscripten.
- `webclient/client-wasm/compat/src/win32_emscripten_stubs.cpp`: core Win32/D3D9/D3DX/WebGL bridge, FFP emulation, shader path, depth/blend/fog/text telemetry.
- `webclient/client-wasm/compat/src/wyd_client_entry.cpp`: browser/WASM entry points.
- `webclient/client-wasm/tools/`: build, link, Playwright smoke tests, shader/catalog/debug scripts.
- `webclient/client-wasm/config/startup-preload-manifest.txt`: files preloaded into the startup WASM virtual FS.
- `webclient/client-wasm/build/link/startup_harness.html`: startup harness used by Playwright/manual testing. Build outputs in the same directory are generated and ignored.
- `v769ClientRelease/`: original client assets used by the port.

## Build and test commands

From repository root:

```bash
bash webclient/client-wasm/tools/run_tmproject_wasm_objects.sh
bash webclient/client-wasm/tools/run_tmproject_wasm_startup_link.sh
```

Smoke test at official logical resolution:

```bash
/Users/alanpetry/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node \
  webclient/client-wasm/tools/playwright_startup_smoke.mjs \
  --url 'http://127.0.0.1:8877/webclient/client-wasm/build/link/startup_harness.html?autoboot=0&autostart=0&quiet=1&fit=actual&logical=1280x720' \
  --ticks 120 --tick-ms 100 --debug-flags 0 --layout capture \
  --logical 1280x720 --viewport 1280x720 \
  --screenshot webclient/client-wasm/build/reports/startup-official-logical-capture.png \
  > webclient/client-wasm/build/reports/startup-official-logical-capture.json
```

The harness/test now separates logical backbuffer size from browser CSS size. Use official logical resolutions first, especially `1280x720`, so validation matches the reference and does not look like camera zoom.

## Current startup-screen state

Recent validated metrics from the 1280x720 smoke:

- `glErrorTotal = 0`
- `depthWriteGuardForcedDraws = 0`
- `shaderDrawsSkipped = 0`
- `skinSuspiciousTextureDraws = 0`
- Fog is active in the final shader path.
- Mounted humans have mount meshes present; body/mount animation must come from the original `TMHuman` pipeline.
- `nHumanAni` from `demo*.bin` is captured for telemetry only. The decompiled select-server reset path does not apply it directly.

Screenshots from recent runs:

- `webclient/client-wasm/build/reports/startup-official-logical-capture-t120-demoani-loop.png`
- `webclient/client-wasm/build/reports/startup-official-logical-capture-t300-demoani-loop.png`

Generated reports/screenshots are ignored by git.

## Important fixes already made

### Preload manifest

`AniSound4.txt` was missing from the WASM preload. Adding:

`v769ClientRelease/AniSound4.txt@/AniSound4.txt`

fixed a major part of animation table resolution. Before this, mounted characters often collapsed to `skinAni=0` and `mountAni=0`.

### FVF / vertex shader state

In real D3D9, `SetFVF()` belongs to the fixed-function path and should not leave a custom vertex shader active. In the WebGL bridge, fixed-function draws after skinned draws could inherit the skin vertex shader state. This affected common mesh draws, including weapons.

`WydD3D9Device_SetFVF()` now clears the active vertex shader and active VS hash. `DummyD3DXSprite::RestoreDeviceState()` restores FVF before shader state so sprite draws do not destroy the restored shader.

### Server-selection animation parity

`demo*.bin` records include `nHumanAni`, but the decompiled official `TMSelectServerScene::ResetDemoPlayer()` path creates each `TMHuman`, applies look/mount/weapon/angle/position/speed, calls `InitObject()`/`CheckWeapon()`, adds it to the human container, then lets `TMHuman::FrameMove()` and route logic choose animation. It does not call `SetAnimation(nHumanAni)` during reset or from `MoveHuman()`.

The WASM client now treats `nHumanAni` as telemetry only. Do not reintroduce select-server-only pose or walk/run overrides unless the official client code proves that path exists. If characters slide or attack unexpectedly, debug `TMHuman::FrameMove()`, route state, animation tables, skin/mount timing, or weapon attachment instead of forcing a scene-local animation.

Two parity bugs were fixed after removing the scene-local animation overrides:

- `TMSelectServerScene::InitializeScene()` had been hardcoded to `m_nDemoType = 1`; the official client uses `GetLocalTime().wSecond % 3`.
- `TMHuman::FrameMove()` was recalculating speed with `SetSpeed(0)` when a normal walk/run route ended. The decompiled client only recalculates speed for the `m_cOnlyMove` idle path. The incorrect call made demo humans fall back to score-derived speed `1`, causing slow/sliding or odd late-demo movement.

### Fog

Fog was moved to the final shader path instead of being pre-applied incorrectly in vertex color. Fragment shader fog uses `D3DRS_FOGSTART`, `D3DRS_FOGEND`, `D3DRS_FOGDENSITY`, fog mode, range fog when enabled, and `uFogColor`. Apply fog after texture stages and alpha test, leaving alpha unchanged.

### Depth / terrain / characters

Earlier debugging found that using terrain with depth test disabled can hide terrain blur/blue-cell bugs but is not a final answer when tree render order puts humans before ground objects. The target default is DX9-like depth behavior with no broad depth-write guard.

Current criterion: `depthWriteGuardForcedDraws = 0`.

## Known remaining work

1. Compare the latest 1280x720 capture against the official reference frame by frame.
2. Validate actual walk/run motion in a harness scenario that exercises demo movement. Some smoke runs show route progress but zero position delta, so confirm whether the harness is advancing the exact original demo timing path.
3. Verify mount seating throughout the full demo, especially after route transitions.
4. Verify weapon attachment and pose for both mounted and non-mounted humans across several timestamps.
5. Finish visual parity of server UI text/fonts later. Text is not the current top priority.
6. Continue `FVF 322` sub-classification by texture/name for shadows, lightmaps, rain, bright and pattern effects, instead of treating the entire FVF as one behavior bucket.
7. Keep inspecting texture stage state leakage when a skin/weapon draw looks like terrain/water/sky.

## Debug telemetry

The startup smoke can export rich telemetry:

- Draw order: first sky/skin/terrain/water/FVF322/FVF530.
- FVF buckets and depth-write/depth-test counts.
- Clip-W rejects grouped by signature.
- Skin suspicious texture samples.
- Fog state and fog-enabled draw counts.
- Per-human select-server telemetry: position, delta, height, mount status, motion, sent motion, skin animation, mount animation, FPS, weapon type, head index, route target/progress, and `demoAni`.

Useful exports include:

- `_wyd_selserver_human_demo_ani`
- `_wyd_selserver_human_motion`
- `_wyd_selserver_human_skin_ani`
- `_wyd_selserver_human_mount_present`
- `_wyd_selserver_human_mount_ani`
- `_wyd_selserver_human_progress_rate`
- `_wyd_d3d9_fog_enabled`
- `_wyd_d3d9_fog_start`
- `_wyd_d3d9_fog_end`
- `_wyd_d3d9_fog_color`

## Collaboration workflow

Multiple people/agents may work on this repo. Before starting work:

```bash
git fetch --all --prune
git status --short --branch
git pull --ff-only
```

If local changes exist, inspect them first and never overwrite other people's work. Use branches for larger changes.

Before finishing a change:

```bash
bash webclient/client-wasm/tools/run_tmproject_wasm_objects.sh
bash webclient/client-wasm/tools/run_tmproject_wasm_startup_link.sh
```

For visual/rendering changes, run the 1280x720 startup smoke and compare against the official reference image. Do not rely only on metrics.

## Notes about a Linux VPS / Wine reference setup

A useful future debugging setup is possible:

1. Run the original Windows client under Wine on an Ubuntu VPS.
2. Use Xvfb or a virtual display plus software/GL rendering if there is no GPU.
3. Add temporary instrumentation to the Windows client build to dump camera matrices, render states, FVF/declaration data, texture names, animation/mount state, and per-tick screenshots.
4. Run the WASM harness for the same tick sequence and compare telemetry/screenshot output.

This can greatly improve pipeline parity debugging, especially for D3D state, render order, animation timing, and matrix conventions. It is not guaranteed that the original client will render perfectly in headless Wine without GPU/DirectX setup, but it is realistic enough to try. If Wine rendering is unstable, the fallback is still valuable: run the instrumented Windows client locally or in a GPU-enabled VM and compare exported telemetry against WASM.
