#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import playwrightPkg from "../../node_modules/playwright/index.js";
import { chromiumLaunchOptions } from "../../tools/playwright_portable_browser.mjs";

const { chromium, firefox } = playwrightPkg;
const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");
const baseUrl = process.argv[2] ||
  "http://127.0.0.1:8877/webclient/client-wasm/build/link/startup_harness.html";
const qualityProfile = process.argv[3] || "auto";
const browserName = process.argv[4] === "firefox" ? "firefox" : "chromium";
const reportPath = path.join(
  repoRoot,
  "webclient/client-wasm/build/reports/optimized-view-smoke.json",
);
const screenshotPath = path.join(
  repoRoot,
  "webclient/client-wasm/build/reports/optimized-view-smoke.png",
);
const profilePath = path.join(
  repoRoot,
  `webclient/client-wasm/build/reports/optimized-view-${browserName}-profile-v2`,
);

fs.mkdirSync(profilePath, { recursive: true });
const browserType = browserName === "firefox" ? firefox : chromium;
const launchOptions = browserName === "firefox" ? {
  headless: true,
  firefoxUserPrefs: {
    "webgl.disabled": false,
    "webgl.force-enabled": true,
  },
} : chromiumLaunchOptions({ headless: true });
const context = await browserType.launchPersistentContext(profilePath, {
  ...launchOptions,
  headless: true,
  viewport: { width: 1280, height: 720 },
  deviceScaleFactor: 1,
});
const page = await context.newPage();
const consoleErrors = [];
const consoleLog = [];
const consoleTasks = [];
page.on("console", (message) => {
  const task = (async () => {
    let rendered = message.text();
    const values = [];
    for (const argument of message.args()) {
      try {
        values.push(await argument.jsonValue());
      } catch {
        values.push(argument.toString());
      }
    }
    if (values.length && (rendered === "JSHandle@object" || rendered === "[object Object]")) {
      try {
        rendered = JSON.stringify(values.length === 1 ? values[0] : values);
      } catch {}
    }
    consoleLog.push(rendered);
    if (message.type() === "error") consoleErrors.push(rendered);
  })();
  consoleTasks.push(task);
});
page.on("pageerror", (error) => consoleErrors.push(error?.message || String(error)));

const url = new URL(baseUrl);
url.searchParams.set("mode", "play");
url.searchParams.set("displayMode", "optimized");
url.searchParams.set("quality", qualityProfile);
url.searchParams.set("fps", "60");
url.searchParams.set("state", "0");
url.searchParams.set("fieldMode", "real");
url.searchParams.set("autoboot", "1");
url.searchParams.set("autostart", "1");

const result = {
  ok: false,
  url: url.toString(),
  qualityProfile,
  browserName,
  samples: [],
  consoleErrors,
  consoleLog,
  screenshot: path.relative(repoRoot, screenshotPath).replaceAll("\\", "/"),
};

