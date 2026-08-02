#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import playwrightPkg from "../../node_modules/playwright/index.js";
import { chromiumLaunchOptions } from "../../tools/playwright_portable_browser.mjs";

const { chromium } = playwrightPkg;
const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");
const baseUrl = process.argv[2] ||
  "http://127.0.0.1:8877/webclient/client-wasm/build/link/startup_harness.html";
const reportPath = path.join(
  repoRoot,
  "webclient/client-wasm/build/reports/optimized-viewport-matrix-smoke.json",
);
const groups = [
  { dpr: 1, sizes: [[1280, 720], [3440, 1440]] },
  { dpr: 1.25, sizes: [[1920, 1080], [3840, 2160]] },
  { dpr: 2, sizes: [[1280, 720], [2560, 1440]] },
];

const browser = await chromium.launch(chromiumLaunchOptions({ headless: true }));
const result = { ok: false, samples: [], consoleErrors: [] };

try {
  for (const group of groups) {
    const [initialWidth, initialHeight] = group.sizes[0];
    const context = await browser.newContext({
      viewport: { width: initialWidth, height: initialHeight },
      deviceScaleFactor: group.dpr,
    });
    const page = await context.newPage();
    page.on("console", message => {
      if (message.type() === "error") result.consoleErrors.push(message.text());
    });
    page.on("pageerror", error => result.consoleErrors.push(error?.message || String(error)));

    const target = new URL(baseUrl);
    target.searchParams.set("mode", "play");
    target.searchParams.set("displayMode", "optimized");
    target.searchParams.set("quality", "quality");
    target.searchParams.set("fps", "60");
    target.searchParams.set("state", "7");
    target.searchParams.set("autoboot", "1");
    target.searchParams.set("autostart", "1");
    await page.goto(target.toString(), { waitUntil: "load", timeout: 120000 });
    await page.waitForFunction(() => (
      window.__runtimeReady === true && Module?._wyd_get_game_state?.() === 7
    ), null, { timeout: 120000 });

    for (const [width, height] of group.sizes) {
      await page.setViewportSize({ width, height });
      await page.waitForFunction(({ width: expectedWidth, height: expectedHeight }) => (
        Module?._wyd_optimized_css_width?.() === expectedWidth &&
        Module?._wyd_optimized_css_height?.() === expectedHeight
      ), { width, height }, { timeout: 15000 });
      await page.mouse.move(Math.floor(width / 2), Math.floor(height / 2));
      await page.waitForTimeout(250);
      const sample = await page.evaluate(({ dpr }) => {
        const canvas = document.getElementById("canvas");
        const rect = canvas.getBoundingClientRect();
        return {
          cssWidth: Math.round(rect.width),
          cssHeight: Math.round(rect.height),
          backingWidth: canvas.width,
          backingHeight: canvas.height,
          backingPixels: canvas.width * canvas.height,
          configuredDpr: dpr,
          browserDpr: devicePixelRatio,
          runtimeWidth: Module._wyd_optimized_css_width(),
          runtimeHeight: Module._wyd_optimized_css_height(),
          mouseX: Module._wyd_input_mouse_x(),
          mouseY: Module._wyd_input_mouse_y(),
          glErrors: Module._wyd_d3d9_gl_error_total(),
          worldSamples: Module._wyd_d3d9_optimized_world_samples(),
          navigationEntries: performance.getEntriesByType("navigation").length,
        };
      }, { dpr: group.dpr });
      result.samples.push(sample);
    }
    await context.close();
  }

  result.ok = result.consoleErrors.length === 0 && result.samples.every(sample => (
    sample.cssWidth === sample.runtimeWidth &&
    sample.cssHeight === sample.runtimeHeight &&
    sample.backingPixels <= 12 * 1024 * 1024 &&
    sample.backingWidth >= sample.cssWidth &&
    sample.backingHeight >= sample.cssHeight &&
    Math.abs(sample.mouseX - sample.cssWidth / 2) <= 2 &&
    Math.abs(sample.mouseY - sample.cssHeight / 2) <= 2 &&
    sample.glErrors === 0 && sample.worldSamples >= 2 &&
    sample.navigationEntries === 1
  ));
} catch (error) {
  result.error = error?.stack || error?.message || String(error);
} finally {
  fs.mkdirSync(path.dirname(reportPath), { recursive: true });
  fs.writeFileSync(reportPath, `${JSON.stringify(result, null, 2)}\n`);
  await browser.close();
}

console.log(JSON.stringify(result, null, 2));
process.exit(result.ok ? 0 : 1);
