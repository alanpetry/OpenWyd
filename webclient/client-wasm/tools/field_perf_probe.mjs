#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import playwrightPkg from "../../node_modules/playwright/index.js";
import { chromiumLaunchOptions } from "../../tools/playwright_portable_browser.mjs";

const { chromium } = playwrightPkg;
const REPO_ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname), "../../..");

function parseArgs(argv) {
  const opts = {
    url: "http://127.0.0.1:8877/webclient/client-wasm/build/link/startup_harness.html",
    viewport: { width: 1220, height: 720 },
    logical: { width: 800, height: 600 },
    layout: "panel",
    tickMs: 100,
    warmupTicks: 60,
    samples: 10,
    ticksPerSample: 45,
    report: "webclient/client-wasm/build/reports/field-perf-probe.json",
    screenshot: "webclient/client-wasm/build/reports/field-perf-probe.png",
  };

  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === "--url" && argv[i + 1]) opts.url = argv[++i];
    else if (arg === "--samples" && argv[i + 1]) opts.samples = Number.parseInt(argv[++i], 10) || opts.samples;
    else if (arg === "--ticks-per-sample" && argv[i + 1]) opts.ticksPerSample = Number.parseInt(argv[++i], 10) || opts.ticksPerSample;
    else if (arg === "--tick-ms" && argv[i + 1]) opts.tickMs = Number.parseInt(argv[++i], 10) || 0;
    else if (arg === "--report" && argv[i + 1]) opts.report = argv[++i];
    else if (arg === "--screenshot" && argv[i + 1]) opts.screenshot = argv[++i];
  }
  return opts;
}

function harnessUrl(opts) {
  const url = new URL(opts.url);
  url.searchParams.set("state", "0");
  url.searchParams.set("logical", `${opts.logical.width}x${opts.logical.height}`);
  url.searchParams.set("layout", opts.layout);
  url.searchParams.set("autostart", "0");
  url.searchParams.set("tickMs", `${opts.tickMs}`);
  return url.toString();
}

async function logicalToPagePoint(page, logicalX, logicalY) {
  return page.evaluate(({ logicalX: x, logicalY: y }) => {
    const canvas = document.getElementById("canvas");
    const rect = canvas.getBoundingClientRect();
    return {
      x: rect.left + (Number(x) * rect.width) / canvas.width,
      y: rect.top + (Number(y) * rect.height) / canvas.height,
    };
  }, { logicalX, logicalY });
}

async function tickAndRead(page, count, tickMs) {
  return page.evaluate(({ count: tickCount, tickMs: tickDelta }) => {
    const M = window.Module || {};
    const call = (name, ...args) => (typeof M[name] === "function" ? M[name](...args) : null);
    if (typeof M._wyd_d3d9_reset_debug_counters === "function") M._wyd_d3d9_reset_debug_counters();
    const start = performance.now();
    for (let i = 0; i < tickCount; i += 1) {
      if (tickDelta > 0 && typeof M._wyd_debug_advance_fake_time === "function") {
        M._wyd_debug_advance_fake_time(tickDelta >>> 0);
      }
      M._wyd_tick_client();
    }
    const elapsedMs = performance.now() - start;
    const fvfBuckets = [];
    const bucketSize = call("_wyd_field_visual_fvf_bucket_size") || 0;
    for (let i = 0; i < Math.min(bucketSize, 8); i += 1) {
      fvfBuckets.push({
        fvf: call("_wyd_field_visual_fvf_bucket_code", i),
        count: call("_wyd_field_visual_fvf_bucket_count", i),
      });
    }
    return {
      elapsedMs,
      avgTickMs: elapsedMs / Math.max(1, tickCount),
      position: [call("_wyd_field_myhuman_x"), call("_wyd_field_myhuman_y")],
      route: {
        motion: call("_wyd_field_myhuman_motion"),
        moving: call("_wyd_field_myhuman_moving"),
        progress: call("_wyd_field_myhuman_progress_rate"),
        last: call("_wyd_field_myhuman_last_route_index"),
        max: call("_wyd_field_myhuman_max_route_index"),
        target: [call("_wyd_field_myhuman_target_x"), call("_wyd_field_myhuman_target_y")],
      },
      camera: {
        x: call("_wyd_debug_camera_x"),
        y: call("_wyd_debug_camera_y"),
        z: call("_wyd_debug_camera_z"),
        h: call("_wyd_debug_camera_h"),
        v: call("_wyd_debug_camera_v"),
        sight: call("_wyd_debug_camera_sight_length"),
      },
      draws: {
        total: call("_wyd_d3d9_draw_calls"),
        primitives: call("_wyd_d3d9_primitives"),
        glErrors: call("_wyd_d3d9_gl_error_total"),
        fieldTotal: call("_wyd_field_visual_total_draws"),
        terrain: call("_wyd_field_visual_terrain_draws"),
        water: call("_wyd_field_visual_water_draws"),
        sky: call("_wyd_field_visual_sky_draws"),
        human: call("_wyd_field_visual_human_draws"),
        object: call("_wyd_field_visual_object_draws"),
        effect: call("_wyd_field_visual_effect_draws"),
        hud: call("_wyd_field_visual_hud_draws"),
        fvfBuckets,
        indexedCompact: {
          draws: call("_wyd_d3d9_indexed_compact_draws"),
          sourceVertices: call("_wyd_d3d9_indexed_compact_source_vertices"),
          decodedVertices: call("_wyd_d3d9_indexed_compact_vertices_decoded"),
          savedVertices: call("_wyd_d3d9_indexed_compact_vertices_saved"),
        },
        declIndexedCompact: {
          draws: call("_wyd_d3d9_decl_indexed_compact_draws"),
          sourceVertices: call("_wyd_d3d9_decl_indexed_compact_source_vertices"),
          decodedVertices: call("_wyd_d3d9_decl_indexed_compact_vertices_decoded"),
          savedVertices: call("_wyd_d3d9_decl_indexed_compact_vertices_saved"),
        },
        fvf530Compact: {
          draws: call("_wyd_d3d9_fvf530_indexed_compact_draws"),
          sourceVertices: call("_wyd_d3d9_fvf530_indexed_compact_source_vertices"),
          decodedVertices: call("_wyd_d3d9_fvf530_indexed_compact_vertices_decoded"),
          savedVertices: call("_wyd_d3d9_fvf530_indexed_compact_vertices_saved"),
        },
      },
    };
  }, { count, tickMs });
}

