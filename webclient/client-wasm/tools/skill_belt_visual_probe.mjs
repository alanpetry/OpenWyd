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
          itemCells: Array.from({ length: count }, (_, i) => Module._wyd_control_grid_item_cell_x(id, i)),
          itemSIndices: Array.from({ length: count }, (_, i) => Module._wyd_control_grid_item_sindex(id, i)),
        };
      };
      const initialGrids = [grid(65644), grid(65645)];
      const auditCount = Module._wyd_control_audit_count();
      for (let index = 0; index < auditCount; index += 1) {
        const id = Module._wyd_control_audit_id(index);
        if (id === 65644) Module._wyd_control_audit_set_raw_visible(index, 0);
        if (id === 65645) Module._wyd_control_audit_set_raw_visible(index, 1);
      }
      // Exercise the second bank as rendered state, not merely as a hidden
      // control whose item geometry still contains its authored fractions.
      Module._wyd_tick_client();
      const alternateGrids = [grid(65644), grid(65645)];
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
        grids: initialGrids,
        alternateGrids,
        glErrors: Module._wyd_d3d9_gl_error_total(),
        assignedSkills: Array.from({ length: 20 }, (_, i) => Module._wyd_control_assigned_short_skill(i)),
      };
    });

    const clickBank = (id, bankOffset) => page.evaluate(({ gridId, offset }) => (
      Array.from({ length: 10 }, (_, cell) => ({
        cell,
        expected: offset + cell,
        selected: Module._wyd_control_audit_click_skill_cell(gridId, cell),
      }))
    ), { gridId: id, offset: bankOffset });

    // Verify the complete browser-input path, not just render geometry.  The
    // second bank is already active after the setup evaluation above.
    metrics.secondBankClicks = await clickBank(65645, 10);
    await page.evaluate(() => {
      const count = Module._wyd_control_audit_count();
      for (let index = 0; index < count; index += 1) {
        const id = Module._wyd_control_audit_id(index);
        if (id === 65644) Module._wyd_control_audit_set_raw_visible(index, 1);
        if (id === 65645) Module._wyd_control_audit_set_raw_visible(index, 0);
      }
      Module._wyd_tick_client();
    });
    metrics.firstBankClicks = await clickBank(65644, 0);

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
    run.metrics.grids.every(grid => grid.itemCount === 10) &&
    run.metrics.alternateGrids.every(grid => grid.itemCount === 10) &&
    run.metrics.assignedSkills.every((skill, index) => skill === index) &&
    run.metrics.grids.every((grid, bank) => (
      grid.itemCells.every((cell, index) => cell === index) &&
      grid.itemSIndices.every((sIndex, index) => sIndex === 5000 + bank * 10 + index)
    )) &&
    run.metrics.firstBankClicks.every(click => click.selected === click.expected) &&
    run.metrics.secondBankClicks.every(click => click.selected === click.expected) &&
    // The active optimized bank must cover the official 197 logical pixels
    // exactly after physical-pixel snapping.  This catches the historic
    // icon/cooldown/shortcut-box drift in both ten-skill pages.
    (run.mode !== "optimized" || (
      Math.abs(run.metrics.grids[0].itemWidths.reduce((sum, value) => sum + value, 0) - 197) < 0.01 &&
      Math.abs(run.metrics.alternateGrids[1].itemWidths.reduce((sum, value) => sum + value, 0) - 197) < 0.01 &&
      run.metrics.grids[0].itemWidths.every(value => Number.isInteger(value)) &&
      run.metrics.alternateGrids[1].itemWidths.every(value => Number.isInteger(value))
    ))
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
