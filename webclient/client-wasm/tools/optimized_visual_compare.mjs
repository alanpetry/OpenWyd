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
const reportDir = process.env.OPENWYD_VISUAL_REPORT_DIR
  ? path.resolve(process.env.OPENWYD_VISUAL_REPORT_DIR)
  : path.join(
    repoRoot,
    "webclient/client-wasm/build/reports/optimized-visual-compare",
  );
// Exercise every startup state, including the diagnostic and secondary Field
// scenes.  This keeps the evidence broad enough to catch text/layout changes
// outside the first Field screen.
const allStates = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
const requestedStates = (process.env.OPENWYD_VISUAL_STATES || "")
  .split(",")
  .map(value => Number.parseInt(value.trim(), 10))
  .filter(value => allStates.includes(value));
const states = requestedStates.length ? [...new Set(requestedStates)] : allStates;
const ticksPerState = Math.max(
  1,
  Number.parseInt(process.env.OPENWYD_VISUAL_TICKS || "45", 10) || 45,
);
const qualityProfile = ["auto", "performance", "quality", "maximum"].includes(
  process.env.OPENWYD_VISUAL_QUALITY,
) ? process.env.OPENWYD_VISUAL_QUALITY : "quality";
const debugFlags = Number.parseInt(process.env.OPENWYD_VISUAL_DEBUG_FLAGS || "0", 10) || 0;
const rendererBackend = process.env.OPENWYD_VISUAL_RENDERER === "native-webgl2"
  ? "native-webgl2"
  : "bridge";
const selectServerDemoType = [0, 1, 2].includes(
  Number.parseInt(process.env.OPENWYD_VISUAL_SELECT_SERVER_DEMO || "", 10),
) ? Number.parseInt(process.env.OPENWYD_VISUAL_SELECT_SERVER_DEMO, 10) : -1;
const allRuns = [
  { label: "legacy-800x600", mode: "legacy", width: 800, height: 600 },
  { label: "optimized-800x600", mode: "optimized", width: 800, height: 600 },
  { label: "optimized-1920x1080", mode: "optimized", width: 1920, height: 1080 },
  { label: "optimized-3840x2160", mode: "optimized", width: 3840, height: 2160 },
];
const requestedRuns = new Set((process.env.OPENWYD_VISUAL_RUNS || "")
  .split(",")
  .map(value => value.trim())
  .filter(Boolean));
const runs = requestedRuns.size
  ? allRuns.filter(run => requestedRuns.has(run.label))
  : allRuns;

fs.mkdirSync(reportDir, { recursive: true });
const configuredProfilePath = process.env.OPENWYD_VISUAL_PROFILE
  ? path.resolve(process.env.OPENWYD_VISUAL_PROFILE)
  : "";
const profilePath = configuredProfilePath ||
  fs.mkdtempSync(path.join(os.tmpdir(), "openwyd-visual-compare-"));
if (configuredProfilePath) fs.mkdirSync(profilePath, { recursive: true });
const result = {
  ok: false,
  baseUrl,
  states,
  ticksPerState,
  profilePath,
  runs: [],
  consoleErrors: [],
};

function screenshotPath(run, state) {
  return path.join(reportDir, `${run.label}-state-${state}.png`);
}

function relativeToRepo(filePath) {
  return path.relative(repoRoot, filePath).replaceAll("\\", "/");
}

