#!/usr/bin/env node

import fs from "fs";
import path from "path";
import playwrightPkg from "../../node_modules/playwright/index.js";
import { chromiumLaunchOptions } from "../../tools/playwright_portable_browser.mjs";

const { chromium } = playwrightPkg;

const REPO_ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname), "../../..");
const DEFAULT_URL = "http://127.0.0.1:8877/webclient/client-wasm/build/link/startup_harness.html";
const DEFAULT_OUT_DIR = path.join(REPO_ROOT, "webclient/client-wasm/build/reports/temporal");
const DEFAULT_TICKS = [0, 1, 2, 5, 10, 20, 40, 80];

function sanitizeLabel(label) {
  return String(label || "temporal")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9._-]+/g, "-")
    .replace(/^-+|-+$/g, "")
    .slice(0, 80) || "temporal";
}

function parseTickList(value) {
  const ticks = String(value || "")
    .split(",")
    .map((part) => Number.parseInt(part.trim(), 10))
    .filter((tick) => Number.isFinite(tick) && tick >= 0);
  return Array.from(new Set(ticks)).sort((a, b) => a - b);
}

function parseTraceProbe(spec) {
  const parts = String(spec || "").split(":");
  if (parts.length !== 3) return null;
  const x = Number.parseFloat(parts[1]);
  const y = Number.parseFloat(parts[2]);
  if (!Number.isFinite(x) || !Number.isFinite(y)) return null;
  return { label: parts[0] || `probe${Date.now()}`, x, y };
}

function parseCameraOffset(value) {
  const parts = String(value || "")
    .split(",")
    .map((part) => Number.parseFloat(part.trim()));
  if (parts.length !== 5 || parts.some((part) => !Number.isFinite(part))) return null;
  const [dx, dy, dz, dh, dv] = parts;
  return { dx, dy, dz, dh, dv };
}

function parseArgs(argv) {
  const opts = {
    url: DEFAULT_URL,
    outDir: DEFAULT_OUT_DIR,
    label: "temporal",
    state: 7,
    debugFlags: 0,
    debugSkipFvf: 0,
    ticks: DEFAULT_TICKS,
    timeoutMs: 45000,
    trace: false,
    traceTop: 24,
    traceProbes: [],
    fixedTimeMs: null,
    tickMs: 16,
    cameraOffset: null,
  };

  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    const next = argv[i + 1];
    if (arg === "--url" && next) {
      opts.url = next;
      i += 1;
    } else if (arg === "--out-dir" && next) {
      opts.outDir = path.resolve(next);
      i += 1;
    } else if (arg === "--label" && next) {
      opts.label = next;
      i += 1;
    } else if (arg === "--state" && next) {
      opts.state = Number.parseInt(next, 10) || opts.state;
      i += 1;
    } else if (arg === "--debug-flags" && next) {
      opts.debugFlags = Number.parseInt(next, 10) || 0;
      i += 1;
    } else if (arg === "--debug-skip-fvf" && next) {
      opts.debugSkipFvf = Number.parseInt(next, 10) || 0;
      i += 1;
    } else if (arg === "--ticks" && next) {
      const ticks = parseTickList(next);
      if (ticks.length) opts.ticks = ticks;
      i += 1;
    } else if (arg === "--timeout-ms" && next) {
      opts.timeoutMs = Number.parseInt(next, 10) || opts.timeoutMs;
      i += 1;
    } else if (arg === "--trace") {
      opts.trace = true;
    } else if (arg === "--trace-top" && next) {
      opts.traceTop = Number.parseInt(next, 10) || opts.traceTop;
      i += 1;
    } else if (arg === "--fixed-time-ms" && next) {
      const parsed = Number.parseInt(next, 10);
      opts.fixedTimeMs = Number.isFinite(parsed) && parsed >= 0 ? parsed : opts.fixedTimeMs;
      i += 1;
    } else if (arg === "--tick-ms" && next) {
      const parsed = Number.parseInt(next, 10);
      opts.tickMs = Number.isFinite(parsed) && parsed >= 0 ? parsed : opts.tickMs;
      i += 1;
    } else if (arg === "--camera-offset" && next) {
      opts.cameraOffset = parseCameraOffset(next);
      i += 1;
    } else if (arg === "--trace-probe" && next) {
      const probe = parseTraceProbe(next);
      if (probe) {
        opts.trace = true;
        opts.traceProbes.push(probe);
      }
      i += 1;
    }
  }

  return opts;
}

