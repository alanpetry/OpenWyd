#!/usr/bin/env node

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import playwrightPkg from "../../node_modules/playwright/index.js";
import { chromiumLaunchOptions } from "../../tools/playwright_portable_browser.mjs";

const { chromium } = playwrightPkg;
const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");
const baseUrl = process.argv[2] ||
  "http://127.0.0.1:8877/webclient/client-wasm/build/link/startup_harness.html";
const outputDir = process.env.OPENWYD_MATERIAL_HD_OUTPUT
  ? path.resolve(process.env.OPENWYD_MATERIAL_HD_OUTPUT)
  : path.join(repoRoot, "webclient/client-wasm/build/reports/optimized-material-hd-compare");
const profileDir = fs.mkdtempSync(path.join(os.tmpdir(), "openwyd-material-hd-"));
const width = Number.parseInt(process.env.OPENWYD_MATERIAL_HD_WIDTH || "1280", 10);
const height = Number.parseInt(process.env.OPENWYD_MATERIAL_HD_HEIGHT || "720", 10);
const dpr = Number.parseFloat(process.env.OPENWYD_MATERIAL_HD_DPR || "1");
fs.mkdirSync(outputDir, { recursive: true });

const result = {
  ok: false,
  baseUrl,
  viewport: { width, height, dpr },
  captures: {},
  errors: [],
};
const context = await chromium.launchPersistentContext(profileDir, {
  ...chromiumLaunchOptions({ headless: true }),
  headless: true,
  viewport: { width, height },
  deviceScaleFactor: dpr,
});
for (const page of context.pages()) await page.close();

async function capture(label, enabled) {
  const page = await context.newPage();
  await page.setViewportSize({ width, height });
  page.on("console", message => {
    if (message.type() === "error") result.errors.push(`${label}: ${message.text()}`);
  });
  page.on("pageerror", error => result.errors.push(`${label}: ${error.message}`));
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
  const telemetry = await page.evaluate(flag => {
    window.stopAutoTick?.();
    document.body.classList.remove("loading");
    document.querySelector(".display-controls")?.style.setProperty("display", "none", "important");
    Module.print = () => {};
    Module.printErr = () => {};
    Module._wyd_d3d9_set_optimized_material_hd_enabled(flag);
    Module._wyd_debug_set_fake_time?.(0);
    Module._wyd_compare_random_arm?.(0x4f50454e);
    if (Module._wyd_boot_client(0) !== 1) throw new Error("boot failed");
    Module._wyd_set_field_mode(1);
    Module._wyd_set_game_state(0);
    for (let index = 0; index < 63; index += 1) {
      Module._wyd_debug_advance_fake_time?.(16);
      Module._wyd_tick_client();
    }
    return {
      enabled: Module._wyd_d3d9_optimized_material_hd_enabled(),
      scale: Module._wyd_d3d9_optimized_material_hd_scale(),
      textures: Module._wyd_d3d9_optimized_material_hd_textures(),
      sourcePixels: Module._wyd_d3d9_optimized_material_hd_source_pixels(),
      physicalPixels: Module._wyd_d3d9_optimized_material_hd_physical_pixels(),
      processingMicroseconds: Module._wyd_d3d9_optimized_material_hd_microseconds(),
      repeatX: Module._wyd_d3d9_optimized_material_hd_repeat_x(),
      repeatY: Module._wyd_d3d9_optimized_material_hd_repeat_y(),
      modelTextures: Module._wyd_d3d9_optimized_material_hd_model_textures(),
      modelSourcePixels: Module._wyd_d3d9_optimized_material_hd_model_source_pixels(),
      samples: Array.from(
        { length: Module._wyd_d3d9_optimized_material_hd_sample_count() },
        (_, index) => Module.UTF8ToString(
          Module._wyd_d3d9_optimized_material_hd_sample(index),
        ),
      ),
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

  const canvas = page.locator("#canvas");
  const full = path.join(outputDir, `${label}-full.png`);
  await canvas.screenshot({ path: full });
  const box = await canvas.boundingBox();
  const world = path.join(outputDir, `${label}-world.png`);
  await page.screenshot({
    path: world,
    clip: {
      x: box.x,
      y: box.y,
      width: box.width,
      height: Math.max(1, box.height - 110),
    },
  });
  await page.close();
  return {
    full: path.relative(repoRoot, full).replaceAll("\\", "/"),
    world: path.relative(repoRoot, world).replaceAll("\\", "/"),
    telemetry,
  };
}

try {
  result.captures.off = await capture("material-hd-off", false);
  result.captures.on = await capture("material-hd-on", true);
  const off = result.captures.off.telemetry;
  const on = result.captures.on.telemetry;
  result.ok = result.errors.length === 0 &&
    off.enabled === 0 && off.scale === 1 && off.textures === 0 &&
    on.enabled === 1 && on.scale === 2 && on.textures > 0 &&
    on.physicalPixels === on.sourcePixels * 4 &&
    off.glErrors === 0 && on.glErrors === 0 &&
    off.frameTime === on.frameTime &&
    JSON.stringify(off.camera) === JSON.stringify(on.camera);
} catch (error) {
  result.errors.push(error?.stack || error?.message || String(error));
} finally {
  await context.close();
  fs.writeFileSync(path.join(outputDir, "report.json"), `${JSON.stringify(result, null, 2)}\n`);
}

console.log(JSON.stringify(result, null, 2));
process.exit(result.ok ? 0 : 1);