async function sample(label) {
  const value = await page.evaluate((sampleLabel) => {
    const canvas = document.getElementById("canvas");
    const rect = canvas.getBoundingClientRect();
    const module = window.Module || {};
    const call = (name) => typeof module[name] === "function" ? module[name]() : null;
    const packageResource = performance.getEntriesByType("resource")
      .find((entry) => /openwyd_assets\..*\.data(?:$|\?)/.test(entry.name));
    const readCString = (pointer) => {
      if (!pointer || !module.HEAPU8) return "";
      let end = pointer;
      while (end < module.HEAPU8.length && module.HEAPU8[end] !== 0) end += 1;
      return new TextDecoder().decode(module.HEAPU8.subarray(pointer, end));
    };
    const assetFailureCount = call("_wyd_d3d9_asset_file_open_fail_sample_count") || 0;
    return {
      label: sampleLabel,
      viewport: { width: innerWidth, height: innerHeight },
      canvas: {
        width: canvas.width,
        height: canvas.height,
        cssWidth: Math.round(rect.width),
        cssHeight: Math.round(rect.height),
      },
      runtime: {
        optimized: call("_wyd_optimized_view_enabled"),
        cssWidth: call("_wyd_optimized_css_width"),
        cssHeight: call("_wyd_optimized_css_height"),
        backingWidth: call("_wyd_optimized_backing_width"),
        backingHeight: call("_wyd_optimized_backing_height"),
        webgl2: call("_wyd_d3d9_is_webgl2"),
        worldSamples: call("_wyd_d3d9_optimized_world_samples"),
        gameState: call("_wyd_get_game_state"),
        glErrors: call("_wyd_d3d9_gl_error_total"),
        assetOpenFailures: call("_wyd_d3d9_asset_file_open_fail"),
        assetOpenFailureSamples: Array.from(
          { length: assetFailureCount },
          (_, index) => readCString(module._wyd_d3d9_asset_file_open_fail_sample(index)),
        ),
      },
      assets: {
        packageBytes: Number(window.__openwydAssetDataBytes) || 0,
        transferBytes: Number(packageResource?.transferSize) || 0,
        encodedBytes: Number(packageResource?.encodedBodySize) || 0,
        decodedBytes: Number(packageResource?.decodedBodySize) || 0,
        httpCacheReused: Boolean(
          packageResource && packageResource.transferSize === 0 &&
          packageResource.decodedBodySize > 0
        ),
      },
      navigationEntries: performance.getEntriesByType("navigation").length,
      href: location.href,
    };
  }, label);
  result.samples.push(value);
  return value;
}

try {
  await page.goto(url.toString(), { waitUntil: "load", timeout: 120000 });
  await page.waitForFunction(() => window.__runtimeReady === true, null, { timeout: 120000 });
  await page.waitForFunction(() => (
    typeof window.Module?._wyd_get_game_state === "function" &&
    window.Module._wyd_get_game_state() === 0
  ), null, { timeout: 120000 });
  await page.waitForTimeout(800);
  result.simulationSchedule = await page.evaluate(() => Object.fromEntries(
    [60, 75, 120, 144].map(fps => [fps, window.__wydSimulateFixedScheduler(fps, 10000)]),
  ));
  await sample("1280x720");

  // The game canvas is continuously presented, so Playwright's locator
  // screenshot stability wait can never settle at high FPS. Pause only the
  // presentation loop after all runtime samples, then capture the last frame.
  await page.evaluate(() => window.stopAutoTick?.());
  await page.waitForTimeout(100);
  const canvasRect = await page.locator("#canvas").boundingBox();
  if (!canvasRect) throw new Error("optimized canvas has no visible bounds");
  await page.screenshot({ path: screenshotPath, clip: canvasRect });
  await Promise.all(consoleTasks);
  const scheduleCounts = Object.values(result.simulationSchedule || {});
  result.ok = consoleErrors.length === 0 &&
    scheduleCounts.length === 4 && Math.max(...scheduleCounts) - Math.min(...scheduleCounts) <= 1 &&
    result.samples.every((entry) => (
    entry.runtime.optimized === 1 &&
    entry.runtime.webgl2 === 1 &&
    entry.runtime.worldSamples >= (
      browserName === "chromium" &&
      (qualityProfile === "quality" || qualityProfile === "maximum") ? 2 : 1
    ) &&
    entry.runtime.gameState === 0 &&
    entry.runtime.glErrors === 0 &&
    entry.assets.packageBytes > 0 &&
    entry.navigationEntries === 1 &&
    entry.canvas.cssWidth === entry.viewport.width &&
    entry.canvas.cssHeight === entry.viewport.height &&
    entry.runtime.cssWidth === entry.viewport.width &&
    entry.runtime.cssHeight === entry.viewport.height
  ));
} catch (error) {
  result.error = error?.stack || error?.message || String(error);
} finally {
  fs.mkdirSync(path.dirname(reportPath), { recursive: true });
  fs.writeFileSync(reportPath, JSON.stringify(result, null, 2));
  await context.close();
}

console.log(JSON.stringify(result, null, 2));
process.exit(result.ok ? 0 : 1);
