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
const outputDir = process.env.OPENWYD_UI_HD_OUTPUT
  ? path.resolve(process.env.OPENWYD_UI_HD_OUTPUT)
  : path.join(repoRoot, "webclient/client-wasm/build/reports/optimized-ui-hd-compare");
const profileDir = path.join(
  repoRoot,
  "webclient/client-wasm/build/cache/optimized-ui-hd-compare-profile",
);
fs.mkdirSync(outputDir, { recursive: true });
fs.mkdirSync(profileDir, { recursive: true });

const viewportWidth = Number.parseInt(process.env.OPENWYD_UI_HD_WIDTH || "1920", 10);
const viewportHeight = Number.parseInt(process.env.OPENWYD_UI_HD_HEIGHT || "1080", 10);
const deviceScaleFactor = Number.parseFloat(process.env.OPENWYD_UI_HD_DPR || "1");
if (!Number.isFinite(viewportWidth) || !Number.isFinite(viewportHeight) ||
    !Number.isFinite(deviceScaleFactor) || viewportWidth < 800 || viewportHeight < 600 ||
    deviceScaleFactor < 1 || deviceScaleFactor > 3) {
  throw new Error("invalid OPENWYD_UI_HD_WIDTH/HEIGHT/DPR");
}

const result = {
  ok: false,
  baseUrl,
  viewport: { width: viewportWidth, height: viewportHeight, deviceScaleFactor },
  captures: {},
  errors: [],
};
const context = await chromium.launchPersistentContext(profileDir, {
  ...chromiumLaunchOptions({ headless: true }),
  headless: true,
  viewport: { width: viewportWidth, height: viewportHeight },
  deviceScaleFactor,
});
for (const existingPage of context.pages()) await existingPage.close();

async function capture(name, enabled) {
  const page = await context.newPage();
  await page.setViewportSize({ width: viewportWidth, height: viewportHeight });
  page.on("console", message => {
    if (message.type() === "error") result.errors.push(`${name}: ${message.text()}`);
  });
  page.on("pageerror", error => result.errors.push(`${name}: ${error.message}`));

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
    const bootStartedAt = performance.now();
    window.stopAutoTick?.();
    document.body.classList.remove("loading");
    document.querySelector(".display-controls")?.style.setProperty("display", "none", "important");
    Module.print = () => {};
    Module.printErr = () => {};
    Module._wyd_d3d9_set_optimized_ui_hd_enabled(flag);
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
    return {
      bootMilliseconds: performance.now() - bootStartedAt,
      enabled: Module._wyd_d3d9_optimized_ui_hd_enabled(),
      scale: Module._wyd_d3d9_optimized_ui_hd_scale(),
      textures: Module._wyd_d3d9_optimized_ui_hd_textures(),
      sourcePixels: Module._wyd_d3d9_optimized_ui_hd_source_pixels(),
      physicalPixels: Module._wyd_d3d9_optimized_ui_hd_physical_pixels(),
      processingMicroseconds: Module._wyd_d3d9_optimized_ui_hd_microseconds(),
      glErrors: Module._wyd_d3d9_gl_error_total(),
      frameTime: Module._wyd_debug_get_time(),
      camera: [
        Module._wyd_debug_camera_x(),
        Module._wyd_debug_camera_y(),
        Module._wyd_debug_camera_z(),
        Module._wyd_debug_camera_h(),
        Module._wyd_debug_camera_v(),
      ],
      cssCanvas: {
        width: document.querySelector("#canvas")?.getBoundingClientRect().width || 0,
        height: document.querySelector("#canvas")?.getBoundingClientRect().height || 0,
      },
      physicalCanvas: {
        width: document.querySelector("#canvas")?.width || 0,
        height: document.querySelector("#canvas")?.height || 0,
      },
      devicePixelRatio: window.devicePixelRatio,
    };
  }, enabled ? 1 : 0);

  const canvas = page.locator("#canvas");
  const suffix = `${viewportWidth}x${viewportHeight}-dpr${String(deviceScaleFactor).replace(".", "_")}`;
  const fullPath = path.join(outputDir, `${name}-${suffix}.png`);
  await canvas.screenshot({ path: fullPath });
  const box = await canvas.boundingBox();
  const hudPath = path.join(outputDir, `${name}-${suffix}-hud.png`);
  await page.screenshot({
    path: hudPath,
    clip: {
      x: box.x + Math.max(0, (box.width - 720) * 0.5),
      y: box.y + Math.max(0, box.height - 100),
      width: Math.min(720, box.width),
      height: Math.min(100, box.height),
    },
  });
  await page.close();
  return {
    full: path.relative(repoRoot, fullPath).replaceAll("\\", "/"),
    hud: path.relative(repoRoot, hudPath).replaceAll("\\", "/"),
    telemetry,
  };
}

try {
  result.captures.off = await capture("ui-hd-off", false);
  result.captures.on = await capture("ui-hd-on", true);
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
