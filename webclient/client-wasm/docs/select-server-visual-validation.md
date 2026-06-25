# Select-server visual validation contract

This checklist keeps the WASM/WebGL select-server work anchored to the original WYD2 client reference instead of relying only on internal counters. Use it whenever a change touches rendering, camera, animation, draw order, preload coverage, FVF handling, shaders, depth, fog, water, shadows, lightmap, rain, brightness, fonts, or UI layout.

## Reference

- Official reference image: `agent_files/Seleção de Servidor.JPG` in scheduled agent workspaces, or the equivalent project attachment.
- Reference contract: logical `1280x720`, 16:9.
- The browser canvas and screenshot must not be zoomed, browser-scaled, or captured at a different logical size before pixel comparison.

## Required smoke path

Run the normal object and startup link checks before visual review:

```sh
bash webclient/client-wasm/tools/run_tmproject_wasm_objects.sh
bash webclient/client-wasm/tools/run_tmproject_wasm_startup_link.sh
```

The current validated baseline expects these counters to remain clean unless a change intentionally updates the acceptance criteria:

- `glErrorTotal = 0`
- `depthWriteGuardForcedDraws = 0`
- `shaderDrawsSkipped = 0`
- `skinSuspiciousTextureDraws = 0`

## Screenshot capture requirements

Capture the select-server scene at logical `1280x720` after startup has reached the stable server-selection view. Store the actual screenshot together with the run reports so visual differences can be reproduced later.

Before accepting a render-affecting change, compare the actual screenshot with the reference image by inspection. Metric helpers may be used as guardrails, but they are not a substitute for the screenshot review because camera framing, character poses, depth, UI ordering, and terrain/sky/water balance can look wrong while still producing acceptable aggregate numbers.

## Visual review checklist

Check these items against the reference image:

- Camera position, angle, vertical framing, and horizon fog.
- Demo human positions, idle poses, weapon visibility, mount visibility, and animation timing.
- Terrain draw order, depth behavior, shadows, lightmap, water, sky, rain, and bright/glow passes.
- UI layout, server list placement, logo placement, button placement, and text visibility.
- Fixed-function/FVF paths, especially FVF 322 and other screen-like draw paths.
- Shader state leakage between programmable and fixed-function draws.

## Telemetry to preserve with captures

When available, archive these values next to the screenshot because they make visual differences easier to diagnose:

- `wyd_selserver_human_*` values for pose, motion, mount, weapon, target, delta, and route progress.
- `wyd_d3d9_draw_order_*` values for sky, skin, terrain, water, FVF 530, and FVF 322.
- Texture/file-open counters and preload manifest audit output.
- Fog values, clear color, clear depth, depth function, and present-state counters.

## Acceptance rule

A change is not visually accepted until the screenshot is reviewed against the official reference. Passing smoke counters, export audits, preload audits, or image-distance thresholds only means the build is ready for visual review.

Avoid accepting workarounds that hide symptoms, such as globally disabling depth writes or skipping terrain classes, unless direct evidence shows that the original client behaves the same way in the same scene.
