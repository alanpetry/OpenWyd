# Select-server motion telemetry gap

## Context

The current validated baseline already captures several select-server human movement fields inside `Projects/TMProject/TMSelectServerScene.cpp` under the Emscripten-only `WydSelServerHumanTelemetry` struct:

- `moving` from `TMHuman::m_bMoveing`
- `sliding` from `TMHuman::m_bSliding`
- `lastRouteIndex` from `TMHuman::m_nLastRouteIndex`
- `maxRouteIndex` from `TMHuman::m_nMaxRouteIndex`
- `maxSpeed` from `TMHuman::m_fMaxSpeed`
- `progressRate` from `TMHuman::m_fProgressRate`
- `targetX` / `targetY` from `TMHuman::m_vecTargetPos`
- `deltaX` / `deltaY` from frame-to-frame position deltas

The exported harness surface currently exposes `sliding`, `maxSpeed`, `progressRate`, target position and deltas, but it does not expose the route-state fields `moving`, `lastRouteIndex`, or `maxRouteIndex`.

## Hypothesis

The pending route validation issue needs to distinguish three cases that currently look too similar from the outside:

1. the character never entered a route
2. the character entered a route but route progress is stuck
3. the route advances while world position does not change

Exporting the already-captured route-state fields should make the Playwright smoke harness able to report those cases explicitly before changing `TMHuman.cpp` or select-server movement behavior.

## Minimal code change to apply next

Add Emscripten accessors in `Projects/TMProject/TMSelectServerScene.cpp` beside the existing `wyd_selserver_human_sliding` accessor:

```cpp
extern "C" int wyd_selserver_human_moving(unsigned int index)
{
	auto entry = WydSelServerHumanTelemetryAt(index);
	return entry ? entry->moving : 0;
}

extern "C" int wyd_selserver_human_last_route_index(unsigned int index)
{
	auto entry = WydSelServerHumanTelemetryAt(index);
	return entry ? entry->lastRouteIndex : 0;
}

extern "C" int wyd_selserver_human_max_route_index(unsigned int index)
{
	auto entry = WydSelServerHumanTelemetryAt(index);
	return entry ? entry->maxRouteIndex : 0;
}
```

Then add these symbols to `extra_exports` in `webclient/client-wasm/tools/link_tmproject_wasm_startup.py`:

```python
"_wyd_selserver_human_moving",
"_wyd_selserver_human_last_route_index",
"_wyd_selserver_human_max_route_index",
```

## Acceptance checks

Run the normal startup build flow:

```bash
bash webclient/client-wasm/tools/run_tmproject_wasm_objects.sh
bash webclient/client-wasm/tools/run_tmproject_wasm_startup_link.sh
```

Then run the select-server visual smoke at logical `1280x720` and compare the screenshot with the official `Selecao de Servidor.JPG` reference. For movement validation, the harness should log or assert at least one present demo human with:

- `moving != 0` after the demo run starts
- `maxRouteIndex > 0` for routed humans
- route index/progress and `deltaX`/`deltaY` changing consistently over sampled frames

## Risk

This is an observability-only change. It should not alter rendering, animation selection, camera, route calculation, timing, assets, or draw order. The main risk is a stale export list causing link failure if the accessors are not added before exporting the symbols.
