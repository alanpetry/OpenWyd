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
const outputDir = process.env.OPENWYD_UI_AUDIT_DIR
  ? path.resolve(process.env.OPENWYD_UI_AUDIT_DIR)
  : path.join(repoRoot, "webclient/client-wasm/build/reports/ui-dialog-visual-audit");
const profileDir = path.join(
  repoRoot,
  "webclient/client-wasm/build/cache/optimized-sharpen-compare-profile",
);
const requestedStates = (process.env.OPENWYD_UI_AUDIT_STATES || "0,1,2,3,4,5,6,7,8,9")
  .split(",")
  .map(value => Number.parseInt(value.trim(), 10))
  .filter(value => Number.isInteger(value) && value >= 0 && value <= 9);
const states = [...new Set(requestedStates)];
const maxCapturesPerState = Math.max(
  1,
  Number.parseInt(process.env.OPENWYD_UI_AUDIT_MAX_CAPTURES || "80", 10) || 80,
);
fs.mkdirSync(outputDir, { recursive: true });
fs.mkdirSync(profileDir, { recursive: true });

const result = {
  ok: false,
  baseUrl,
  states,
  maxCapturesPerState,
  scenes: [],
  errors: [],
};

const readString = (M, pointer) => {
  if (!pointer || !M.HEAPU8) return "";
  let end = pointer;
  while (end < M.HEAPU8.length && M.HEAPU8[end] !== 0) end += 1;
  return new TextDecoder("windows-1252").decode(M.HEAPU8.subarray(pointer, end));
};

async function bootHarness(page) {
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
  return page.evaluate(() => {
    window.stopAutoTick?.();
    document.body.classList.remove("loading");
    document.querySelector(".display-controls")?.style.setProperty("display", "none", "important");
    Module.print = () => {};
    Module.printErr = () => {};
    Module._wyd_debug_set_fake_time?.(0);
    Module._wyd_compare_random_arm?.(0x4f50454e);
    if (Module._wyd_boot_client(0) !== 1) throw new Error("boot failed");
    return true;
  });
}

async function enterState(page, state) {
  return page.evaluate(requestedState => {
    Module._wyd_debug_set_fake_time?.(0);
    if (requestedState === 0) Module._wyd_set_field_mode(1);
    if (Module._wyd_set_game_state(requestedState) !== 1) {
      throw new Error(`set state ${requestedState} failed`);
    }
    for (let index = 0; index < 45; index += 1) {
      Module._wyd_debug_advance_fake_time?.(16);
      Module._wyd_tick_client();
    }
    return {
      actualState: Module._wyd_get_game_state(),
      sceneType: Module._wyd_get_scene_type(),
      labelPointer: Module._wyd_get_state_debug_label(),
      glErrors: Module._wyd_d3d9_gl_error_total(),
    };
  }, state);
}