async function chooseTopClick(page) {
  return page.evaluate(() => {
    const M = window.Module || {};
    const call = (name, ...args) => (typeof M[name] === "function" ? M[name](...args) : null);
    const humanX = call("_wyd_field_myhuman_x");
    const humanY = call("_wyd_field_myhuman_y");
    const candidates = [
      [400, 95], [360, 105], [440, 105], [320, 125], [480, 125],
      [400, 145], [350, 160], [450, 160], [300, 180], [500, 180],
      [400, 210], [340, 220], [460, 220],
    ];
    const attempts = [];
    for (const [x, y] of candidates) {
      const valid = call("_wyd_field_pick_at", x, y);
      const pickX = call("_wyd_field_last_pick_x");
      const pickY = call("_wyd_field_last_pick_y");
      const pickZ = call("_wyd_field_last_pick_z");
      const distance = Math.hypot((pickX ?? 0) - (humanX ?? 0), (pickZ ?? 0) - (humanY ?? 0));
      const attempt = { logicalX: x, logicalY: y, valid, pickX, pickY, pickZ, distance };
      attempts.push(attempt);
      if (valid === 1 && distance > 2.0) return { ...attempt, attempts };
    }
    return { logicalX: 400, logicalY: 180, valid: 0, attempts };
  });
}

async function clickLogical(page, x, y) {
  const point = await logicalToPagePoint(page, x, y);
  await page.mouse.move(point.x, point.y);
  await page.mouse.down({ button: "left" });
  await page.evaluate(() => Module._wyd_tick_client());
  await page.mouse.up({ button: "left" });
  return point;
}

const opts = parseArgs(process.argv);
const browser = await chromium.launch(chromiumLaunchOptions({ headless: true }));
const page = await browser.newPage({ viewport: opts.viewport });
const consoleLog = [];
page.on("console", (msg) => consoleLog.push(`[${msg.type()}] ${msg.text()}`));
page.on("pageerror", (err) => consoleLog.push(`[pageerror] ${err?.message || err}`));

const result = {
  ok: false,
  url: harnessUrl(opts),
  samples: [],
  consoleLog,
  screenshot: opts.screenshot,
};

try {
  await page.goto(result.url, { waitUntil: "load", timeout: 30000 });
  await page.waitForFunction(
    () => window.__runtimeReady === true || /runtime initialized/.test(document.getElementById("log")?.textContent || ""),
    { timeout: 30000 },
  );
  await page.evaluate(() => {
    if (typeof window.stopAutoTick === "function") window.stopAutoTick();
    if (typeof window.write === "function") window.write = () => {};
    if (window.Module) {
      Module.print = () => {};
      Module.printErr = () => {};
    }
  });
  result.boot = await page.evaluate(() => Module._wyd_boot_client(0));
  await page.evaluate(() => {
    Module._wyd_set_field_mode(1);
    Module._wyd_set_game_state(0);
  });
  result.warmup = await tickAndRead(page, opts.warmupTicks, opts.tickMs);

  for (let i = 0; i < opts.samples; i += 1) {
    const click = await chooseTopClick(page);
    const pagePoint = await clickLogical(page, click.logicalX, click.logicalY);
    const metrics = await tickAndRead(page, opts.ticksPerSample, opts.tickMs);
    result.samples.push({ index: i, click, pagePoint, metrics });
  }

  await page.locator("#canvas").screenshot({ path: path.resolve(REPO_ROOT, opts.screenshot) });
  result.shutdown = await page.evaluate(() => Module._wyd_shutdown_client());
  result.ok = result.boot === 1 &&
    result.shutdown === 1 &&
    result.samples.every((sample) => sample.metrics.draws.glErrors === 0) &&
    consoleLog.every((line) => !line.startsWith("[error]") && !line.startsWith("[pageerror]"));
} catch (err) {
  result.error = err?.stack || err?.message || String(err);
} finally {
  await browser.close();
}

const reportPath = path.resolve(REPO_ROOT, opts.report);
fs.mkdirSync(path.dirname(reportPath), { recursive: true });
fs.writeFileSync(reportPath, JSON.stringify(result, null, 2), "utf-8");
console.log(JSON.stringify({
  ok: result.ok,
  report: opts.report,
  screenshot: opts.screenshot,
  warmup: result.warmup,
  samples: result.samples.map((sample) => ({
    index: sample.index,
    click: sample.click,
    avgTickMs: sample.metrics.avgTickMs,
    position: sample.metrics.position,
    target: sample.metrics.route.target,
    draws: sample.metrics.draws,
  })),
  error: result.error || null,
}, null, 2));
process.exit(result.ok ? 0 : 1);
