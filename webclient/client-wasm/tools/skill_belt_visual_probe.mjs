#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import playwrightPkg from "../../node_modules/playwright/index.js";
import { chromiumLaunchOptions } from "../../tools/playwright_portable_browser.mjs";

const { chromium } = playwrightPkg;
const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");
const label = process.argv[2] || "current";
const baseUrl = process.argv[3] ||
  "http://127.0.0.1:8877/webclient/client-wasm/build/link/startup_harness.html";
const outputDir = path.join(
  repoRoot,
  "webclient/client-wasm/build/reports/skill-belt-visual-probe",
  label,
);
const profileDir = path.join(
  repoRoot,
  "webclient/client-wasm/build/cache/skill-belt-visual-probe-profile",
);
fs.mkdirSync(outputDir, { recursive: true });
fs.mkdirSync(profileDir, { recursive: true });

const allRuns = [
  { name: "legacy-800x600", mode: "legacy", width: 800, height: 600 },
  { name: "optimized-800x600", mode: "optimized", width: 800, height: 600 },
  { name: "optimized-1920x1080", mode: "optimized", width: 1920, height: 1080 },
];
const requestedRuns = new Set((process.env.OPENWYD_SKILL_RUNS || "")
  .split(",")
  .map(value => value.trim())
  .filter(Boolean));
const runs = requestedRuns.size
  ? allRuns.filter(run => requestedRuns.has(run.name))
  : allRuns;
const result = { ok: false, label, runs: [], errors: [] };

async function runProbe(context, run) {
  const page = await context.newPage();
  await page.setViewportSize({ width: run.width, height: run.height });
  const cdp = await context.newCDPSession(page);
  page.on("console", message => {
    if (message.type() === "error") result.errors.push(`${run.name}: ${message.text()}`);
  });
  page.on("pageerror", error => result.errors.push(`${run.name}: ${error.message}`));

  const url = new URL(baseUrl);
  url.searchParams.set("mode", "play");
  url.searchParams.set("displayMode", run.mode);
  url.searchParams.set("quality", "quality");
  url.searchParams.set("uiScale", "100");
  url.searchParams.set("fps", "60");
  url.searchParams.set("state", "0");
  url.searchParams.set("fieldMode", "real");
  url.searchParams.set("tickMs", "16");
  url.searchParams.set("autoboot", "0");
  url.searchParams.set("autostart", "0");
  url.searchParams.set("quiet", "1");

  try {
    await page.goto(url.toString(), { waitUntil: "load", timeout: 240000 });
    await page.waitForFunction(() => window.__runtimeReady === true, null, { timeout: 240000 });
    const metrics = await page.evaluate(() => {
      window.stopAutoTick?.();
      document.body.classList.remove("loading");
      document.querySelector(".display-controls")?.style.setProperty("display", "none", "important");
      if (window.Module) {
        Module.print = () => {};
        Module.printErr = () => {};
      }
      Module._wyd_debug_set_fake_time?.(0);
      Module._wyd_compare_random_arm?.(0x4f50454e);
      if (Module._wyd_boot_client(0) !== 1) throw new Error("boot failed");
      Module._wyd_set_field_mode(1);
      Module._wyd_set_game_state(0);
      for (let index = 0; index < 60; index += 1) {
        Module._wyd_debug_advance_fake_time?.(16);
        Module._wyd_tick_client();
      }
      const populated = Module._wyd_control_audit_populate_skill_belt();
      for (let index = 0; index < 3; index += 1) {
        Module._wyd_debug_advance_fake_time?.(16);
        Module._wyd_tick_client();
      }
      const canvas = document.getElementById("canvas");
      const canvasRect = canvas.getBoundingClientRect();
      const grid = id => {
        const count = Module._wyd_control_grid_item_count(id);
        return {
          id,
          x: Module._wyd_control_abs_x(id),
          y: Module._wyd_control_abs_y(id),
          width: Module._wyd_control_width(id),
          height: Module._wyd_control_height(id),
          rows: Module._wyd_control_grid_rows(id),
          columns: Module._wyd_control_grid_columns(id),
          itemCount: count,
          itemWidths: Array.from({ length: count }, (_, i) => Module._wyd_control_grid_item_width(id, i)),
          itemHeights: Array.from({ length: count }, (_, i) => Module._wyd_control_grid_item_height(id, i)),
        };
      };
      return {
        populated,
        canvas: {
          x: canvasRect.x,
          y: canvasRect.y,
          cssWidth: canvasRect.width,
          cssHeight: canvasRect.height,
          backingWidth: canvas.width,
          backingHeight: canvas.height,
        },
        grids: [grid(65644), grid(65645)],
        glErrors: Module._wyd_d3d9_gl_error_total(),
      };
    });

    const capture = await cdp.send("Page.captureScreenshot", {
      format: "png",
      fromSurface: true,
      captureBeyondViewport: false,
      clip: {
        x: metrics.canvas.x,
        y: metrics.canvas.y,
        width: metrics.canvas.cssWidth,
        height: metrics.canvas.cssHeight,
        scale: 1,
      },
    });
    const screenshot = path.join(outputDir, `${run.name}.png`);
    fs.writeFileSync(screenshot, Buffer.from(capture.data, "base64"));
    return { ...run, url: url.toString(), metrics, screenshot: path.relative(repoRoot, screenshot).replaceAll("\\", "/") };
  } finally {
    await page.close();
  }
}

let context;
try {
  context = await chromium.launchPersistentContext(profileDir, {
    ...chromiumLaunchOptions({ headless: true }),
    headless: true,
    viewport: { width: 800, height: 600 },
    deviceScaleFactor: 1,
  });
  for (const existingPage of context.pages()) await existingPage.close();
  for (const run of runs) result.runs.push(await runProbe(context, run));
  result.ok = result.errors.length === 0 && result.runs.every(run => (
    run.metrics.populated === 20 && run.metrics.glErrors === 0 &&
    run.metrics.grids.every(grid => grid.itemCount === 10)
  ));
} catch (error) {
  result.errors.push(error?.stack || error?.message || String(error));
} finally {
  await context?.close();
  fs.writeFileSync(path.join(outputDir, "report.json"), `${JSON.stringify(result, null, 2)}\n`);
}

console.log(JSON.stringify({
  ok: result.ok,
  output: path.relative(repoRoot, outputDir).replaceAll("\\", "/"),
  runs: result.runs.map(run => ({ name: run.name, metrics: run.metrics })),
  errors: result.errors,
}, null, 2));
process.exit(result.ok ? 0 : 1);
