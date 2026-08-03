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
const outputDir = path.join(
  repoRoot,
  "webclient/client-wasm/build/reports/optimized-sharpen-compare",
);
const profileDir = path.join(
  repoRoot,
  "webclient/client-wasm/build/cache/optimized-sharpen-compare-profile",
);
fs.mkdirSync(outputDir, { recursive: true });
fs.mkdirSync(profileDir, { recursive: true });

const result = { ok: false, baseUrl, captures: {}, errors: [] };
let context;
try {
  context = await chromium.launchPersistentContext(profileDir, {
    ...chromiumLaunchOptions({ headless: true }),
    headless: true,
    viewport: { width: 1920, height: 1080 },
    deviceScaleFactor: 1,
  });
  for (const existingPage of context.pages()) await existingPage.close();
  const page = await context.newPage();
  await page.setViewportSize({ width: 1920, height: 1080 });
  page.on("console", message => {
    if (message.type() === "error") result.errors.push(message.text());
  });
  page.on("pageerror", error => result.errors.push(error.message));

  const url = new URL(baseUrl);
  for (const [key, value] of Object.entries({
    mode: "play",
    displayMode: "optimized",
    quality: "quality",
    uiScale: "100",
    fps: "60",
    state: "0",
    fieldMode: "real",
    tickMs: "16",
    autoboot: "0",
    autostart: "0",
    quiet: "1",
  })) url.searchParams.set(key, value);

  await page.goto(url.toString(), { waitUntil: "load", timeout: 240000 });
  await page.waitForFunction(() => window.__runtimeReady === true, null, { timeout: 240000 });
  await page.evaluate(() => {
    window.stopAutoTick?.();
    document.body.classList.remove("loading");
    document.querySelector(".display-controls")?.style.setProperty("display", "none", "important");
    Module.print = () => {};
    Module.printErr = () => {};
    Module._wyd_debug_set_fake_time?.(0);
    Module._wyd_compare_random_arm?.(0x4f50454e);
    if (Module._wyd_boot_client(0) !== 1) throw new Error("boot failed");
    Module._wyd_set_field_mode(1);
    Module._wyd_set_game_state(0);
    for (let index = 0; index < 60; index += 1) {
      Module._wyd_debug_advance_fake_time?.(16);
      Module._wyd_tick_client();
    }
    Module._wyd_control_audit_populate_skill_belt();
    for (let index = 0; index < 3; index += 1) {
      Module._wyd_debug_advance_fake_time?.(16);
      Module._wyd_tick_client();
    }
  });

  const capture = async (name, enabled) => {
    const telemetry = await page.evaluate(flag => {
      Module._wyd_d3d9_set_optimized_sharpen_enabled(flag);
      Module._wyd_render_client();
      return {
        enabled: Module._wyd_d3d9_optimized_sharpen_enabled(),
        strength: Module._wyd_d3d9_optimized_sharpen_strength(),
        glErrors: Module._wyd_d3d9_gl_error_total(),
        frameTime: Module._wyd_debug_get_time(),
        camera: [
          Module._wyd_debug_camera_x(),
          Module._wyd_debug_camera_y(),
          Module._wyd_debug_camera_z(),
          Module._wyd_debug_camera_h(),
          Module._wyd_debug_camera_v(),
        ],
      };
    }, enabled ? 1 : 0);
    const file = path.join(outputDir, `${name}.png`);
    await page.locator("#canvas").screenshot({ path: file });
    return {
      file: path.relative(repoRoot, file).replaceAll("\\", "/"),
      telemetry,
    };
  };

  result.captures.off = await capture("sharpen-off", false);
  result.captures.on = await capture("sharpen-on", true);
  result.ok = result.errors.length === 0 &&
    result.captures.off.telemetry.enabled === 0 &&
    result.captures.on.telemetry.enabled === 1 &&
    result.captures.off.telemetry.glErrors === 0 &&
    result.captures.on.telemetry.glErrors === 0 &&
    JSON.stringify(result.captures.off.telemetry.camera) ===
      JSON.stringify(result.captures.on.telemetry.camera) &&
    result.captures.off.telemetry.frameTime === result.captures.on.telemetry.frameTime;
  await page.close();
} catch (error) {
  result.errors.push(error?.stack || error?.message || String(error));
} finally {
  await context?.close();
  fs.writeFileSync(path.join(outputDir, "raw-report.json"), `${JSON.stringify(result, null, 2)}\n`);
}

console.log(JSON.stringify(result, null, 2));
process.exit(result.ok ? 0 : 1);