async function snapshot(page) {
  return page.evaluate(() => {
    const M = window.Module || {};
    const call = (name, ...args) => typeof M[name] === "function" ? M[name](...args) : null;
    const readCString = (pointer) => {
      if (!pointer || !M.HEAPU8) return "";
      let end = pointer;
      while (end < M.HEAPU8.length && M.HEAPU8[end] !== 0) end += 1;
      return new TextDecoder("windows-1252").decode(M.HEAPU8.subarray(pointer, end));
    };
    const canvas = document.getElementById("canvas");
    const rect = canvas.getBoundingClientRect();
    const optimizedEnabled = Number(call("_wyd_optimized_view_enabled")) === 1;
    const currentSceneType = Number(call("_wyd_get_scene_type"));
    const textCount = Math.min(2048, Number(call("_wyd_control_visible_text_count")) || 0);
    const visibleText = Array.from({ length: textCount }, (_, index) => ({
      id: call("_wyd_control_visible_text_id", index),
      type: call("_wyd_control_visible_text_type", index),
      align: call("_wyd_control_visible_text_align", index),
      x: call("_wyd_control_visible_text_x", index),
      y: call("_wyd_control_visible_text_y", index),
      width: call("_wyd_control_visible_text_width", index),
      height: call("_wyd_control_visible_text_height", index),
      renderX: call("_wyd_control_visible_text_render_x", index),
      renderY: call("_wyd_control_visible_text_render_y", index),
      extentWidth: call("_wyd_control_visible_text_extent_width", index),
      extentHeight: call("_wyd_control_visible_text_extent_height", index),
      color: call("_wyd_control_visible_text_color", index),
      value: readCString(call("_wyd_control_visible_text_value", index)),
    }));
    const controlCount = Math.min(8192, Number(call("_wyd_control_audit_count")) || 0);
    const controls = Array.from({ length: controlCount }, (_, index) => ({
      id: call("_wyd_control_audit_id", index),
      parentId: call("_wyd_control_audit_parent_id", index),
      type: call("_wyd_control_audit_type", index),
      visible: call("_wyd_control_audit_visible", index),
      localX: call("_wyd_control_audit_local_x", index),
      localY: call("_wyd_control_audit_local_y", index),
      x: call("_wyd_control_audit_abs_x", index),
      y: call("_wyd_control_audit_abs_y", index),
      width: call("_wyd_control_audit_width", index),
      height: call("_wyd_control_audit_height", index),
    }));
    const layoutFindings = [];
    for (const control of controls) {
      if (!control.visible) continue;
	  const isFullBleedShellBackground = optimizedEnabled && control.parentId === 0 &&
		control.type === 1 && (
		  (currentSceneType === 0x7532 && control.id >= 305 && control.id <= 312) ||
		  (currentSceneType === 0x7533 && control.id === 305)
		);
	  if (isFullBleedShellBackground) {
		layoutFindings.push({
		  severity: "INFO",
		  issue: "intentional_full_bleed_background_crop",
		  ...control,
		});
		continue;
	  }
	  // FieldScene2.bin deliberately parks the obsolete PK caption outside
	  // the authored viewport while the real PK button (65786) remains active.
	  // Preserve the official RC data and keep the exception visible in the
	  // report instead of treating it as a responsive-layout regression.
	  if (control.id === 65785 && control.parentId === 65628 && control.localX >= 3000) {
		layoutFindings.push({ severity: "INFO", issue: "authored_offscreen_control", ...control });
		continue;
	  }
      if (control.width < 0 || control.height < 0) {
        layoutFindings.push({ severity: "ERROR", issue: "negative_control_size", ...control });
        continue;
      }
      if (control.width === 0 || control.height === 0) continue;
      if (control.x + control.width <= 0 || control.y + control.height <= 0 ||
          control.x >= rect.width || control.y >= rect.height) {
        layoutFindings.push({ severity: "ERROR", issue: "visible_control_outside_viewport", ...control });
      } else if (control.x < -1 || control.y < -1 ||
                 control.x + control.width > rect.width + 1 ||
                 control.y + control.height > rect.height + 1) {
        layoutFindings.push({ severity: "REVIEW", issue: "visible_control_partly_outside_viewport", ...control });
      }
    }
    for (const item of visibleText) {
      if (!item.value.trim()) continue;
      if (item.extentWidth <= 0 || item.extentHeight <= 0) {
        layoutFindings.push({ severity: "ERROR", issue: "nonempty_text_without_extent", ...item });
        continue;
      }
	  // List-box items remain in the control tree while clipped outside the
	  // list viewport.  Their GeomControl retains an old/default render point,
	  // so it is not evidence that text was actually submitted this frame.
	  const renderMatchesControl =
		Math.abs(item.renderY - (item.y + 2)) <= Math.max(32, item.height + 8) &&
		Math.abs(item.renderX - item.x) <= Math.max(32, item.width + 8);
	  if (!renderMatchesControl) continue;
      if (item.renderX + item.extentWidth <= 0 || item.renderY + item.extentHeight <= 0 ||
          item.renderX >= rect.width || item.renderY >= rect.height) {
        layoutFindings.push({ severity: "ERROR", issue: "rendered_text_outside_viewport", ...item });
      }
      const expectedX = item.align === 0 ? item.x + 8
        : item.align === 1 ? item.x + (item.width - item.extentWidth) / 2
        : item.align === 2 && call("_wyd_optimized_view_enabled") === 1
          ? item.x + item.width - item.extentWidth - 8
          : item.align === 3 ? item.x + 2
          : null;
      if (expectedX !== null && Math.abs(item.renderX - expectedX) > 1.1) {
        layoutFindings.push({
          severity: "ERROR",
          issue: "text_alignment_mismatch",
          expectedX,
          deltaX: item.renderX - expectedX,
          ...item,
        });
      }
      if (item.renderX < item.x - 1 || item.renderX + item.extentWidth > item.x + item.width + 1) {
        layoutFindings.push({ severity: "REVIEW", issue: "text_exceeds_control_width", ...item });
      }
    }
    const matrices = Array.from(
      { length: 48 },
      (_, index) => Number(call("_wyd_compare_3d_state_matrix_value", index)),
    );
    const packageEntry = performance.getEntriesByType("resource")
      .find(entry => /openwyd_assets\..*\.data(?:$|\?)/.test(entry.name));
    const preload = Object.values(M.preloadResults || {})[0] || null;
    const nativeCommandCount = Math.min(
      4096,
      Number(call("_wyd_native_renderer_last_command_count")) || 0,
    );
    const nativeCommandGroups = new Map();
    for (let index = 0; index < nativeCommandCount; index += 1) {
      const values = [
        call("_wyd_native_renderer_command_pass", index),
        call("_wyd_native_renderer_command_fvf", index),
        call("_wyd_native_renderer_command_vs_hash_high", index),
        call("_wyd_native_renderer_command_vs_hash_low", index),
        call("_wyd_native_renderer_command_ps_hash_high", index),
        call("_wyd_native_renderer_command_ps_hash_low", index),
        call("_wyd_native_renderer_command_blend", index),
        call("_wyd_native_renderer_command_depth", index),
        call("_wyd_native_renderer_command_raster", index),
        call("_wyd_native_renderer_command_texture_stages", index),
        call("_wyd_native_renderer_command_stride", index),
      ].map(value => Number(value) >>> 0);
      const key = values.join(":");
      const group = nativeCommandGroups.get(key) || {
        pass: values[0], fvf: values[1],
        vsHash: `${values[2].toString(16).padStart(8, "0")}${values[3].toString(16).padStart(8, "0")}`,
        psHash: `${values[4].toString(16).padStart(8, "0")}${values[5].toString(16).padStart(8, "0")}`,
        blend: values[6], depth: values[7], raster: values[8],
        textureStages: values[9], stride: values[10], draws: 0, indices: 0,
        supported: 0, fallback: 0,
      };
      group.draws += 1;
      group.indices += Number(call("_wyd_native_renderer_command_index_count", index)) || 0;
      if (Number(call("_wyd_native_renderer_command_supported", index)) || 0) {
        group.supported += 1;
      } else {
        group.fallback += 1;
      }
      nativeCommandGroups.set(key, group);
    }
    return {
      state: call("_wyd_get_game_state"),
      sceneType: call("_wyd_get_scene_type"),
      canvas: {
        cssWidth: Math.round(rect.width),
        cssHeight: Math.round(rect.height),
        backingWidth: canvas.width,
        backingHeight: canvas.height,
      },
      optimized: {
        enabled: call("_wyd_optimized_view_enabled"),
        uiScale: call("_wyd_optimized_ui_scale"),
        uiScalePercent: call("_wyd_optimized_ui_scale_percent"),
        worldScale: call("_wyd_optimized_world_scale"),
        samples: call("_wyd_d3d9_optimized_world_samples"),
        webgl2: call("_wyd_d3d9_is_webgl2"),
        worldSharpenStrength: call("_wyd_d3d9_optimized_sharpen_strength"),
        uiSharpenStrength: call("_wyd_d3d9_optimized_ui_sharpen_strength"),
        uiSharpenedTextures: call("_wyd_d3d9_optimized_ui_sharpened_textures"),
        uiSharpenedPixels: call("_wyd_d3d9_optimized_ui_sharpened_pixels"),
      },
      renderer: {
        backend: call("_wyd_renderer_backend"),
        nativeEnabled: call("_wyd_native_renderer_enabled"),
        frameId: call("_wyd_native_renderer_last_frame_id_low"),
        commands: call("_wyd_native_renderer_last_command_count"),
        supportedDraws: call("_wyd_native_renderer_last_supported_draws"),
        fallbackDraws: call("_wyd_native_renderer_last_fallback_draws"),
        streamHash: call("_wyd_native_renderer_last_stream_hash_low"),
        bufferUploads: call("_wyd_native_renderer_buffer_uploads"),
        bufferUploadBytes: call("_wyd_native_renderer_buffer_upload_bytes_low"),
        residentDraws: call("_wyd_native_renderer_resident_draws"),
        actorFailureReason: call("_wyd_native_renderer_actor_failure_reason"),
        actorError: readCString(call("_wyd_native_renderer_actor_error")),
        commandGroups: [...nativeCommandGroups.values()]
          .sort((left, right) => right.draws - left.draws)
          .slice(0, 64),
      },
      camera: {
        valid: call("_wyd_debug_camera_valid"),
        x: call("_wyd_debug_camera_x"),
        y: call("_wyd_debug_camera_y"),
        z: call("_wyd_debug_camera_z"),
        horizon: call("_wyd_debug_camera_h"),
        vertical: call("_wyd_debug_camera_v"),
        sightLength: call("_wyd_debug_camera_sight_length"),
        wantLength: call("_wyd_debug_camera_want_length"),
      },
	  mouseOverHumanId: call("_wyd_field_mouse_over_human_id"),
      compare3d: {
        valid: call("_wyd_compare_3d_state_valid"),
        matrices,
        projection: matrices.slice(32, 48),
      },
      font: {
        renderCalls: call("_wyd_font2_render_calls"),
        nonEmptyCalls: call("_wyd_font2_render_nonempty"),
        lastWidth: call("_wyd_font2_last_nonempty_size0"),
        alphaPixels: call("_wyd_font2_last_nonempty_alpha_pixels"),
        maxWidth: call("_wyd_font2_max_size0"),
      },
      visibleText,
      controls,
      layoutFindings,
      glErrors: call("_wyd_d3d9_gl_error_total"),
	  assetFailures: call("_wyd_d3d9_asset_file_open_fail"),
	  assetFailureSamples: Array.from(
		{ length: Math.min(64, Number(call("_wyd_d3d9_asset_file_open_fail_sample_count")) || 0) },
		(_, index) => readCString(call("_wyd_d3d9_asset_file_open_fail_sample", index)),
	  ),
      cache: {
        fromIndexedDb: Boolean(preload?.fromCache),
        networkTransferBytes: Number(packageEntry?.transferSize) || 0,
        networkDecodedBytes: Number(packageEntry?.decodedBodySize) || 0,
      },
    };
  });
}