function normalizeHarnessUrl(rawUrl) {
  const url = new URL(rawUrl);
  if (url.pathname.endsWith("/startup_harness.html")) {
    if (!url.searchParams.has("autoboot")) url.searchParams.set("autoboot", "0");
    if (!url.searchParams.has("autostart")) url.searchParams.set("autostart", "0");
    if (!url.searchParams.has("quiet")) url.searchParams.set("quiet", "1");
  }
  return url.toString();
}

function regionDefs(width, height) {
  const clampRect = (label, x, y, w, h) => ({
    label,
    x: Math.max(0, Math.min(width - 1, Math.round(x))),
    y: Math.max(0, Math.min(height - 1, Math.round(y))),
    w: Math.max(1, Math.min(width, Math.round(w))),
    h: Math.max(1, Math.min(height, Math.round(h))),
  });
  const sampleW = Math.min(width, 320);
  const sampleH = Math.min(height, 240);
  const regions = [
    clampRect("frame_sample", (width - sampleW) / 2, (height - sampleH) / 2, sampleW, sampleH),
    clampRect("sky", width * 0.05, height * 0.02, width * 0.90, height * 0.28),
    clampRect("characters_right", width * 0.55, height * 0.25, width * 0.40, height * 0.43),
    clampRect("grass_lower_left", width * 0.03, height * 0.48, width * 0.38, height * 0.42),
    clampRect("water_lower", width * 0.18, height * 0.55, width * 0.62, height * 0.38),
    clampRect("center_scene", width * 0.25, height * 0.25, width * 0.50, height * 0.45),
  ];
  for (const region of regions) {
    if (region.x + region.w > width) region.w = width - region.x;
    if (region.y + region.h > height) region.h = height - region.y;
  }
  return regions;
}

async function setupTrace(page, opts, width, height) {
  const probes = opts.traceProbes.map((probe, index) => {
    const normalized = Math.abs(probe.x) <= 1 && Math.abs(probe.y) <= 1;
    return {
      index,
      label: probe.label || `probe${index}`,
      x: normalized ? Math.round(probe.x * width) : Math.round(probe.x),
      y: normalized ? Math.round(probe.y * height) : Math.round(probe.y),
    };
  }).slice(0, 16);

  if (!opts.trace) return probes;
  await page.evaluate(({ probes: pageProbes }) => {
    if (typeof Module._wyd_d3d9_trace_clear_probes === "function") Module._wyd_d3d9_trace_clear_probes();
    if (typeof Module._wyd_d3d9_trace_set_enabled === "function") Module._wyd_d3d9_trace_set_enabled(1);
    if (typeof Module._wyd_d3d9_trace_reset === "function") Module._wyd_d3d9_trace_reset();
    if (typeof Module._wyd_d3d9_trace_set_probe === "function") {
      for (const probe of pageProbes) {
        Module._wyd_d3d9_trace_set_probe(probe.index >>> 0, Number(probe.x), Number(probe.y));
      }
    }
  }, { probes });
  return probes;
}

async function resetTrace(page, opts) {
  if (!opts.trace) return;
  await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_trace_reset === "function") Module._wyd_d3d9_trace_reset();
  });
}