async function auditState(page, cdp, state) {
  const preparedRuntimePanels = await page.evaluate(() => {
    const prepared = [];
    for (const id of [289]) {
      if (Module._wyd_control_audit_prepare_runtime_panel?.(id) === 1) prepared.push(id);
    }
    if (prepared.length) Module._wyd_tick_client();
    return prepared;
  });
  const setup = await page.evaluate(() => {
    const count = Math.min(8192, Module._wyd_control_audit_count());
    const controls = Array.from({ length: count }, (_, index) => ({
      index,
      id: Module._wyd_control_audit_id(index),
      parentId: Module._wyd_control_audit_parent_id(index),
      type: Module._wyd_control_audit_type(index),
      visible: Module._wyd_control_audit_visible(index),
      rawVisible: Module._wyd_control_audit_raw_visible(index),
      depth: Module._wyd_control_audit_depth(index),
      x: Module._wyd_control_audit_abs_x(index),
      y: Module._wyd_control_audit_abs_y(index),
      width: Module._wyd_control_audit_width(index),
      height: Module._wyd_control_audit_height(index),
    }));
    return {
      controls,
      canvas: (() => {
        const rect = document.getElementById("canvas").getBoundingClientRect();
        return { x: rect.x, y: rect.y, width: rect.width, height: rect.height };
      })(),
    };
  });

  for (let index = 0; index < setup.controls.length; index += 1) {
    const current = setup.controls[index];
    let descendants = 0;
    for (let child = index + 1; child < setup.controls.length; child += 1) {
      if (setup.controls[child].depth <= current.depth) break;
      descendants += 1;
    }
    current.descendants = descendants;
  }
  const candidateTypes = new Set([1, 8, 9, 14]);
  const candidates = setup.controls
    .filter(control => candidateTypes.has(control.type))
    .filter(control => control.width >= 96 && control.height >= 48)
    .filter(control => control.depth <= 5)
    .filter(control => control.descendants >= 2 || control.width * control.height >= 30000)
    .sort((left, right) => (
      left.rawVisible - right.rawVisible ||
      left.depth - right.depth ||
      right.width * right.height - left.width * left.height
    ));
  const originalRawVisibility = setup.controls.map(control => control.rawVisible);
  const scene = {
    state,
    preparedRuntimePanels,
    controlCount: setup.controls.length,
    candidateCount: candidates.length,
    capturedCount: 0,
    candidates: [],
  };

  for (const [candidateNumber, candidate] of candidates.entries()) {
    const entry = { ...candidate, findings: [], screenshot: null, status: "pending" };
    if (candidateNumber >= maxCapturesPerState) {
      entry.status = "not_captured_limit";
      scene.candidates.push(entry);
      continue;
    }

    const snapshot = await page.evaluate(({ target, original }) => {
      for (let index = 0; index < original.length; index += 1) {
        Module._wyd_control_audit_set_raw_visible(index, original[index]);
      }
      Module._wyd_control_audit_reveal_with_ancestors(target);
      // Control geometry is queued during the scene tick, not RenderScene.
      // Keep the fake clock fixed so this rebuilds the official draw list
      // without advancing animation or gameplay state.
      Module._wyd_tick_client();
      const textCount = Math.min(512, Module._wyd_control_visible_text_count());
      const texts = Array.from({ length: textCount }, (_, index) => ({
        id: Module._wyd_control_visible_text_id(index),
        type: Module._wyd_control_visible_text_type(index),
        align: Module._wyd_control_visible_text_align(index),
        comma: Module._wyd_control_visible_text_comma(index),
        x: Module._wyd_control_visible_text_x(index),
        y: Module._wyd_control_visible_text_y(index),
        width: Module._wyd_control_visible_text_width(index),
        height: Module._wyd_control_visible_text_height(index),
        renderX: Module._wyd_control_visible_text_render_x(index),
        renderY: Module._wyd_control_visible_text_render_y(index),
        extentWidth: Module._wyd_control_visible_text_extent_width(index),
        extentHeight: Module._wyd_control_visible_text_extent_height(index),
        valuePointer: Module._wyd_control_visible_text_value(index),
      }));
      return {
        texts,
        glErrors: Module._wyd_d3d9_gl_error_total(),
      };
    }, { target: candidate.index, original: originalRawVisibility });
    entry.textCount = snapshot.texts.length;
    entry.glErrors = snapshot.glErrors;
    const targetTextIds = new Set(
      setup.controls
        .slice(candidate.index + 1, candidate.index + candidate.descendants + 1)
        .filter(control => control.type === 12 || control.type === 13)
        .map(control => control.id),
    );
    const canvasBounds = {
      left: 0,
      top: 0,
      right: setup.canvas.width,
      bottom: setup.canvas.height,
    };
    for (const text of snapshot.texts) {
      text.value = await page.evaluate(pointer => {
        if (!pointer) return "";
        let end = pointer;
        while (end < Module.HEAPU8.length && Module.HEAPU8[end] !== 0) end += 1;
        return new TextDecoder("windows-1252").decode(Module.HEAPU8.subarray(pointer, end));
      }, text.valuePointer);
      delete text.valuePointer;
      if (!text.value.trim() || !targetTextIds.has(text.id)) continue;
      const rendered = {
        left: text.renderX,
        top: text.renderY,
        right: text.renderX + text.extentWidth,
        bottom: text.renderY + text.extentHeight,
      };
      const declared = {
        left: text.x,
        top: text.y,
        right: text.x + text.width,
        bottom: text.y + text.height,
      };
      const expectedX = text.align === 0 ? text.x + 8
        : text.align === 1 ? text.x + (text.width - text.extentWidth) / 2
        : text.align === 2 ? text.x + text.width - text.extentWidth - 8
        : text.align === 3 ? text.x + 2
        : text.align === 4 ? text.x + 112
        : null;
      const expectedY = text.y + (text.height - 16) / 2 + 2;
      const outsideViewport = rendered.right <= canvasBounds.left ||
        rendered.bottom <= canvasBounds.top || rendered.left >= canvasBounds.right ||
        rendered.top >= canvasBounds.bottom;
      if (outsideViewport) {
        entry.findings.push({
          ...text,
          severity: "ERROR",
          type: "rendered_text_outside_viewport",
          rendered,
        });
      }
      const deltaX = expectedX === null ? 0 : text.renderX - expectedX;
      const deltaY = text.renderY - expectedY;
      if ((expectedX !== null && Math.abs(deltaX) > 1.1) || Math.abs(deltaY) > 1.1) {
        entry.findings.push({
          ...text,
          severity: text.comma === 2 ? "REVIEW" : "ERROR",
          type: "text_alignment_formula_mismatch",
          expectedX,
          expectedY,
          deltaX,
          deltaY,
        });
      }
      const exceedsDeclared = rendered.left < declared.left - 1 ||
        rendered.top < declared.top - 1 || rendered.right > declared.right + 1 ||
        rendered.bottom > declared.bottom + 1;
      if (exceedsDeclared) {
        entry.findings.push({
          ...text,
          severity: text.type === 13 ? "ERROR" : "REVIEW",
          type: text.type === 13
            ? "editable_text_exceeds_input"
            : "text_exceeds_authored_declaration",
          rendered,
          declared,
        });
      }
      const panel = {
        left: candidate.x,
        top: candidate.y,
        right: candidate.x + candidate.width,
        bottom: candidate.y + candidate.height,
      };
      if (rendered.left < panel.left - 8 || rendered.top < panel.top - 8 ||
          rendered.right > panel.right + 8 || rendered.bottom > panel.bottom + 8) {
        entry.findings.push({
          ...text,
          severity: "REVIEW",
          type: "text_exceeds_captured_panel",
          rendered,
          panel,
        });
      }
    }

    const left = Math.max(0, Math.floor(candidate.x - 8));
    const top = Math.max(0, Math.floor(candidate.y - 8));
    const right = Math.min(setup.canvas.width, Math.ceil(candidate.x + candidate.width + 8));
    const bottom = Math.min(setup.canvas.height, Math.ceil(candidate.y + candidate.height + 8));
    if (right <= left || bottom <= top) {
      entry.status = "offscreen";
    } else {
      const image = await cdp.send("Page.captureScreenshot", {
        format: "png",
        fromSurface: true,
        captureBeyondViewport: false,
        clip: {
          x: setup.canvas.x + left,
          y: setup.canvas.y + top,
          width: right - left,
          height: bottom - top,
          scale: 1,
        },
      });
      const fileName = `state-${state}-panel-${candidateNumber}-id-${candidate.id}.png`;
      fs.writeFileSync(path.join(outputDir, fileName), Buffer.from(image.data, "base64"));
      entry.screenshot = fileName;
      entry.status = "captured";
      scene.capturedCount += 1;
    }
    scene.candidates.push(entry);
  }

  await page.evaluate(original => {
    for (let index = 0; index < original.length; index += 1) {
      Module._wyd_control_audit_set_raw_visible(index, original[index]);
    }
    Module._wyd_tick_client();
  }, originalRawVisibility);
  return scene;
}

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
  await bootHarness(page);
  const cdp = await context.newCDPSession(page);
  for (const state of states) {
    const boot = await enterState(page, state);
    const scene = await auditState(page, cdp, state);
    scene.boot = boot;
    scene.stateLabel = await page.evaluate(pointer => {
      if (!pointer) return "";
      let end = pointer;
      while (end < Module.HEAPU8.length && Module.HEAPU8[end] !== 0) end += 1;
      return new TextDecoder("windows-1252").decode(Module.HEAPU8.subarray(pointer, end));
    }, boot.labelPointer);
    delete scene.boot.labelPointer;
    result.scenes.push(scene);
  }
  await page.close();
  const errorFindings = result.scenes.flatMap(scene => scene.candidates)
    .flatMap(candidate => candidate.findings)
    .filter(finding => finding.severity === "ERROR");
  result.errorFindingCount = errorFindings.length;
  result.ok = result.errors.length === 0 && errorFindings.length === 0 &&
    result.scenes.length === states.length &&
    result.scenes.every(scene => scene.boot.glErrors === 0 &&
      scene.candidates.every(candidate => (candidate.glErrors ?? 0) === 0));
} catch (error) {
  result.errors.push(error?.stack || error?.message || String(error));
} finally {
  await context?.close();
  fs.writeFileSync(path.join(outputDir, "report.json"), `${JSON.stringify(result, null, 2)}\n`);
}

console.log(JSON.stringify({
  ok: result.ok,
  scenes: result.scenes.map(scene => ({
    state: scene.state,
    label: scene.stateLabel,
    controls: scene.controlCount,
    candidates: scene.candidateCount,
    captured: scene.capturedCount,
    findings: scene.candidates.reduce((sum, candidate) => sum + candidate.findings.length, 0),
    errors: scene.candidates.reduce((sum, candidate) => (
      sum + candidate.findings.filter(finding => finding.severity === "ERROR").length
    ), 0),
    reviews: scene.candidates.reduce((sum, candidate) => (
      sum + candidate.findings.filter(finding => finding.severity === "REVIEW").length
    ), 0),
  })),
  errors: result.errors,
  output: path.relative(repoRoot, outputDir).replaceAll("\\", "/"),
}, null, 2));
process.exit(result.ok ? 0 : 1);