async function executeRun(context, run) {
  const page = await context.newPage();
  const cdp = await context.newCDPSession(page);
  await page.setViewportSize({ width: run.width, height: run.height });
  page.on("console", message => {
    if (message.type() === "error") {
      result.consoleErrors.push(`${run.label}: ${message.text()}`);
    }
  });
  page.on("pageerror", error => {
    result.consoleErrors.push(`${run.label}: ${error?.message || String(error)}`);
  });

  const url = new URL(baseUrl);
  url.searchParams.set("mode", "play");
  url.searchParams.set("displayMode", run.mode);
  url.searchParams.set("quality", qualityProfile);
  url.searchParams.set("renderer", rendererBackend);
  url.searchParams.set("uiScale", "100");
  url.searchParams.set("fps", "60");
  url.searchParams.set("state", "0");
  url.searchParams.set("fieldMode", "real");
  url.searchParams.set("tickMs", "16");
  url.searchParams.set("autoboot", "0");
  url.searchParams.set("autostart", "0");
  url.searchParams.set("quiet", "1");
  url.searchParams.set("debugFlags", String(debugFlags >>> 0));

  const output = { ...run, url: url.toString(), states: [], cache: null };
  // Register the run before it starts so an interrupted capture still leaves
  // every completed state in the checkpoint report.
  result.runs.push(output);
  try {
    await page.goto(url.toString(), { waitUntil: "load", timeout: 240000 });
    await page.waitForFunction(() => window.__runtimeReady === true, null, { timeout: 240000 });
    await page.evaluate(({ requestedDebugFlags, requestedSelectServerDemoType }) => {
      window.stopAutoTick?.();
      document.body.classList.remove("loading");
      // The settings button is HTML chrome, not part of the Direct3D/WebGL
      // surface.  Author CSS uses display:flex and can override [hidden], so
      // force it out of deterministic evidence captures.
      document.querySelector(".display-controls")?.style.setProperty(
        "display", "none", "important",
      );
      if (window.Module) {
        Module.print = () => {};
        Module.printErr = () => {};
      }
      Module._wyd_debug_set_fake_time?.(0);
      Module._wyd_compare_random_arm?.(0x4f50454e);
      Module._wyd_d3d9_set_debug_flags?.(requestedDebugFlags >>> 0);
      Module._wyd_selserver_set_demo_type_override?.(requestedSelectServerDemoType);
    }, {
      requestedDebugFlags: debugFlags,
      requestedSelectServerDemoType: selectServerDemoType,
    });
    const boot = await page.evaluate(() => Module._wyd_boot_client(0));
    if (!boot) throw new Error("boot failed");

    for (const state of states) {
      const setResult = await page.evaluate(value => Module._wyd_set_game_state(value), state);
      if (!setResult) throw new Error(`set state ${state} failed`);
	  // Keep hover-dependent materials identical at every viewport size.  The
	  // legacy client initializes its logical cursor near the 800x600 center,
	  // which highlights the test human only in the narrow capture.
	  await page.evaluate(() => Module._wyd_mouse_event?.(0x0200, 0, 4, 4, 0));
      const tickResult = await page.evaluate(count => {
        let rc = 1;
        for (let index = 0; index < count; index += 1) {
          Module._wyd_debug_advance_fake_time?.(16);
          rc = Module._wyd_tick_client();
          if (rc < 0) break;
        }
        return rc;
      }, ticksPerState);
      if (tickResult < 0) throw new Error(`state ${state} tick failed: ${tickResult}`);
      const stateSnapshot = await snapshot(page);
      const shot = screenshotPath(run, state);
      const canvasBox = await page.locator("#canvas").boundingBox();
      if (!canvasBox) throw new Error(`state ${state} canvas has no bounds`);
      // Playwright's element/page screenshots wait for visual stability and
      // can time out while the WebGL canvas is continuously presented.  CDP
      // reads the browser surface immediately without that stability wait.
      const captured = await cdp.send("Page.captureScreenshot", {
        format: "png",
        fromSurface: true,
        captureBeyondViewport: false,
        clip: {
          x: canvasBox.x,
          y: canvasBox.y,
          width: canvasBox.width,
          height: canvasBox.height,
          scale: 1,
        },
      });
      fs.writeFileSync(shot, Buffer.from(captured.data, "base64"));
      output.states.push({
        requestedState: state,
        tickResult,
        screenshot: relativeToRepo(shot),
        snapshot: stateSnapshot,
      });
      fs.writeFileSync(
        path.join(reportDir, "raw-report.checkpoint.json"),
        `${JSON.stringify(result, null, 2)}\n`,
      );
      process.stdout.write(`${run.label} state=${state} captured\n`);
    }
    output.cache = output.states[0]?.snapshot?.cache || null;
    await page.evaluate(() => Module._wyd_shutdown_client?.());
  } finally {
    await page.close();
  }
  return output;
}