async function readTrace(page, opts, probes) {
  if (!opts.trace) return { enabled: false, probes: [], top: [] };
  const payload = await page.evaluate(({ topLimit }) => {
    const text = (ptr) => (typeof Module.UTF8ToString === "function" && ptr ? Module.UTF8ToString(ptr >>> 0) : "");
    const out = {
      enabled: typeof Module._wyd_d3d9_trace_get_enabled === "function" ? Module._wyd_d3d9_trace_get_enabled() >>> 0 : 0,
      probes: [],
      top: [],
    };
    if (typeof Module._wyd_d3d9_trace_probe_capacity === "function") {
      const capacity = Module._wyd_d3d9_trace_probe_capacity() >>> 0;
      for (let i = 0; i < Math.min(capacity, 16); i += 1) {
        const enabled = typeof Module._wyd_d3d9_trace_probe_enabled === "function"
          ? Module._wyd_d3d9_trace_probe_enabled(i >>> 0) >>> 0
          : 0;
        if (!enabled) continue;
        const draw = typeof Module._wyd_d3d9_trace_probe_draw === "function"
          ? Module._wyd_d3d9_trace_probe_draw(i >>> 0) >>> 0
          : 0;
        const ptr = typeof Module._wyd_d3d9_trace_probe_result === "function"
          ? Module._wyd_d3d9_trace_probe_result(i >>> 0)
          : 0;
        out.probes.push({ index: i, draw, result: text(ptr) });
      }
    }
    if (typeof Module._wyd_d3d9_trace_top_count === "function" &&
        typeof Module._wyd_d3d9_trace_top_sample === "function") {
      const count = Module._wyd_d3d9_trace_top_count() >>> 0;
      for (let i = 0; i < Math.min(count, topLimit); i += 1) {
        const sample = text(Module._wyd_d3d9_trace_top_sample(i >>> 0));
        if (sample) out.top.push(sample);
      }
    }
    return out;
  }, { topLimit: opts.traceTop });

  return {
    enabled: payload.enabled === 1,
    probes: probes.map((probe) => {
      const hit = payload.probes.find((candidate) => candidate.index === probe.index);
      return { ...probe, draw: hit?.draw || 0, result: hit?.result || "" };
    }),
    top: payload.top,
  };
}

