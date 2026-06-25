# Select-Server Visual Snapshot Protocol

This protocol keeps WASM/WebGL validation tied to the original WYD2 client image instead of relying on internal counters alone. Use it when changing rendering, camera, animation, mesh, FVF, depth, shader, preload, timing, or select-server scene code.

## Canonical Viewport

- Browser viewport: `1280x720` logical pixels.
- Canvas: keep the game canvas at the same aspect ratio as the official select-server reference.
- Reference image: `Selecao de Servidor.JPG` from the agent reference files.
- Capture point: after the scene has booted, assets have loaded, and at least one stable rendered frame has completed.

The 1280x720 viewport is required because the current reference uses this aspect ratio. Do not accept a screenshot taken from a browser window that makes the canvas look zoomed or letterboxed differently from the reference.

## Required Screenshot Evidence

For any visual change, attach or store the following evidence with the branch or PR notes:

- current WASM select-server screenshot at 1280x720;
- the official reference screenshot used for comparison;
- a short visual assessment of terrain, humans, mounts, weapons, sky/fog horizon, water/bright effects, UI placement, and text legibility;
- whether the screenshot was captured after a deterministic fake-time step or during live timing.

## Required Telemetry Snapshot

Record these counters alongside the screenshot so visual differences can be correlated with engine state:

- `glErrorTotal`;
- `depthWriteGuardForcedDraws`;
- `shaderDrawsSkipped`;
- `skinSuspiciousTextureDraws`;
- draw-order first/count values for sky, skin, terrain FVF 594, water FVF 578, FVF 530, and FVF 322;
- camera validity and camera position/angles;
- per-human presence, position, visibility, mount, motion, sent motion, skin animation, mount animation, weapon type index, demo animation, progress rate, and delta position.

The current accepted baseline expects:

- `glErrorTotal = 0`;
- `depthWriteGuardForcedDraws = 0`;
- `shaderDrawsSkipped = 0`;
- `skinSuspiciousTextureDraws = 0`;
- characters and mounts visible;
- fog visible on the horizon;
- idle select-server humans reapply `nHumanAni` from `demo2.bin` for varied poses and weapons.

A metric match is not sufficient by itself. If the screenshot does not visually match the reference, keep the branch experimental and describe the mismatch.

## Acceptance Rules

A visual change can be treated as an improvement only when:

- the 1280x720 screenshot is closer to the official reference in at least one named area;
- the baseline counters above do not regress;
- no workaround disables a major render path, depth behavior, shader path, or scene object without evidence from the original client;
- any remaining mismatch is described in the PR so the next execution can continue from concrete evidence.

## Backlog Focus

Use this protocol first for these active migration questions:

- frame-by-frame comparison of select-server poses, weapons, mounts, and camera;
- validating real walk/run route progress, especially cases where route progress changes but position delta stays zero;
- terrain, water, shadows, lightmap, rain, bright effects, and FVF 322 parity;
- text and font parity after the primary graphical paths are stable.