let context;
try {
  context = await chromium.launchPersistentContext(profilePath, {
    ...chromiumLaunchOptions({ headless: true }),
    headless: true,
    viewport: { width: 800, height: 600 },
    deviceScaleFactor: 1,
  });
  // Revalidate only stable bootstraps, matching the public nginx policy. The
  // large content-addressed package is still restored from IndexedDB.
  await context.setExtraHTTPHeaders({
    "Cache-Control": "no-cache",
    Pragma: "no-cache",
  });
  for (const run of runs) await executeRun(context, run);
  result.ok = result.consoleErrors.length === 0 && result.runs.every(run => (
    run.states.length === states.length && run.states.every(entry => (
      entry.snapshot.state === entry.requestedState &&
      entry.snapshot.glErrors === 0 &&
      entry.snapshot.layoutFindings.every(finding => finding.severity !== "ERROR") &&
      entry.snapshot.canvas.cssWidth === run.width &&
      entry.snapshot.canvas.cssHeight === run.height &&
      (run.mode === "legacy" || (
        entry.snapshot.optimized.enabled === 1 &&
        Math.abs(entry.snapshot.optimized.uiScale - 1) < 0.0001
      ))
    ))
  ));
} catch (error) {
  result.error = error?.stack || error?.message || String(error);
} finally {
  if (context) await context.close();
  if (!configuredProfilePath) {
    try {
      fs.rmSync(profilePath, { recursive: true, force: true });
    } catch {}
  }
  const reportPath = path.join(reportDir, "raw-report.json");
  fs.writeFileSync(reportPath, `${JSON.stringify(result, null, 2)}\n`);
}

console.log(JSON.stringify({
  ok: result.ok,
  report: relativeToRepo(path.join(reportDir, "raw-report.json")),
  runs: result.runs.map(run => ({
    label: run.label,
    cache: run.cache,
    stateCount: run.states.length,
  })),
  consoleErrors: result.consoleErrors,
  error: result.error || null,
}, null, 2));
process.exit(result.ok ? 0 : 1);