async function readRuntimeDebugState(page) {
  return page.evaluate(() => {
    const call = (name) => (typeof Module?.[name] === "function" ? Module[name]() >>> 0 : null);
    const callFloat = (name) => (typeof Module?.[name] === "function" ? Number(Module[name]()) : null);
    const color = call("_wyd_d3d9_last_clear_color");
    return {
      timeMs: call("_wyd_debug_get_time"),
      camera: {
        valid: call("_wyd_debug_camera_valid"),
        standalone: call("_wyd_debug_camera_standalone"),
        x: callFloat("_wyd_debug_camera_x"),
        y: callFloat("_wyd_debug_camera_y"),
        z: callFloat("_wyd_debug_camera_z"),
        h: callFloat("_wyd_debug_camera_h"),
        v: callFloat("_wyd_debug_camera_v"),
        offsetEnabled: call("_wyd_debug_demo_camera_offset_enabled"),
        offsetX: callFloat("_wyd_debug_demo_camera_offset_x"),
        offsetY: callFloat("_wyd_debug_demo_camera_offset_y"),
        offsetZ: callFloat("_wyd_debug_demo_camera_offset_z"),
        offsetH: callFloat("_wyd_debug_demo_camera_offset_h"),
        offsetV: callFloat("_wyd_debug_demo_camera_offset_v"),
      },
      clearCalls: call("_wyd_d3d9_clear_calls"),
      presentCalls: call("_wyd_d3d9_present_calls"),
      beginSceneCalls: call("_wyd_d3d9_begin_scene_calls"),
      endSceneCalls: call("_wyd_d3d9_end_scene_calls"),
      lastClearColor: color,
      lastClearColorHex: color === null ? null : `0x${color.toString(16).padStart(8, "0")}`,
      lastClearFlags: call("_wyd_d3d9_last_clear_flags"),
      lastClearTimeMs: call("_wyd_d3d9_last_clear_time_ms"),
      lastPresentTimeMs: call("_wyd_d3d9_last_present_time_ms"),
      lastPresentBlendEnabled: call("_wyd_d3d9_last_present_blend_enabled"),
      lastPresentDepthEnabled: call("_wyd_d3d9_last_present_depth_enabled"),
      lastPresentDepthWrite: call("_wyd_d3d9_last_present_depth_write"),
      lastPresentAlphaTest: call("_wyd_d3d9_last_present_alpha_test"),
      lastPresentSrcBlend: call("_wyd_d3d9_last_present_src_blend"),
      lastPresentDstBlend: call("_wyd_d3d9_last_present_dst_blend"),
      drawCalls: call("_wyd_d3d9_draw_calls"),
      primitives: call("_wyd_d3d9_primitives"),
      shaderDrawsSkipped: call("_wyd_d3d9_shader_draws_skipped"),
      shaderFileOpenAttempts: call("_wyd_d3d9_shader_file_open_attempts"),
      shaderFileOpenSuccess: call("_wyd_d3d9_shader_file_open_success"),
      shaderFileOpenFail: call("_wyd_d3d9_shader_file_open_fail"),
      shaderFileOpenSkinmesh: call("_wyd_d3d9_shader_file_open_skinmesh"),
      shaderFileOpenVSEffect: call("_wyd_d3d9_shader_file_open_vseffect"),
      shaderFileOpenPSEffect: call("_wyd_d3d9_shader_file_open_pseffect"),
      vsUniqueShaders: call("_wyd_d3d9_vs_unique_shaders"),
      psUniqueShaders: call("_wyd_d3d9_ps_unique_shaders"),
      vsBindCalls: call("_wyd_d3d9_vs_bind_calls"),
      psBindCalls: call("_wyd_d3d9_ps_bind_calls"),
      drawsWithVS: call("_wyd_d3d9_draws_with_vs"),
      drawsWithPS: call("_wyd_d3d9_draws_with_ps"),
      declDrawCalls: call("_wyd_d3d9_decl_draw_calls"),
      declSkinnedDrawCalls: call("_wyd_d3d9_decl_skinned_draw_calls"),
      declVerticesTotal: call("_wyd_d3d9_decl_vertices_total"),
      declVerticesInClip: call("_wyd_d3d9_decl_vertices_in_clip"),
      fvf3dVerticesTotal: call("_wyd_d3d9_fvf_3d_vertices_total"),
      fvf3dVerticesInClip: call("_wyd_d3d9_fvf_3d_vertices_in_clip"),
      alphaTestEnabledDraws: call("_wyd_d3d9_alpha_test_enabled_draws"),
      alphaTestDisabledDraws: call("_wyd_d3d9_alpha_test_disabled_draws"),
      blendEnabledDraws: call("_wyd_d3d9_blend_enabled_draws"),
      vegetationAlphaMaskDraws: call("_wyd_d3d9_vegetation_alpha_mask_draws"),
      vegetationAlphaBlendDraws: call("_wyd_d3d9_vegetation_alpha_blend_draws"),
      vegetationDraws: call("_wyd_d3d9_vegetation_draws"),
      depthTestDisabledDraws: call("_wyd_d3d9_depth_test_disabled_draws"),
      depthWriteDisabledDraws: call("_wyd_d3d9_depth_write_disabled_draws"),
      depthWriteGuardForcedDraws: call("_wyd_d3d9_depth_write_guard_forced_draws"),
      fvfTopCode: call("_wyd_d3d9_fvf_top_code"),
      fvfTopCount: call("_wyd_d3d9_fvf_top_count"),
      fvfDepthWriteEnabledTopCode: call("_wyd_d3d9_fvf_depth_write_enabled_top_code"),
      fvfDepthWriteEnabledTopCount: call("_wyd_d3d9_fvf_depth_write_enabled_top_count"),
      fvfDepthWriteDisabledTopCode: call("_wyd_d3d9_fvf_depth_write_disabled_top_code"),
      fvfDepthWriteDisabledTopCount: call("_wyd_d3d9_fvf_depth_write_disabled_top_count"),
      fvf530Draws: call("_wyd_d3d9_fvf530_draws"),
      fvf530LargeBoundsDraws: call("_wyd_d3d9_fvf530_large_bounds_draws"),
      fvf530LargeBoundsSkipDraws: call("_wyd_d3d9_fvf530_large_bounds_skip_draws"),
      fvf530UnstableWDraws: call("_wyd_d3d9_fvf530_unstable_w_draws"),
      textureDrawsSky: call("_wyd_d3d9_texture_draws_sky"),
      textureDrawsWater: call("_wyd_d3d9_texture_draws_water"),
      textureDrawsBright: call("_wyd_d3d9_texture_draws_bright"),
      effectDraws: call("_wyd_d3d9_effect_draws"),
      fvf322EffectDraws: call("_wyd_d3d9_fvf322_effect_draws"),
      skyRenderCalls: call("_wyd_sky_render_calls"),
      skyMeshDraws: call("_wyd_sky_mesh_draws"),
      glErrorTotal: call("_wyd_d3d9_gl_error_total"),
      glErrorLast: call("_wyd_d3d9_gl_error_last"),
    };
  });
}

function diffBuffers(current, previous) {
  if (!current || !previous || current.length !== previous.length) return null;
  let changed = 0;
  let sum = 0;
  let sq = 0;
  let maxDelta = 0;
  const pixels = current.length / 4;
  for (let i = 0; i < current.length; i += 4) {
    const dr = Math.abs(current[i] - previous[i]);
    const dg = Math.abs(current[i + 1] - previous[i + 1]);
    const db = Math.abs(current[i + 2] - previous[i + 2]);
    const d = dr + dg + db;
    if (d > 0) changed += 1;
    sum += d / 3;
    sq += ((dr * dr) + (dg * dg) + (db * db)) / 3;
    maxDelta = Math.max(maxDelta, dr, dg, db);
  }
  return {
    changedPct: pixels ? (changed / pixels) * 100 : 0,
    meanAbsRgb: pixels ? sum / pixels : 0,
    rmsRgb: pixels ? Math.sqrt(sq / pixels) : 0,
    maxDelta,
  };
}

function summarisePixels(buffer) {
  let brightness = 0;
  let grayish = 0;
  let dark = 0;
  let colored = 0;
  let alphaNonOpaque = 0;
  const pixels = buffer.length / 4;
  for (let i = 0; i < buffer.length; i += 4) {
    const r = buffer[i];
    const g = buffer[i + 1];
    const b = buffer[i + 2];
    const a = buffer[i + 3];
    const max = Math.max(r, g, b);
    const min = Math.min(r, g, b);
    const luma = (r + g + b) / 3;
    brightness += luma;
    if (max - min <= 12 && luma >= 25 && luma <= 210) grayish += 1;
    if (luma < 45) dark += 1;
    if (max - min > 35 && luma >= 25) colored += 1;
    if (a < 250) alphaNonOpaque += 1;
  }
  return {
    pixels,
    brightness: pixels ? brightness / pixels : 0,
    grayishPct: pixels ? (grayish / pixels) * 100 : 0,
    darkPct: pixels ? (dark / pixels) * 100 : 0,
    coloredPct: pixels ? (colored / pixels) * 100 : 0,
    alphaNonOpaquePct: pixels ? (alphaNonOpaque / pixels) * 100 : 0,
  };
}

async function readFrameMetrics(page, regions) {
  return page.evaluate(({ regions: pageRegions }) => {
    const canvas = document.getElementById("canvas");
    if (!canvas) return { ok: false, reason: "canvas-not-found" };
    const gl =
      canvas.getContext("webgl2") ||
      canvas.getContext("webgl") ||
      canvas.getContext("experimental-webgl");
    if (!gl) return { ok: false, reason: "no-webgl-context" };

    const summariseData = (data) => {
      let brightness = 0;
      let grayish = 0;
      let dark = 0;
      let colored = 0;
      let alphaNonOpaque = 0;
      const pixels = data.length / 4;
      for (let i = 0; i < data.length; i += 4) {
        const r = data[i];
        const g = data[i + 1];
        const b = data[i + 2];
        const a = data[i + 3];
        const max = Math.max(r, g, b);
        const min = Math.min(r, g, b);
        const luma = (r + g + b) / 3;
        brightness += luma;
        if (max - min <= 12 && luma >= 25 && luma <= 210) grayish += 1;
        if (luma < 45) dark += 1;
        if (max - min > 35 && luma >= 25) colored += 1;
        if (a < 250) alphaNonOpaque += 1;
      }
      return {
        pixels,
        brightness: pixels ? brightness / pixels : 0,
        grayishPct: pixels ? (grayish / pixels) * 100 : 0,
        darkPct: pixels ? (dark / pixels) * 100 : 0,
        coloredPct: pixels ? (colored / pixels) * 100 : 0,
        alphaNonOpaquePct: pixels ? (alphaNonOpaque / pixels) * 100 : 0,
      };
    };

    const readRegion = (region) => {
      const x = Math.max(0, Math.min(canvas.width - 1, region.x | 0));
      const yTop = Math.max(0, Math.min(canvas.height - 1, region.y | 0));
      const w = Math.max(1, Math.min(canvas.width - x, region.w | 0));
      const h = Math.max(1, Math.min(canvas.height - yTop, region.h | 0));
      const y = Math.max(0, canvas.height - yTop - h);
      const data = new Uint8Array(w * h * 4);
      gl.readPixels(x, y, w, h, gl.RGBA, gl.UNSIGNED_BYTE, data);
      return {
        label: region.label,
        x,
        yTop,
        w,
        h,
        summary: summariseData(data),
        data: region.label === "frame_sample" ? Array.from(data) : null,
      };
    };

    return {
      ok: true,
      width: canvas.width,
      height: canvas.height,
      regions: pageRegions.map(readRegion),
    };
  }, { regions });
}

function writeHtmlReport(reportPath, summary) {
  const rows = summary.frames.map((frame) => {
    const regionRows = Object.entries(frame.regions).map(([label, metrics]) => (
      `<tr><td>${label}</td><td>${metrics.brightness.toFixed(2)}</td><td>${metrics.grayishPct.toFixed(2)}%</td><td>${metrics.darkPct.toFixed(2)}%</td><td>${metrics.coloredPct.toFixed(2)}%</td></tr>`
    )).join("");
    const diffText = frame.diffFromPrevious
      ? `changed=${frame.diffFromPrevious.changedPct.toFixed(2)}% mean=${frame.diffFromPrevious.meanAbsRgb.toFixed(2)} rms=${frame.diffFromPrevious.rmsRgb.toFixed(2)}`
      : "sem diff";
    const diffFiveText = frame.diffFromFiveBack
      ? `5-back changed=${frame.diffFromFiveBack.changedPct.toFixed(2)}% mean=${frame.diffFromFiveBack.meanAbsRgb.toFixed(2)} rms=${frame.diffFromFiveBack.rmsRgb.toFixed(2)}`
      : "sem diff 5-back";
    const runtime = frame.runtime
      ? `<pre>${escapeHtml(JSON.stringify(frame.runtime, null, 2))}</pre>`
      : "";
    const probes = frame.trace?.probes?.length
      ? `<pre>${escapeHtml(frame.trace.probes.map((probe) => `${probe.label}: ${probe.result}`).join("\n"))}</pre>`
      : "";
    return `
      <section>
        <h2>tick ${frame.tick}</h2>
        <p>${diffText}</p>
        <p>${diffFiveText}</p>
        <img src="${path.basename(frame.screenshot)}" alt="tick ${frame.tick}">
        <table>
          <thead><tr><th>regiao</th><th>brilho</th><th>cinza</th><th>escuro</th><th>colorido</th></tr></thead>
          <tbody>${regionRows}</tbody>
        </table>
        ${runtime}
        ${probes}
      </section>`;
  }).join("\n");
  fs.writeFileSync(reportPath, `<!doctype html>
<meta charset="utf-8">
<title>Temporal graphics probe</title>
<style>
body{font:14px/1.4 -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;background:#151515;color:#eee;margin:24px}
section{margin:0 0 28px;padding:16px;background:#202020;border:1px solid #3a3a3a}
img{max-width:100%;display:block;border:1px solid #555;margin:10px 0}
table{border-collapse:collapse;margin-top:8px}
td,th{border:1px solid #555;padding:4px 8px;text-align:right}
td:first-child,th:first-child{text-align:left}
pre{white-space:pre-wrap;background:#111;padding:10px;border:1px solid #444}
</style>
<h1>Temporal graphics probe</h1>
<p>URL: ${escapeHtml(summary.options.url)}</p>
<p>Ticks: ${summary.options.ticks.join(", ")}</p>
${rows}
`);
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

const opts = parseArgs(process.argv);
opts.url = normalizeHarnessUrl(opts.url);
const label = sanitizeLabel(opts.label);
const stamp = new Date().toISOString().replace(/[:.]/g, "-");
const runDir = path.join(opts.outDir, `${stamp}-${label}`);
fs.mkdirSync(runDir, { recursive: true });

const result = {
  ok: false,
  startedAt: new Date().toISOString(),
  finishedAt: null,
  directory: runDir,
  options: { ...opts, outDir: runDir },
  boot: null,
  shutdown: null,
  canvas: null,
  frames: [],
  error: null,
};

const browser = await chromium.launch(chromiumLaunchOptions({ headless: true }));
const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
const consoleLog = [];
page.on("console", (msg) => consoleLog.push(`[${msg.type()}] ${msg.text()}`));
page.on("pageerror", (err) => consoleLog.push(`[pageerror] ${err.message}`));

try {
  await page.goto(opts.url, { waitUntil: "load", timeout: opts.timeoutMs });
  await page.waitForFunction(
    () => window.__runtimeReady === true || /runtime initialized/.test(document.getElementById("log")?.textContent || ""),
    { timeout: opts.timeoutMs },
  );
  await page.evaluate(() => {
    if (typeof window.stopAutoTick === "function") window.stopAutoTick();
    if (typeof window.write === "function") window.write = () => {};
    if (window.Module) {
      Module.print = () => {};
      Module.printErr = () => {};
    }
    const log = document.getElementById("log");
    if (log) log.textContent = "";
  });

  if (opts.fixedTimeMs !== null) {
    await page.evaluate((ms) => {
      if (typeof Module._wyd_debug_set_fake_time === "function") Module._wyd_debug_set_fake_time(ms >>> 0);
    }, opts.fixedTimeMs);
  }
  if (opts.cameraOffset) {
    await page.evaluate((offset) => {
      if (typeof Module._wyd_debug_set_demo_camera_offset === "function") {
        Module._wyd_debug_set_demo_camera_offset(
          Number(offset.dx),
          Number(offset.dy),
          Number(offset.dz),
          Number(offset.dh),
          Number(offset.dv),
        );
      }
    }, opts.cameraOffset);
  } else {
    await page.evaluate(() => {
      if (typeof Module._wyd_debug_clear_demo_camera_offset === "function") {
        Module._wyd_debug_clear_demo_camera_offset();
      }
    });
  }
  result.boot = await page.evaluate(() => Module._wyd_boot_client(0));
  await page.evaluate(({ state, debugFlags, debugSkipFvf }) => {
    if (typeof Module._wyd_set_game_state === "function") Module._wyd_set_game_state(state);
    if (typeof Module._wyd_d3d9_set_debug_flags === "function") Module._wyd_d3d9_set_debug_flags(debugFlags >>> 0);
    if (typeof Module._wyd_d3d9_set_debug_skip_fvf === "function") Module._wyd_d3d9_set_debug_skip_fvf(debugSkipFvf >>> 0);
    if (typeof Module._wyd_d3d9_reset_debug_counters === "function") Module._wyd_d3d9_reset_debug_counters();
    if (typeof Module._wyd_sky_reset_debug_counters === "function") Module._wyd_sky_reset_debug_counters();
  }, { state: opts.state, debugFlags: opts.debugFlags, debugSkipFvf: opts.debugSkipFvf });

  const canvas = await page.evaluate(() => {
    const canvasElement = document.getElementById("canvas");
    return { width: canvasElement?.width || 1280, height: canvasElement?.height || 720 };
  });
  result.canvas = canvas;
  const probes = await setupTrace(page, opts, canvas.width, canvas.height);
  const regions = regionDefs(canvas.width, canvas.height);
  let currentTick = 0;
  let previousFullBuffer = null;
  let firstFullBuffer = null;
  const frameSampleHistory = [];

  for (const targetTick of opts.ticks) {
    const delta = targetTick - currentTick;
    if (delta < 0) continue;
    if (delta > 0) {
      await resetTrace(page, opts);
      const tickResults = await page.evaluate(({ count, useFakeTime, tickMs }) => {
        const out = [];
        for (let i = 0; i < count; i += 1) {
          if (useFakeTime && typeof Module._wyd_debug_advance_fake_time === "function") {
            Module._wyd_debug_advance_fake_time(tickMs >>> 0);
          }
          out.push(Module._wyd_tick_client());
        }
        return out;
      }, { count: delta, useFakeTime: opts.fixedTimeMs !== null, tickMs: opts.tickMs });
      currentTick = targetTick;
      if (tickResults.some((ret) => ret < 0)) {
        throw new Error(`tick failure before target ${targetTick}: ${tickResults.join(",")}`);
      }
    }
    await page.waitForTimeout(40);

    const screenshot = path.join(runDir, `tick-${String(targetTick).padStart(4, "0")}.png`);
    await page.locator("#canvas").screenshot({ path: screenshot });
    const rawMetrics = await readFrameMetrics(page, regions);
    if (!rawMetrics.ok) throw new Error(rawMetrics.reason || "failed to read frame metrics");
    const regionMetrics = {};
    const regionBuffers = {};
    for (const region of rawMetrics.regions) {
      regionMetrics[region.label] = {
        rect: { x: region.x, y: region.yTop, w: region.w, h: region.h },
        ...region.summary,
      };
      if (region.data) regionBuffers[region.label] = Uint8Array.from(region.data);
    }
    const diffBuffer = regionBuffers.frame_sample;
    if (!firstFullBuffer) firstFullBuffer = diffBuffer;
    const trace = await readTrace(page, opts, probes);
    const runtime = await readRuntimeDebugState(page);
    const fiveBackBuffer = frameSampleHistory.length >= 5 ? frameSampleHistory[frameSampleHistory.length - 5] : null;
    result.frames.push({
      tick: targetTick,
      screenshot,
      regions: regionMetrics,
      diffFromPrevious: previousFullBuffer ? diffBuffers(diffBuffer, previousFullBuffer) : null,
      diffFromFirst: targetTick === opts.ticks[0] ? null : diffBuffers(diffBuffer, firstFullBuffer),
      diffFromFiveBack: fiveBackBuffer ? diffBuffers(diffBuffer, fiveBackBuffer) : null,
      trace,
      runtime,
    });
    previousFullBuffer = diffBuffer;
    frameSampleHistory.push(diffBuffer);
  }

  result.shutdown = await page.evaluate(() => Module._wyd_shutdown_client());
  result.ok = result.boot === 1 && result.shutdown === 1 && result.frames.length === opts.ticks.length;
} catch (err) {
  result.error = err?.message || String(err);
} finally {
  result.finishedAt = new Date().toISOString();
  result.consoleTail = consoleLog.slice(-60);
  result.consoleErrorCount = consoleLog.filter((line) => line.startsWith("[error]") || line.startsWith("[pageerror]")).length;
  result.consoleWarnCount = consoleLog.filter((line) => line.startsWith("[warning]")).length;
  const summaryPath = path.join(runDir, "summary.json");
  const reportPath = path.join(runDir, "index.html");
  fs.writeFileSync(summaryPath, `${JSON.stringify(result, null, 2)}\n`);
  if (result.frames.length) writeHtmlReport(reportPath, result);
  result.summary = summaryPath;
  result.report = fs.existsSync(reportPath) ? reportPath : null;
  console.log(JSON.stringify({
    ok: result.ok,
    directory: result.directory,
    summary: result.summary,
    report: result.report,
    frames: result.frames.map((frame) => ({
      tick: frame.tick,
      screenshot: frame.screenshot,
      frameSample: frame.regions.frame_sample,
      charactersRight: frame.regions.characters_right,
      grassLowerLeft: frame.regions.grass_lower_left,
      diffFromPrevious: frame.diffFromPrevious,
      diffFromFirst: frame.diffFromFirst,
    })),
    error: result.error,
    consoleErrorCount: result.consoleErrorCount,
    consoleWarnCount: result.consoleWarnCount,
  }, null, 2));
  await browser.close().catch(() => {});
}

process.exit(result.ok ? 0 : 1);
