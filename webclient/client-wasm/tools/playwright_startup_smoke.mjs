#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import playwrightPkg from "../../node_modules/playwright/index.js";
import { chromiumLaunchOptions } from "../../tools/playwright_portable_browser.mjs";

const { chromium } = playwrightPkg;
const GAME_STATE_FIELD = 0;
const ALL_GAME_STATES = [-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
const SCREEN_STATES = {
  none: -1,
  field: 0,
  test2: 1,
  sea: 2,
  login: 3,
  createid: 4,
  createaccount: 4,
  selectchar: 5,
  char: 5,
  createchar: 6,
  selectserver: 7,
  server: 7,
  demo: 8,
  field2: 9,
};

function parseArgs(argv) {
  const opts = {
    url: "http://127.0.0.1:8877/webclient/client-wasm/build/link/startup_harness.html",
    state: GAME_STATE_FIELD,
    screen: null,
    debugFlags: 0,
    debugSkipFvf: 0,
    ticks: 30,
    tickMs: 100,
    timeoutMs: 30000,
    screenshot: "webclient/client-wasm/build/reports/startup-canvas.png",
    trace: false,
    traceDefaultProbes: false,
    traceProbes: [],
    traceTop: 24,
    preBootDebugFlags: null,
    cameraOffset: null,
    viewport: { width: 1220, height: 720 },
    canvas: null,
    logical: null,
    layout: null,
    fit: null,
    fieldMode: "real",
    weatherMode: 0,
    fieldCaptureTicks: null,
    fieldCaptureDir: "webclient/client-wasm/build/reports/field-captures",
    socketProxy: null,
    host: null,
    account: "OPENWYD01",
    password: "TEST1234",
    dummyLogin: false,
    loginTicks: 120,
    allStates: false,
    summaryOnly: false,
    mouseTests: [],
  };

  for (let i = 2; i < argv.length; i += 1) {
    const a = argv[i];
    if (a === "--url" && argv[i + 1]) {
      opts.url = argv[i + 1];
      i += 1;
      continue;
    }
    if (a === "--state" && argv[i + 1]) {
      const parsed = Number.parseInt(argv[i + 1], 10);
      if (Number.isFinite(parsed)) opts.state = parsed;
      i += 1;
      continue;
    }
    if (a === "--screen" && argv[i + 1]) {
      const screen = String(argv[i + 1] || "").toLowerCase();
      opts.screen = screen;
      if (Object.prototype.hasOwnProperty.call(SCREEN_STATES, screen)) {
        opts.state = SCREEN_STATES[screen];
      }
      i += 1;
      continue;
    }
    if (a === "--debug-flags" && argv[i + 1]) {
      opts.debugFlags = Number.parseInt(argv[i + 1], 10) || 0;
      i += 1;
      continue;
    }
    if (a === "--preboot-debug-flags" && argv[i + 1]) {
      opts.preBootDebugFlags = Number.parseInt(argv[i + 1], 10) || 0;
      i += 1;
      continue;
    }
    if (a === "--debug-skip-fvf" && argv[i + 1]) {
      opts.debugSkipFvf = Number.parseInt(argv[i + 1], 10) || 0;
      i += 1;
      continue;
    }
    if (a === "--ticks" && argv[i + 1]) {
      opts.ticks = Number.parseInt(argv[i + 1], 10) || opts.ticks;
      i += 1;
      continue;
    }
    if (a === "--tick-ms" && argv[i + 1]) {
      opts.tickMs = Number.parseInt(argv[i + 1], 10) || 0;
      i += 1;
      continue;
    }
    if (a === "--timeout-ms" && argv[i + 1]) {
      opts.timeoutMs = Number.parseInt(argv[i + 1], 10) || opts.timeoutMs;
      i += 1;
      continue;
    }
    if (a === "--screenshot" && argv[i + 1]) {
      opts.screenshot = argv[i + 1];
      i += 1;
      continue;
    }
    if (a === "--viewport" && argv[i + 1]) {
      const size = parseSize(argv[i + 1]);
      if (size) opts.viewport = size;
      i += 1;
      continue;
    }
    if (a === "--canvas" && argv[i + 1]) {
      opts.canvas = parseSize(argv[i + 1]);
      i += 1;
      continue;
    }
    if (a === "--logical" && argv[i + 1]) {
      opts.logical = parseSize(argv[i + 1]);
      i += 1;
      continue;
    }
    if (a === "--layout" && argv[i + 1]) {
      opts.layout = argv[i + 1];
      i += 1;
      continue;
    }
    if (a === "--fit" && argv[i + 1]) {
      opts.fit = argv[i + 1];
      i += 1;
      continue;
    }
    if (a === "--field-capture-ticks" && argv[i + 1]) {
      opts.fieldCaptureTicks = parseTickList(argv[i + 1]);
      i += 1;
      continue;
    }
    if (a === "--field-capture-dir" && argv[i + 1]) {
      opts.fieldCaptureDir = String(argv[i + 1] || "webclient/client-wasm/build/reports/field-captures");
      i += 1;
      continue;
    }
    if (a === "--field-mode" && argv[i + 1]) {
      opts.fieldMode = String(argv[i + 1] || "real").toLowerCase();
      i += 1;
      continue;
    }
    if (a === "--weather-mode" && argv[i + 1]) {
      opts.weatherMode = Number.parseInt(argv[i + 1], 10) || 0;
      i += 1;
      continue;
    }
    if (a === "--socket-proxy" && argv[i + 1]) {
      opts.socketProxy = argv[i + 1];
      i += 1;
      continue;
    }
    if (a === "--host" && argv[i + 1]) {
      opts.host = argv[i + 1];
      i += 1;
      continue;
    }
    if (a === "--account" && argv[i + 1]) {
      opts.account = argv[i + 1];
      i += 1;
      continue;
    }
    if (a === "--password" && argv[i + 1]) {
      opts.password = argv[i + 1];
      i += 1;
      continue;
    }
    if (a === "--dummy-login") {
      opts.dummyLogin = true;
      continue;
    }
    if (a === "--mouse-test" && argv[i + 1]) {
      opts.mouseTests.push(String(argv[i + 1] || "").toLowerCase());
      i += 1;
      continue;
    }
    if (a === "--login-ticks" && argv[i + 1]) {
      opts.loginTicks = Number.parseInt(argv[i + 1], 10) || opts.loginTicks;
      i += 1;
      continue;
    }
    if (a === "--all-states") {
      opts.allStates = true;
      continue;
    }
    if (a === "--summary-only") {
      opts.summaryOnly = true;
      continue;
    }
    if (a === "--trace") {
      opts.trace = true;
      continue;
    }
    if (a === "--trace-default-probes") {
      opts.trace = true;
      opts.traceDefaultProbes = true;
      continue;
    }
    if (a === "--trace-top" && argv[i + 1]) {
      opts.traceTop = Number.parseInt(argv[i + 1], 10) || opts.traceTop;
      i += 1;
      continue;
    }
    if (a === "--trace-probe" && argv[i + 1]) {
      const parsed = parseTraceProbe(argv[i + 1]);
      if (parsed) {
        opts.trace = true;
        opts.traceProbes.push(parsed);
      }
      i += 1;
      continue;
    }
    if (a === "--camera-offset" && argv[i + 1]) {
      opts.cameraOffset = parseCameraOffset(argv[i + 1]);
      i += 1;
      continue;
    }
  }

  return opts;
}

function parseSize(value) {
  const match = String(value || "").match(/^(\d+)x(\d+)$/i);
  if (!match) return null;
  const width = Number.parseInt(match[1], 10);
  const height = Number.parseInt(match[2], 10);
  if (!Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0) return null;
  return { width, height };
}

function harnessUrl(opts) {
  const url = new URL(opts.url, "http://127.0.0.1");
  url.searchParams.set("state", `${opts.state}`);
  if (opts.screen) url.searchParams.set("screen", opts.screen);
  if (opts.layout) url.searchParams.set("layout", opts.layout);
  const logical = opts.logical || opts.canvas;
  if (logical) url.searchParams.set("logical", `${logical.width}x${logical.height}`);
  if (opts.fit) url.searchParams.set("fit", opts.fit);
  if (opts.fieldMode) url.searchParams.set("fieldMode", opts.fieldMode);
  if (opts.socketProxy) url.searchParams.set("socketProxy", opts.socketProxy);
  if (opts.host) url.searchParams.set("host", opts.host);
  if (opts.account) url.searchParams.set("account", opts.account);
  if (opts.password) url.searchParams.set("password", opts.password);
  if (Number.isFinite(opts.tickMs)) url.searchParams.set("tickMs", `${opts.tickMs}`);
  return url.toString();
}

function parseTickList(value) {
  return String(value || "")
    .split(",")
    .map((s) => Number.parseInt(s.trim(), 10))
    .filter((n) => Number.isFinite(n) && n > 0)
    .sort((a, b) => a - b);
}

function parseCameraOffset(value) {
  const parts = String(value || "")
    .split(",")
    .map((part) => Number.parseFloat(part.trim()));
  if (parts.length !== 5 || parts.some((part) => !Number.isFinite(part))) return null;
  const [dx, dy, dz, dh, dv] = parts;
  return { dx, dy, dz, dh, dv };
}

function parseTraceProbe(spec) {
  const parts = String(spec || "").split(":");
  if (parts.length !== 3) return null;
  const x = Number.parseFloat(parts[1]);
  const y = Number.parseFloat(parts[2]);
  if (!Number.isFinite(x) || !Number.isFinite(y)) return null;
  return { label: parts[0] || `probe${Date.now()}`, x, y };
}

function defaultTraceProbes() {
  return [
    { label: "left_dark", x: 0.16, y: 0.50 },
    { label: "center", x: 0.50, y: 0.50 },
    { label: "right_dark", x: 0.82, y: 0.52 },
    { label: "top_sky", x: 0.50, y: 0.13 },
    { label: "lower_water", x: 0.50, y: 0.78 },
    { label: "right_characters", x: 0.70, y: 0.48 },
  ];
}

function materializeTraceProbes(opts, width, height) {
  if (!opts.trace) return [];
  const probes = [];
  if (opts.traceDefaultProbes || opts.traceProbes.length === 0) probes.push(...defaultTraceProbes());
  probes.push(...opts.traceProbes);
  return probes.slice(0, 16).map((probe, index) => {
    const normalized = Math.abs(probe.x) <= 1 && Math.abs(probe.y) <= 1;
    return {
      index,
      label: probe.label || `probe${index}`,
      x: normalized ? Math.round(probe.x * width) : Math.round(probe.x),
      y: normalized ? Math.round(probe.y * height) : Math.round(probe.y),
    };
  });
}

async function tickWasm(page, count, tickMs) {
  return page.evaluate(({ count: tickCount, tickMs: tickDelta }) => {
    const out = [];
    for (let i = 0; i < tickCount; i += 1) {
      if (tickDelta > 0 && typeof Module._wyd_debug_advance_fake_time === "function") {
        Module._wyd_debug_advance_fake_time(tickDelta >>> 0);
      }
      out.push(Module._wyd_tick_client());
    }
    return out;
  }, { count, tickMs });
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

async function readFieldMouseState(page) {
  return page.evaluate(() => {
    const M = window.Module || {};
    const call = (name, ...args) => (typeof M[name] === "function" ? M[name](...args) : null);
    return {
      humanX: call("_wyd_field_myhuman_x"),
      humanY: call("_wyd_field_myhuman_y"),
      motion: call("_wyd_field_myhuman_motion"),
      sentMotion: call("_wyd_field_myhuman_sent_motion"),
      moving: call("_wyd_field_myhuman_moving"),
      progress: call("_wyd_field_myhuman_progress_rate"),
      lastRoute: call("_wyd_field_myhuman_last_route_index"),
      maxRoute: call("_wyd_field_myhuman_max_route_index"),
      targetX: call("_wyd_field_myhuman_target_x"),
      targetY: call("_wyd_field_myhuman_target_y"),
      moveToX: call("_wyd_field_myhuman_move_to_x"),
      moveToY: call("_wyd_field_myhuman_move_to_y"),
      mouseX: call("_wyd_input_mouse_x"),
      mouseY: call("_wyd_input_mouse_y"),
      mouseEvents: call("_wyd_input_mouse_event_count"),
      lastMouseMsg: call("_wyd_input_mouse_last_msg"),
      controlEventCount: call("_wyd_control_event_count"),
      lastControlId: call("_wyd_control_last_event_id"),
      lastControlType: call("_wyd_control_last_event_type"),
      cameraH: call("_wyd_debug_camera_h"),
      cameraV: call("_wyd_debug_camera_v"),
      cameraSightLength: call("_wyd_debug_camera_sight_length"),
      cameraWantLength: call("_wyd_debug_camera_want_length"),
    };
  });
}

async function runFieldUiMouseTest(page, opts) {
  const candidates = [
    { id: 65790, label: "B_CHAR" },
    { id: 65791, label: "B_EQUIP" },
    { id: 65793, label: "B_QUESTLOG" },
    { id: 65787, label: "P_MINIBTNPANEL_BTN" },
    { id: 65795, label: "B_HELP" },
    { id: 65796, label: "B_SYSTEM" },
  ];
  const attempts = [];

  for (const candidate of candidates) {
    const before = await page.evaluate((controlId) => {
    const M = window.Module || {};
    const call = (name, ...args) => (typeof M[name] === "function" ? M[name](...args) : null);
    return {
      exists: call("_wyd_control_exists", controlId),
      visible: call("_wyd_control_visible", controlId),
      enabled: call("_wyd_control_enabled", controlId),
      selectEnabled: call("_wyd_control_select_enabled", controlId),
      selected: call("_wyd_control_selected", controlId),
      x: call("_wyd_control_abs_x", controlId),
      y: call("_wyd_control_abs_y", controlId),
      w: call("_wyd_control_width", controlId),
      h: call("_wyd_control_height", controlId),
      eventCount: call("_wyd_control_event_count"),
      humanX: call("_wyd_field_myhuman_x"),
      humanY: call("_wyd_field_myhuman_y"),
    };
  }, candidate.id);

    if (before.exists !== 1 || before.visible !== 1 || before.enabled !== 1 ||
        before.selectEnabled !== 1 || before.w <= 0 || before.h <= 0) {
      attempts.push({ ...candidate, ok: false, before, error: "control is not clickable" });
      continue;
    }

    const point = await logicalToPagePoint(page, before.x + before.w * 0.5, before.y + before.h * 0.5);
    await page.mouse.move(point.x, point.y);
    await page.mouse.down({ button: "left" });
    await tickWasm(page, 3, opts.tickMs);
    await page.mouse.up({ button: "left" });
    await tickWasm(page, 8, opts.tickMs);

    const after = await page.evaluate((controlId) => {
    const M = window.Module || {};
    const call = (name, ...args) => (typeof M[name] === "function" ? M[name](...args) : null);
    return {
      selected: call("_wyd_control_selected", controlId),
      pressed: call("_wyd_control_pressed", controlId),
      eventCount: call("_wyd_control_event_count"),
      lastControlId: call("_wyd_control_last_event_id"),
      lastControlType: call("_wyd_control_last_event_type"),
      lastMouseProcessedId: call("_wyd_control_last_mouse_processed_id"),
      lastMouseProcessedFlags: call("_wyd_control_last_mouse_processed_flags"),
      lastMouseProcessedType: call("_wyd_control_last_mouse_processed_type"),
      lastMouseProcessedX: call("_wyd_control_last_mouse_processed_x"),
      lastMouseProcessedY: call("_wyd_control_last_mouse_processed_y"),
      humanX: call("_wyd_field_myhuman_x"),
      humanY: call("_wyd_field_myhuman_y"),
      mouseEvents: call("_wyd_input_mouse_event_count"),
      lastMouseMsg: call("_wyd_input_mouse_last_msg"),
    };
  }, candidate.id);

    const humanDelta = Math.hypot((after.humanX ?? 0) - (before.humanX ?? 0), (after.humanY ?? 0) - (before.humanY ?? 0));
    const ok = after.lastControlId === candidate.id && after.eventCount > before.eventCount && humanDelta < 0.25;
    const attempt = { name: "field-ui", ok, controlId: candidate.id, label: candidate.label, point, before, after, humanDelta };
    attempts.push(attempt);
    if (ok) return { ...attempt, attempts };
  }

  return { name: "field-ui", ok: false, attempts, error: "no official field UI button produced OnControlEvent" };
}

async function chooseFieldMoveTarget(page) {
  return page.evaluate(() => {
    const M = window.Module || {};
    const call = (name, ...args) => (typeof M[name] === "function" ? M[name](...args) : null);
    const humanX = call("_wyd_field_myhuman_x");
    const humanY = call("_wyd_field_myhuman_y");
    const candidates = [
      [400, 260],
      [470, 300],
      [330, 300],
      [520, 240],
      [280, 250],
      [600, 300],
      [210, 280],
    ];
    for (const [x, y] of candidates) {
      const valid = call("_wyd_field_pick_at", x, y);
      const pickX = call("_wyd_field_last_pick_x");
      const pickZ = call("_wyd_field_last_pick_z");
      const distance = Math.hypot((pickX ?? 0) - (humanX ?? 0), (pickZ ?? 0) - (humanY ?? 0));
      if (valid === 1 && distance > 1.5) {
        return { logicalX: x, logicalY: y, pickX, pickZ, distance, humanX, humanY };
      }
    }
    return null;
  });
}

async function runFieldMoveMouseTest(page, opts) {
  const target = await chooseFieldMoveTarget(page);
  const before = await readFieldMouseState(page);
  if (!target) return { name: "field-move", ok: false, before, error: "no valid ground pick target" };

  const point = await logicalToPagePoint(page, target.logicalX, target.logicalY);
  await page.mouse.move(point.x, point.y);
  await page.mouse.down({ button: "left" });
  await tickWasm(page, 4, opts.tickMs);
  await page.mouse.up({ button: "left" });
  await tickWasm(page, 90, opts.tickMs);

  const after = await readFieldMouseState(page);
  const humanDelta = Math.hypot((after.humanX ?? 0) - (before.humanX ?? 0), (after.humanY ?? 0) - (before.humanY ?? 0));
  const routeChanged = after.targetX !== before.targetX || after.targetY !== before.targetY ||
    after.moveToX !== before.moveToX || after.moveToY !== before.moveToY ||
    after.maxRoute !== before.maxRoute || after.lastRoute !== before.lastRoute;
  const ok = humanDelta > 0.05 || routeChanged || after.moving === 1;
  return { name: "field-move", ok, target, point, before, after, humanDelta, routeChanged };
}

async function runFieldCameraMouseTest(page, opts) {
  const before = await readFieldMouseState(page);
  const start = await logicalToPagePoint(page, 400, 300);
  const end = await logicalToPagePoint(page, 500, 340);
  await page.mouse.move(start.x, start.y);
  await page.mouse.down({ button: "middle" });
  await page.mouse.move(end.x, end.y, { steps: 8 });
  await tickWasm(page, 5, opts.tickMs);
  await page.mouse.up({ button: "middle" });
  await tickWasm(page, 5, opts.tickMs);
  const after = await readFieldMouseState(page);
  const cameraDelta = Math.hypot((after.cameraH ?? 0) - (before.cameraH ?? 0), (after.cameraV ?? 0) - (before.cameraV ?? 0));
  return { name: "field-camera", ok: cameraDelta > 0.0001, before, after, cameraDelta };
}

async function runFieldWheelMouseTest(page, opts) {
  const before = await readFieldMouseState(page);
  const point = await logicalToPagePoint(page, 400, 300);
  await page.mouse.move(point.x, point.y);
  await page.mouse.wheel(0, -120);
  await tickWasm(page, 8, opts.tickMs);
  const after = await readFieldMouseState(page);
  const sightDelta = Math.abs((after.cameraSightLength ?? 0) - (before.cameraSightLength ?? 0));
  const wantDelta = Math.abs((after.cameraWantLength ?? 0) - (before.cameraWantLength ?? 0));
  return { name: "field-wheel", ok: sightDelta > 0.0001 || wantDelta > 0.0001, before, after, sightDelta, wantDelta };
}

async function runMouseTests(page, opts) {
  const tests = [];
  for (const test of opts.mouseTests) {
    if (test === "field-ui") {
      tests.push(await runFieldUiMouseTest(page, opts));
    } else if (test === "field-move") {
      tests.push(await runFieldMoveMouseTest(page, opts));
    } else if (test === "field-camera") {
      tests.push(await runFieldCameraMouseTest(page, opts));
    } else if (test === "field-wheel") {
      tests.push(await runFieldWheelMouseTest(page, opts));
    } else {
      tests.push({ name: test, ok: false, error: "unknown mouse test" });
    }
  }
  return tests;
}

const opts = parseArgs(process.argv);
if (opts.allStates) {
  const passthrough = [];
  for (let i = 2; i < process.argv.length; i += 1) {
    const arg = process.argv[i];
    if (arg === "--all-states") continue;
    if (arg === "--summary-only") continue;
    if (arg === "--state" || arg === "--screen" || arg === "--screenshot") {
      i += 1;
      continue;
    }
    passthrough.push(arg);
  }

  const states = [];
  for (const state of ALL_GAME_STATES) {
    const screenshotState = state < 0 ? "neg1" : String(state);
    const screenshot = `webclient/client-wasm/build/reports/startup-canvas-state-${screenshotState}.png`;
    const child = spawnSync(
      process.execPath,
      [
        new URL(import.meta.url).pathname,
        ...passthrough,
        "--state",
        String(state),
        "--screenshot",
        screenshot,
        "--summary-only",
      ],
      { encoding: "utf-8", maxBuffer: 64 * 1024 * 1024 },
    );
    let parsed = null;
    try {
      parsed = JSON.parse(child.stdout || "{}");
    } catch (err) {
      parsed = {
        ok: false,
        error: `failed to parse child JSON: ${err?.message || err}`,
        stdout: child.stdout,
        stderr: child.stderr,
      };
    }
    states.push({
      state,
      ok: child.status === 0 && parsed?.ok === true,
      status: child.status,
      gameState: parsed?.gameState ?? null,
      sceneType: parsed?.stateDebug?.sceneType ?? null,
      placeholder: parsed?.stateDebug?.placeholder ?? null,
      label: parsed?.stateDebug?.label ?? null,
      field: parsed?.field ?? null,
      glErrorTotal: parsed?.glErrorTotal ?? null,
      consoleErrorCount: parsed?.consoleErrorCount ?? null,
      screenshot: parsed?.screenshot ?? screenshot,
      error: parsed?.error ?? null,
    });
  }

  const aggregate = {
    ok: states.every((entry) => (
      entry.ok &&
      entry.gameState === entry.state &&
      entry.glErrorTotal === 0 &&
      entry.consoleErrorCount === 0
    )),
    states,
  };
  console.log(JSON.stringify(aggregate, null, 2));
  process.exit(aggregate.ok ? 0 : 1);
}
const browser = await chromium.launch(chromiumLaunchOptions({ headless: true }));
const page = await browser.newPage({ viewport: opts.viewport });
const consoleLog = [];

page.on("console", (msg) => {
  const line = `[${msg.type()}] ${msg.text()}`;
  consoleLog.push(line);
});
page.on("pageerror", (err) => {
  const line = `[pageerror] ${err?.message || err}`;
  consoleLog.push(line);
});

let result = {
  ok: false,
  boot: null,
  ticks: [],
  shutdown: null,
  drawCalls: null,
  primitiveCount: null,
  shaderDrawsSkipped: null,
  fvfXyzrhwDraws: null,
  fvfWeightedDraws: null,
  fvfTex2PlusDraws: null,
  fvf3dVerticesTotal: null,
  fvf3dVerticesInClip: null,
  declDrawCalls: null,
  declSkinnedDrawCalls: null,
  declVerticesTotal: null,
  declVerticesInClip: null,
  declRgbaIndexOrderDraws: null,
  declBgraIndexOrderDraws: null,
  stage1GeneratedTciDraws: null,
  stage1TextransformDraws: null,
  stage1Tci0Draws: null,
  stage1Tci1Draws: null,
  stage1TciOtherDraws: null,
  alphaTestEnabledDraws: null,
  alphaTestDisabledDraws: null,
  blendEnabledDraws: null,
  vegetationAlphaMaskDraws: null,
  vegetationAlphaBlendDraws: null,
  vegetationDraws: null,
  depthTestDisabledDraws: null,
  depthWriteDisabledDraws: null,
  depthWriteGuardForcedDraws: null,
  lightingEnabledDraws: null,
  glErrorTotal: null,
  glErrorDrawCalls: null,
  glErrorLast: null,
  clearCalls: null,
  presentCalls: null,
  beginSceneCalls: null,
  endSceneCalls: null,
  lastClearColor: null,
  lastClearFlags: null,
  lastClearZ: null,
  lastPresentDepthEnabled: null,
  lastPresentDepthWrite: null,
  currentZFunc: null,
  lastPresentAlphaTest: null,
  stage0ColorSelectArg1Draws: null,
  stage0ColorModulateDraws: null,
  stage0AlphaSelectArg1Draws: null,
  stage0AlphaModulateDraws: null,
  cullNoneDraws: null,
  cullCwDraws: null,
  cullCcwDraws: null,
  cullMirrorWorldviewDraws: null,
  cullFrontfaceFlippedDraws: null,
  assetFileOpenAttempts: null,
  assetFileOpenSuccess: null,
  assetFileOpenFail: null,
  assetFileOpenFailMesh: null,
  assetFileOpenFailEnv: null,
  assetFileOpenFailUi: null,
  assetFileOpenFailTexture: null,
  assetFileOpenFailSound: null,
  assetFileOpenFailSamples: [],
  assetPathFallbackAttempts: null,
  assetPathFallbackHits: null,
  assetPathFallbackOr010Hits: null,
  assetPathFallbackSamples: [],
  fvfTop: [],
  fvfDepthWriteDisabledTop: [],
  fvfDepthWriteEnabledTop: [],
  fvf322DrawPrimitiveUp: null,
  fvf322DrawIndexedPrimitiveUp: null,
  fvf322DrawIndexedPrimitive: null,
  fvf322WithStage0Texture: null,
  fvf322WithoutStage0Texture: null,
  fvf322ScreenlikeVertices: null,
  fvf322ScreenlikeDraws: null,
  fvf322ScreenlikeReplayDraws: null,
  fvf322ScreenlikeReplaySuppressed: null,
  textureDrawsSky: null,
  textureDrawsWater: null,
  textureDrawsBright: null,
  waterStage1DisableDraws: null,
  waterStage1ModulateDraws: null,
  waterStage1Modulate2xDraws: null,
  waterStage1AddDraws: null,
  waterBlendEnabledDraws: null,
  waterBlendDisabledDraws: null,
  waterDepthWriteDisabledDraws: null,
  waterFogDisabledDraws: null,
  fvf322BrightDraws: null,
  effectDraws: null,
  fvf322EffectDraws: null,
  stage0ColorOp8Draws: null,
  stage0ColorOp8WithTexture: null,
  stage0ColorOp8WithoutTexture: null,
  stage0ColorOp8PathlessTexture: null,
  stage0ColorOp8LastFvf: null,
  stage0ColorOp8LastWidth: null,
  stage0ColorOp8LastHeight: null,
  stage0ColorOp8LastPathLen: null,
  setStage0ColorOp8Calls: null,
  setStage0ColorOp4Calls: null,
  setStage0ColorOpLastValue: null,
  setTextureStage0SkyCalls: null,
  setTextureStage1SkyCalls: null,
  drawAttemptsWithSkyTexture: null,
  drawAttemptsWithSkyTextureIndexed: null,
  drawAttemptsWithSkyTextureUp: null,
  drawAttemptsWithSkyLastFvf: null,
  skyClipDraws: null,
  skyClipLastVertexCount: null,
  skyClipLastIndexCount: null,
  skyClipLastStableWVertices: null,
  skyClipLastNegativeWVertices: null,
  skyClipLastNearWVertices: null,
  skyClipLastInsideVertices: null,
  skyClipLastLargeNdcVertices: null,
  skyClipLastTriangleCount: null,
  skyClipLastTrianglesAllStableW: null,
  skyClipLastTrianglesAnyUnstableW: null,
  skyClipLastTrianglesAnyOutside: null,
  skyClipLastMinW: null,
  skyClipLastMaxW: null,
  skyClipLastMinNdcX: null,
  skyClipLastMaxNdcX: null,
  skyClipLastMinNdcY: null,
  skyClipLastMaxNdcY: null,
  skyClipLastMinNdcZ: null,
  skyClipLastMaxNdcZ: null,
  skyRenderCalls: null,
  skyHiddenReturns: null,
  skyEligibleCalls: null,
  skyBranchSkipped: null,
  skyMeshNull: null,
  skyMeshDraws: null,
  skyLastDungeon: null,
  skyLastState: null,
  skyLastTextureIndex: null,
  skyLastMeshTextureIndex: null,
  skyLastMeshHasVB: null,
  skyLastMeshHasIB: null,
  skyLastMeshFVF: null,
  skyLastMeshAttCount: null,
  skyLastMeshFaceCount: null,
  skyLastMeshVertexCount: null,
  skyLastMeshRenderResult: null,
  sunRenderCalls: null,
  sunHiddenReturnsBg: null,
  sunHiddenReturnsSelf: null,
  sunOutOfViewport: null,
  sunFlareDraws: null,
  sunLastHide: null,
  sunLastDefSize: null,
  sunLastInViewport: null,
  sunLastFlareCount: null,
  sunLastScreenX: null,
  sunLastScreenY: null,
  sunLastScreenZ: null,
  fvf322AutoClipWDraws: null,
  fvf322AutoClipWRejectDraws: null,
  fvf530AutoClipWDraws: null,
  fvf530AutoClipWRejectDraws: null,
  fvf530Draws: null,
  fvf530LargeBoundsDraws: null,
  fvf530LargeBoundsSkipDraws: null,
  fvf530LargeBoundSamples: [],
  fvf530UnstableWDraws: null,
  fvf530Vertices: null,
  fvf530InsideVertices: null,
  fvf530LastVertexCount: null,
  fvf530LastIndexCount: null,
  fvf530LastStableWVertices: null,
  fvf530LastInsideVertices: null,
  fvf530LastLargeNdcVertices: null,
  fvf530LastMinNdcX: null,
  fvf530LastMaxNdcX: null,
  fvf530LastMinNdcY: null,
  fvf530LastMaxNdcY: null,
  fvf530LastMinNdcZ: null,
  fvf530LastMaxNdcZ: null,
  fvf594AutoClipWDraws: null,
  fvf594AutoClipWRejectDraws: null,
  upResetStream0Calls: null,
  upResetIndicesCalls: null,
  invalidIndexedDraws: null,
  invalidIndicesTotal: null,
  clipWRejectDraws: null,
  clipWRejectTriangles: null,
  clipWKeepTriangles: null,
  clipWEmptySignatures: [],
  drawOrder: null,
  fvf322ClassCounts: [],
  fvf322ClassCountMax: null,
  skinSuspiciousTextureDraws: null,
  skinSuspiciousTextureSamples: [],
  selServerHumans: [],
  debugFlags: null,
  debugSkipFvf: null,
  drawTraceEnabled: false,
  drawTraceProbes: [],
  drawTraceTop: [],
  font2: null,
  gameState: null,
  stateDebug: null,
  screen: opts.screen,
  selChar: null,
  field: null,
  fieldVisual: null,
  fieldCaptures: null,
  fieldCapturesDir: null,
  mouseTests: [],
  socket: null,
  dummyLoginReturn: null,
  pixel: null,
  camera: null,
  renderVisible: false,
  screenshot: null,
  error: null,
};

try {
  result.step = "goto";
  const launchUrl = harnessUrl(opts);
  result.url = launchUrl;
  result.viewport = opts.viewport;
  result.requestedCanvas = opts.canvas;
  result.requestedLogical = opts.logical || opts.canvas;
  result.fit = opts.fit;
  result.layout = opts.layout;
  await page.goto(launchUrl, { waitUntil: "load", timeout: opts.timeoutMs });
  result.step = "wait-runtime";
  await page.waitForFunction(
    () => window.__runtimeReady === true || /runtime initialized/.test(document.getElementById("log")?.textContent || ""),
    { timeout: opts.timeoutMs },
  );
  await page.evaluate(() => {
    if (typeof window.stopAutoTick === "function") window.stopAutoTick();
    if (typeof window.write === "function") window.write = () => {};
    if (window.Module) {
      Module.print = () => {};
      Module.printErr = () => {};
    }
    const log = document.getElementById("log");
    if (log) log.textContent = "";
  });

  if (opts.preBootDebugFlags !== null) {
    await page.evaluate((debugFlags) => {
      if (typeof Module._wyd_d3d9_set_debug_flags === "function") Module._wyd_d3d9_set_debug_flags(debugFlags >>> 0);
    }, opts.preBootDebugFlags);
  }

  result.step = "boot";
  result.boot = await page.evaluate(() => Module._wyd_boot_client(0));
  result.step = "configure-state";
  await page.evaluate(({ state, debugFlags, debugSkipFvf, fieldMode, weatherMode }) => {
    if (typeof Module._wyd_set_field_mode === "function") Module._wyd_set_field_mode(fieldMode === "placeholder" ? 0 : 1);
    if (typeof Module._wyd_d3d9_set_debug_flags === "function") Module._wyd_d3d9_set_debug_flags(debugFlags >>> 0);
    if (typeof Module._wyd_d3d9_set_debug_skip_fvf === "function") Module._wyd_d3d9_set_debug_skip_fvf(debugSkipFvf >>> 0);
    if (typeof Module._wyd_debug_set_weather_mode === "function") Module._wyd_debug_set_weather_mode(weatherMode >>> 0);
    if (typeof Module._wyd_set_game_state === "function") Module._wyd_set_game_state(state);
    if (typeof Module._wyd_d3d9_reset_debug_counters === "function") Module._wyd_d3d9_reset_debug_counters();
    if (typeof Module._wyd_sky_reset_debug_counters === "function") Module._wyd_sky_reset_debug_counters();
    if (typeof Module._wyd_sun_reset_debug_counters === "function") Module._wyd_sun_reset_debug_counters();
  }, { state: opts.state, debugFlags: opts.debugFlags, debugSkipFvf: opts.debugSkipFvf, fieldMode: opts.fieldMode, weatherMode: opts.weatherMode });
  result.debugFlagsEarly = await page.evaluate(() => (
    typeof Module._wyd_d3d9_get_debug_flags === "function" ? Module._wyd_d3d9_get_debug_flags() : null
  ));
  if (opts.dummyLogin) {
    result.step = "dummy-login";
    result.dummyLoginReturn = await page.evaluate(({ host, account, password, socketProxy }) => {
      const hostInput = document.getElementById("debug-host");
      const accountInput = document.getElementById("debug-account");
      const passwordInput = document.getElementById("debug-password");
      const proxyInput = document.getElementById("socket-proxy");
      if (hostInput && host) hostInput.value = host;
      if (accountInput && account) accountInput.value = account;
      if (passwordInput && password) passwordInput.value = password;
      if (proxyInput && socketProxy) proxyInput.value = socketProxy;
      if (typeof window.runDummyLogin !== "function") return -1;
      return window.runDummyLogin();
    }, {
      host: opts.host || "",
      account: opts.account || "OPENWYD01",
      password: opts.password || "TEST1234",
      socketProxy: opts.socketProxy || "",
    });
    await page.waitForTimeout(250);
    if (opts.loginTicks > 0) {
      await page.evaluate(({ count, tickMs }) => {
        for (let i = 0; i < count; i += 1) {
          if (tickMs > 0 && typeof Module._wyd_debug_advance_fake_time === "function") {
            Module._wyd_debug_advance_fake_time(tickMs >>> 0);
          }
          Module._wyd_tick_client();
        }
      }, { count: opts.loginTicks, tickMs: opts.tickMs });
    }
  }
  if (opts.cameraOffset) {
    await page.evaluate((offset) => {
      if (typeof Module._wyd_debug_set_demo_camera_offset === "function") {
        Module._wyd_debug_set_demo_camera_offset(
          Number(offset.dx),
          Number(offset.dy),
          Number(offset.dz),
          Number(offset.dh),
          Number(offset.dv),
        );
      }
    }, opts.cameraOffset);
  } else {
    await page.evaluate(() => {
      if (typeof Module._wyd_debug_clear_demo_camera_offset === "function") {
        Module._wyd_debug_clear_demo_camera_offset();
      }
    });
  }
  result.step = "read-canvas";
  const traceCanvas = await page.evaluate(() => {
    const canvas = document.getElementById("canvas");
    const rect = canvas?.getBoundingClientRect();
    return {
      width: canvas?.width || 1280,
      height: canvas?.height || 720,
      cssWidth: rect?.width || 0,
      cssHeight: rect?.height || 0,
    };
  });
  result.canvas = traceCanvas;
  const traceProbes = materializeTraceProbes(opts, traceCanvas.width, traceCanvas.height);
  result.drawTraceProbes = traceProbes.map((probe) => ({ ...probe, draw: 0, result: "" }));
  if (opts.trace) {
    result.step = "trace-configure";
    await page.evaluate(({ probes }) => {
      if (typeof Module._wyd_d3d9_trace_clear_probes === "function") Module._wyd_d3d9_trace_clear_probes();
      if (typeof Module._wyd_d3d9_trace_set_enabled === "function") Module._wyd_d3d9_trace_set_enabled(1);
      if (typeof Module._wyd_d3d9_trace_reset === "function") Module._wyd_d3d9_trace_reset();
      if (typeof Module._wyd_d3d9_trace_set_probe === "function") {
        for (const probe of probes) {
          Module._wyd_d3d9_trace_set_probe(probe.index >>> 0, Number(probe.x), Number(probe.y));
        }
      }
    }, { probes: traceProbes });
  }
  result.step = "ticks";
  result.ticks = [];
  const canvas = page.locator("#canvas");
  if (opts.fieldCaptureTicks && opts.fieldCaptureTicks.length > 0) {
    const captureTicks = opts.fieldCaptureTicks;
    const maxTicks = Math.max(...captureTicks, opts.ticks);
    let tickOffset = 0;
    const captures = [];
    const dir = opts.fieldCaptureDir;
    fs.mkdirSync(dir, { recursive: true });
    result.fieldCapturesDir = dir;

    for (let ci = 0; ci < captureTicks.length; ci += 1) {
      const targetTick = captureTicks[ci];
      const batchSize = targetTick - tickOffset;
      if (batchSize > 0) {
        const batchResults = await page.evaluate(({ count, tickMs }) => {
          const out = [];
          for (let i = 0; i < count; i += 1) {
            if (tickMs > 0 && typeof Module._wyd_debug_advance_fake_time === "function") {
              Module._wyd_debug_advance_fake_time(tickMs >>> 0);
            }
            out.push(Module._wyd_tick_client());
          }
          return out;
        }, { count: batchSize, tickMs: opts.tickMs });
        for (const v of batchResults) result.ticks.push(v);
        tickOffset = targetTick;
      }

      const screenshotPath = path.join(dir, `field-tick-${String(targetTick).padStart(4, "0")}.png`);
      await canvas.screenshot({ path: screenshotPath });

      const snapshot = await page.evaluate(() => {
        const M = window.Module || {};
        const call = (name) => (typeof M[name] === "function" ? M[name]() : null);
        return {
          glErrorTotal: call("_wyd_d3d9_gl_error_total"),
          glErrorDrawCalls: call("_wyd_d3d9_gl_error_draw_calls"),
          drawCalls: call("_wyd_d3d9_draw_calls"),
          texturedDraws: call("_wyd_d3d9_textured_draws"),
          texDecodeSuccess: call("_wyd_d3d9_tex_decode_success"),
          texDecodeFail: call("_wyd_d3d9_tex_decode_fail"),
          fieldInitialized: call("_wyd_field_initialized"),
          fieldHasGround: call("_wyd_field_has_ground"),
          fieldHasMyHuman: call("_wyd_field_has_my_human"),
          fieldCriticalError: call("_wyd_field_critical_error"),
          cameraX: call("_wyd_debug_camera_x"),
          cameraY: call("_wyd_debug_camera_y"),
          cameraZ: call("_wyd_debug_camera_z"),
          cameraH: call("_wyd_debug_camera_h"),
          cameraV: call("_wyd_debug_camera_v"),
        };
      });
      captures.push({
        tick: targetTick,
        screenshot: screenshotPath,
        ...snapshot,
      });
    }

    const remaining = maxTicks - tickOffset;
    if (remaining > 0) {
      const batchResults = await page.evaluate(({ count, tickMs }) => {
        const out = [];
        for (let i = 0; i < count; i += 1) {
          if (tickMs > 0 && typeof Module._wyd_debug_advance_fake_time === "function") {
            Module._wyd_debug_advance_fake_time(tickMs >>> 0);
          }
          out.push(Module._wyd_tick_client());
        }
        return out;
      }, { count: remaining, tickMs: opts.tickMs });
      for (const v of batchResults) result.ticks.push(v);
    }

    result.fieldCaptures = captures;
    const summaryPath = path.join(dir, "field-captures.json");
    fs.writeFileSync(summaryPath, JSON.stringify(captures, null, 2), "utf-8");
  } else {
    result.ticks = await page.evaluate(({ count, tickMs }) => {
      const out = [];
      for (let i = 0; i < count; i += 1) {
        if (tickMs > 0 && typeof Module._wyd_debug_advance_fake_time === "function") {
          Module._wyd_debug_advance_fake_time(tickMs >>> 0);
        }
        out.push(Module._wyd_tick_client());
      }
      return out;
    }, { count: opts.ticks, tickMs: opts.tickMs });
  }
  if (opts.mouseTests.length > 0) {
    result.step = "mouse-tests";
    result.mouseTests = await runMouseTests(page, opts);
  }
  await page.waitForTimeout(120);
  if (opts.trace) {
    result.step = "trace-readback";
    const tracePayload = await page.evaluate(({ topLimit }) => {
      const text = (ptr) => (typeof Module.UTF8ToString === "function" && ptr ? Module.UTF8ToString(ptr >>> 0) : "");
      const out = {
        enabled: typeof Module._wyd_d3d9_trace_get_enabled === "function" ? Module._wyd_d3d9_trace_get_enabled() >>> 0 : 0,
        probes: [],
        top: [],
      };
      if (typeof Module._wyd_d3d9_trace_probe_capacity === "function") {
        const capacity = Module._wyd_d3d9_trace_probe_capacity() >>> 0;
        for (let i = 0; i < Math.min(capacity, 16); i += 1) {
          const enabled = typeof Module._wyd_d3d9_trace_probe_enabled === "function"
            ? Module._wyd_d3d9_trace_probe_enabled(i >>> 0) >>> 0
            : 0;
          if (!enabled) continue;
          const draw = typeof Module._wyd_d3d9_trace_probe_draw === "function"
            ? Module._wyd_d3d9_trace_probe_draw(i >>> 0) >>> 0
            : 0;
          const ptr = typeof Module._wyd_d3d9_trace_probe_result === "function"
            ? Module._wyd_d3d9_trace_probe_result(i >>> 0)
            : 0;
          const hitCount = typeof Module._wyd_d3d9_trace_probe_hit_count === "function"
            ? Module._wyd_d3d9_trace_probe_hit_count(i >>> 0) >>> 0
            : 0;
          const firstHitDraw = typeof Module._wyd_d3d9_trace_probe_first_hit_draw === "function"
            ? Module._wyd_d3d9_trace_probe_first_hit_draw(i >>> 0) >>> 0
            : 0;
          const firstHitPtr = typeof Module._wyd_d3d9_trace_probe_first_hit_result === "function"
            ? Module._wyd_d3d9_trace_probe_first_hit_result(i >>> 0)
            : 0;
          const nearestHitDraw = typeof Module._wyd_d3d9_trace_probe_nearest_hit_draw === "function"
            ? Module._wyd_d3d9_trace_probe_nearest_hit_draw(i >>> 0) >>> 0
            : 0;
          const nearestHitPtr = typeof Module._wyd_d3d9_trace_probe_nearest_hit_result === "function"
            ? Module._wyd_d3d9_trace_probe_nearest_hit_result(i >>> 0)
            : 0;
          const nearestZWriteDraw = typeof Module._wyd_d3d9_trace_probe_nearest_zwrite_draw === "function"
            ? Module._wyd_d3d9_trace_probe_nearest_zwrite_draw(i >>> 0) >>> 0
            : 0;
          const nearestZWritePtr = typeof Module._wyd_d3d9_trace_probe_nearest_zwrite_result === "function"
            ? Module._wyd_d3d9_trace_probe_nearest_zwrite_result(i >>> 0)
            : 0;
          out.probes.push({
            index: i,
            draw,
            result: text(ptr),
            hitCount,
            firstHitDraw,
            firstHitResult: text(firstHitPtr),
            nearestHitDraw,
            nearestHitResult: text(nearestHitPtr),
            nearestZWriteDraw,
            nearestZWriteResult: text(nearestZWritePtr),
          });
        }
      }
      if (typeof Module._wyd_d3d9_trace_top_count === "function" &&
          typeof Module._wyd_d3d9_trace_top_sample === "function") {
        const count = Module._wyd_d3d9_trace_top_count() >>> 0;
        for (let i = 0; i < Math.min(count, topLimit); i += 1) {
          const sample = text(Module._wyd_d3d9_trace_top_sample(i >>> 0));
          if (sample) out.top.push(sample);
        }
      }
      return out;
    }, { topLimit: opts.traceTop });
    result.drawTraceEnabled = tracePayload.enabled === 1;
    result.drawTraceTop = tracePayload.top;
    result.drawTraceProbes = result.drawTraceProbes.map((probe) => {
      const hit = tracePayload.probes.find((candidate) => candidate.index === probe.index);
      return {
        ...probe,
        draw: hit?.draw || 0,
        result: hit?.result || "",
        hitCount: hit?.hitCount || 0,
        firstHitDraw: hit?.firstHitDraw || 0,
        firstHitResult: hit?.firstHitResult || "",
        nearestHitDraw: hit?.nearestHitDraw || 0,
        nearestHitResult: hit?.nearestHitResult || "",
        nearestZWriteDraw: hit?.nearestZWriteDraw || 0,
        nearestZWriteResult: hit?.nearestZWriteResult || "",
      };
    });
  }
  result.step = "pixel-readback";
  result.pixel = await page.evaluate(() => {
    const canvas = document.getElementById("canvas");
    if (!canvas) return { ok: false, reason: "canvas-not-found" };
    const gl =
      canvas.getContext("webgl2", { preserveDrawingBuffer: true }) ||
      canvas.getContext("webgl", { preserveDrawingBuffer: true }) ||
      canvas.getContext("experimental-webgl");
    if (!gl) return { ok: false, reason: "no-webgl-context" };

    const w = canvas.width;
    const h = canvas.height;
    // readPixels uses bottom-left origin; grid fractions are top-left.
    const probes = [];
    for (let row = 0; row < 5; row++) {
      for (let col = 0; col < 5; col++) {
        const ux = (col + 1) / 6;
        const uy = (row + 1) / 6;
        probes.push({ label: `r${row}c${col}`, ux, uy });
      }
    }
    probes.push({ label: "center", ux: 0.5, uy: 0.5 });
    const results = [];
    for (const probe of probes) {
      const px_x = Math.max(0, Math.min(w - 1, Math.floor(w * probe.ux)));
      const px_y_gl = Math.max(0, Math.min(h - 1, Math.floor(h * (1.0 - probe.uy))));
      const px = new Uint8Array(4);
      gl.readPixels(px_x, px_y_gl, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
      results.push({
        label: probe.label,
        rgba: [px[0], px[1], px[2], px[3]],
      });
    }

    const cx = Math.floor(w * 0.5);
    const cy = Math.floor(h * 0.5);
    const cpx = new Uint8Array(4);
    gl.readPixels(cx, cy, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, cpx);
    return {
      ok: true,
      rgba: [cpx[0], cpx[1], cpx[2], cpx[3]],
      x: cx,
      y: cy,
      width: w,
      height: h,
      probes: results,
    };
  });
  if (result.pixel?.ok && Array.isArray(result.pixel.rgba)) {
    const [r, g, b, a] = result.pixel.rgba;
    result.renderVisible = (r + g + b) > 0 || a > 0;
  }
  result.step = "camera";
  result.camera = await page.evaluate(() => {
    const call = (name) => (typeof Module[name] === "function" ? Module[name]() : null);
    return {
      valid: call("_wyd_debug_camera_valid"),
      standalone: call("_wyd_debug_camera_standalone"),
      x: call("_wyd_debug_camera_x"),
      y: call("_wyd_debug_camera_y"),
      z: call("_wyd_debug_camera_z"),
      h: call("_wyd_debug_camera_h"),
      v: call("_wyd_debug_camera_v"),
      offsetEnabled: call("_wyd_debug_demo_camera_offset_enabled"),
      offsetX: call("_wyd_debug_demo_camera_offset_x"),
      offsetY: call("_wyd_debug_demo_camera_offset_y"),
      offsetZ: call("_wyd_debug_demo_camera_offset_z"),
      offsetH: call("_wyd_debug_demo_camera_offset_h"),
      offsetV: call("_wyd_debug_demo_camera_offset_v"),
    };
  });
  result.drawCalls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_draw_calls === "function") {
      return Module._wyd_d3d9_draw_calls();
    }
    return null;
  });
  result.primitiveCount = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_primitives === "function") {
      return Module._wyd_d3d9_primitives();
    }
    return null;
  });
  result.texDecodeSuccess = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_tex_decode_success === "function") {
      return Module._wyd_d3d9_tex_decode_success();
    }
    return null;
  });
  result.texDecodeFail = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_tex_decode_fail === "function") {
      return Module._wyd_d3d9_tex_decode_fail();
    }
    return null;
  });
  result.texUploads = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_tex_uploads === "function") {
      return Module._wyd_d3d9_tex_uploads();
    }
    return null;
  });
  result.texturedDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_textured_draws === "function") {
      return Module._wyd_d3d9_textured_draws();
    }
    return null;
  });
  result.shaderDrawsSkipped = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_shader_draws_skipped === "function") {
      return Module._wyd_d3d9_shader_draws_skipped();
    }
    return null;
  });
  result.vsUniqueShaders = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_vs_unique_shaders === "function") {
      return Module._wyd_d3d9_vs_unique_shaders();
    }
    return null;
  });
  result.psUniqueShaders = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_ps_unique_shaders === "function") {
      return Module._wyd_d3d9_ps_unique_shaders();
    }
    return null;
  });
  result.vsBindCalls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_vs_bind_calls === "function") {
      return Module._wyd_d3d9_vs_bind_calls();
    }
    return null;
  });
  result.psBindCalls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_ps_bind_calls === "function") {
      return Module._wyd_d3d9_ps_bind_calls();
    }
    return null;
  });
  result.drawsWithVs = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_draws_with_vs === "function") {
      return Module._wyd_d3d9_draws_with_vs();
    }
    return null;
  });
  result.drawsWithPs = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_draws_with_ps === "function") {
      return Module._wyd_d3d9_draws_with_ps();
    }
    return null;
  });
  result.shaderFileOpenAttempts = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_shader_file_open_attempts === "function") {
      return Module._wyd_d3d9_shader_file_open_attempts();
    }
    return null;
  });
  result.shaderFileOpenSuccess = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_shader_file_open_success === "function") {
      return Module._wyd_d3d9_shader_file_open_success();
    }
    return null;
  });
  result.shaderFileOpenFail = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_shader_file_open_fail === "function") {
      return Module._wyd_d3d9_shader_file_open_fail();
    }
    return null;
  });
  result.shaderFileOpenSkinmesh = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_shader_file_open_skinmesh === "function") {
      return Module._wyd_d3d9_shader_file_open_skinmesh();
    }
    return null;
  });
  result.shaderFileOpenVsEffect = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_shader_file_open_vseffect === "function") {
      return Module._wyd_d3d9_shader_file_open_vseffect();
    }
    return null;
  });
  result.shaderFileOpenPsEffect = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_shader_file_open_pseffect === "function") {
      return Module._wyd_d3d9_shader_file_open_pseffect();
    }
    return null;
  });
  result.assetFileOpenAttempts = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_asset_file_open_attempts === "function") {
      return Module._wyd_d3d9_asset_file_open_attempts();
    }
    return null;
  });
  result.assetFileOpenSuccess = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_asset_file_open_success === "function") {
      return Module._wyd_d3d9_asset_file_open_success();
    }
    return null;
  });
  result.assetFileOpenFail = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_asset_file_open_fail === "function") {
      return Module._wyd_d3d9_asset_file_open_fail();
    }
    return null;
  });
  result.assetFileOpenFailMesh = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_asset_file_open_fail_mesh === "function") {
      return Module._wyd_d3d9_asset_file_open_fail_mesh();
    }
    return null;
  });
  result.assetFileOpenFailEnv = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_asset_file_open_fail_env === "function") {
      return Module._wyd_d3d9_asset_file_open_fail_env();
    }
    return null;
  });
  result.assetFileOpenFailUi = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_asset_file_open_fail_ui === "function") {
      return Module._wyd_d3d9_asset_file_open_fail_ui();
    }
    return null;
  });
  result.assetFileOpenFailTexture = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_asset_file_open_fail_texture === "function") {
      return Module._wyd_d3d9_asset_file_open_fail_texture();
    }
    return null;
  });
  result.assetFileOpenFailSound = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_asset_file_open_fail_sound === "function") {
      return Module._wyd_d3d9_asset_file_open_fail_sound();
    }
    return null;
  });
  result.assetFileOpenFailSamples = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_asset_file_open_fail_sample_count !== "function") return [];
    if (typeof Module._wyd_d3d9_asset_file_open_fail_sample !== "function") return [];
    const decoder = typeof TextDecoder === "function" ? new TextDecoder("utf-8") : null;
    const decodeCString = (ptr) => {
      if (!ptr) return "";
      if (!decoder) return "";
      let heap;
      try {
        heap = Module.HEAPU8;
      } catch {
        return "";
      }
      if (!heap) return "";
      let end = ptr >>> 0;
      while (end < heap.length && heap[end] !== 0) end += 1;
      return decoder.decode(heap.subarray(ptr >>> 0, end));
    };
    const pushSample = (out, ptr) => {
      const text = decodeCString(ptr >>> 0);
      if (text) out.push(text);
    };
    const out = [];
    const count = Module._wyd_d3d9_asset_file_open_fail_sample_count() >>> 0;
    const limit = Math.min(count, 24);
    for (let i = 0; i < limit; i += 1) {
      const ptr = Module._wyd_d3d9_asset_file_open_fail_sample(i >>> 0);
      pushSample(out, ptr);
    }
    return out;
  });
  result.assetPathFallbackAttempts = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_asset_path_fallback_attempts === "function") {
      return Module._wyd_d3d9_asset_path_fallback_attempts();
    }
    return null;
  });
  result.assetPathFallbackHits = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_asset_path_fallback_hits === "function") {
      return Module._wyd_d3d9_asset_path_fallback_hits();
    }
    return null;
  });
  result.assetPathFallbackOr010Hits = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_asset_path_fallback_or010_hits === "function") {
      return Module._wyd_d3d9_asset_path_fallback_or010_hits();
    }
    return null;
  });
  result.assetPathFallbackSamples = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_asset_path_fallback_sample_count !== "function") return [];
    if (typeof Module._wyd_d3d9_asset_path_fallback_sample !== "function") return [];
    const out = [];
    const count = Module._wyd_d3d9_asset_path_fallback_sample_count() >>> 0;
    const limit = Math.min(count, 24);
    for (let i = 0; i < limit; i += 1) {
      const ptr = Module._wyd_d3d9_asset_path_fallback_sample(i >>> 0);
      const text = (typeof Module.UTF8ToString === "function" && ptr)
        ? Module.UTF8ToString(ptr >>> 0)
        : "";
      if (text) out.push(text);
    }
    return out;
  });
  result.activeVsHashLo = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_active_vs_hash_lo === "function") {
      return Module._wyd_d3d9_active_vs_hash_lo() >>> 0;
    }
    return null;
  });
  result.activeVsHashHi = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_active_vs_hash_hi === "function") {
      return Module._wyd_d3d9_active_vs_hash_hi() >>> 0;
    }
    return null;
  });
  result.activePsHashLo = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_active_ps_hash_lo === "function") {
      return Module._wyd_d3d9_active_ps_hash_lo() >>> 0;
    }
    return null;
  });
  result.activePsHashHi = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_active_ps_hash_hi === "function") {
      return Module._wyd_d3d9_active_ps_hash_hi() >>> 0;
    }
    return null;
  });
  result.vsTop = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_vs_top_hash_lo !== "function") return [];
    const out = [];
    for (let i = 0; i < 3; i += 1) {
      out.push({
        rank: i,
        hashLo: Module._wyd_d3d9_vs_top_hash_lo(i) >>> 0,
        hashHi: Module._wyd_d3d9_vs_top_hash_hi(i) >>> 0,
        binds: Module._wyd_d3d9_vs_top_binds(i) >>> 0,
        uses: Module._wyd_d3d9_vs_top_uses(i) >>> 0,
        skips: Module._wyd_d3d9_vs_top_skips(i) >>> 0,
        version: Module._wyd_d3d9_vs_top_version(i) >>> 0
      });
    }
    return out;
  });
  result.psTop = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_ps_top_hash_lo !== "function") return [];
    const out = [];
    for (let i = 0; i < 3; i += 1) {
      out.push({
        rank: i,
        hashLo: Module._wyd_d3d9_ps_top_hash_lo(i) >>> 0,
        hashHi: Module._wyd_d3d9_ps_top_hash_hi(i) >>> 0,
        binds: Module._wyd_d3d9_ps_top_binds(i) >>> 0,
        uses: Module._wyd_d3d9_ps_top_uses(i) >>> 0,
        skips: Module._wyd_d3d9_ps_top_skips(i) >>> 0,
        version: Module._wyd_d3d9_ps_top_version(i) >>> 0
      });
    }
    return out;
  });
  result.fvfXyzrhwDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf_xyzrhw_draws === "function") {
      return Module._wyd_d3d9_fvf_xyzrhw_draws();
    }
    return null;
  });
  result.fvfWeightedDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf_weighted_draws === "function") {
      return Module._wyd_d3d9_fvf_weighted_draws();
    }
    return null;
  });
  result.fvfTex2PlusDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf_tex2plus_draws === "function") {
      return Module._wyd_d3d9_fvf_tex2plus_draws();
    }
    return null;
  });
  result.fvf3dVerticesTotal = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf_3d_vertices_total === "function") {
      return Module._wyd_d3d9_fvf_3d_vertices_total();
    }
    return null;
  });
  result.fvf3dVerticesInClip = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf_3d_vertices_in_clip === "function") {
      return Module._wyd_d3d9_fvf_3d_vertices_in_clip();
    }
    return null;
  });
  result.declDrawCalls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_decl_draw_calls === "function") {
      return Module._wyd_d3d9_decl_draw_calls();
    }
    return null;
  });
  result.declSkinnedDrawCalls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_decl_skinned_draw_calls === "function") {
      return Module._wyd_d3d9_decl_skinned_draw_calls();
    }
    return null;
  });
  result.declVerticesTotal = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_decl_vertices_total === "function") {
      return Module._wyd_d3d9_decl_vertices_total();
    }
    return null;
  });
  result.declVerticesInClip = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_decl_vertices_in_clip === "function") {
      return Module._wyd_d3d9_decl_vertices_in_clip();
    }
    return null;
  });
  result.declRgbaIndexOrderDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_decl_rgba_index_order_draws === "function") {
      return Module._wyd_d3d9_decl_rgba_index_order_draws();
    }
    return null;
  });
  result.declBgraIndexOrderDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_decl_bgra_index_order_draws === "function") {
      return Module._wyd_d3d9_decl_bgra_index_order_draws();
    }
    return null;
  });
  result.stage1GeneratedTciDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage1_generated_tci_draws === "function") {
      return Module._wyd_d3d9_stage1_generated_tci_draws();
    }
    return null;
  });
  result.stage1TextransformDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage1_textransform_draws === "function") {
      return Module._wyd_d3d9_stage1_textransform_draws();
    }
    return null;
  });
  result.stage1Tci0Draws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage1_tci0_draws === "function") {
      return Module._wyd_d3d9_stage1_tci0_draws();
    }
    return null;
  });
  result.stage1Tci1Draws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage1_tci1_draws === "function") {
      return Module._wyd_d3d9_stage1_tci1_draws();
    }
    return null;
  });
  result.stage1TciOtherDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage1_tci_other_draws === "function") {
      return Module._wyd_d3d9_stage1_tci_other_draws();
    }
    return null;
  });
  result.alphaTestEnabledDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_alpha_test_enabled_draws === "function") {
      return Module._wyd_d3d9_alpha_test_enabled_draws();
    }
    return null;
  });
  result.alphaTestDisabledDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_alpha_test_disabled_draws === "function") {
      return Module._wyd_d3d9_alpha_test_disabled_draws();
    }
    return null;
  });
  result.blendEnabledDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_blend_enabled_draws === "function") {
      return Module._wyd_d3d9_blend_enabled_draws();
    }
    return null;
  });
  result.vegetationAlphaMaskDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_vegetation_alpha_mask_draws === "function") {
      return Module._wyd_d3d9_vegetation_alpha_mask_draws();
    }
    return null;
  });
  result.vegetationAlphaBlendDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_vegetation_alpha_blend_draws === "function") {
      return Module._wyd_d3d9_vegetation_alpha_blend_draws();
    }
    return null;
  });
  result.vegetationDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_vegetation_draws === "function") {
      return Module._wyd_d3d9_vegetation_draws();
    }
    return null;
  });
  result.depthTestDisabledDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_depth_test_disabled_draws === "function") {
      return Module._wyd_d3d9_depth_test_disabled_draws();
    }
    return null;
  });
  result.depthWriteDisabledDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_depth_write_disabled_draws === "function") {
      return Module._wyd_d3d9_depth_write_disabled_draws();
    }
    return null;
  });
  result.depthWriteGuardForcedDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_depth_write_guard_forced_draws === "function") {
      return Module._wyd_d3d9_depth_write_guard_forced_draws();
    }
    return null;
  });
  result.lightingEnabledDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_lighting_enabled_draws === "function") {
      return Module._wyd_d3d9_lighting_enabled_draws();
    }
    return null;
  });
  result.glErrorTotal = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_gl_error_total === "function") {
      return Module._wyd_d3d9_gl_error_total();
    }
    return null;
  });
  result.glErrorDrawCalls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_gl_error_draw_calls === "function") {
      return Module._wyd_d3d9_gl_error_draw_calls();
    }
    return null;
  });
  result.glErrorLast = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_gl_error_last === "function") {
      return Module._wyd_d3d9_gl_error_last();
    }
    return null;
  });
  Object.assign(result, await page.evaluate(() => {
    const call = (name) => (typeof Module[name] === "function" ? Module[name]() : null);
    return {
      clearCalls: call("_wyd_d3d9_clear_calls"),
      presentCalls: call("_wyd_d3d9_present_calls"),
      beginSceneCalls: call("_wyd_d3d9_begin_scene_calls"),
      endSceneCalls: call("_wyd_d3d9_end_scene_calls"),
      lastClearColor: call("_wyd_d3d9_last_clear_color"),
      lastClearFlags: call("_wyd_d3d9_last_clear_flags"),
      lastClearZ: call("_wyd_d3d9_last_clear_z"),
      lastPresentDepthEnabled: call("_wyd_d3d9_last_present_depth_enabled"),
      lastPresentDepthWrite: call("_wyd_d3d9_last_present_depth_write"),
      currentZFunc: call("_wyd_d3d9_current_z_func"),
      lastPresentAlphaTest: call("_wyd_d3d9_last_present_alpha_test"),
      lastPresentFog: call("_wyd_d3d9_last_present_fog"),
    };
  }));
  result.fogEnabledDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fog_enabled_draws === "function") {
      return Module._wyd_d3d9_fog_enabled_draws();
    }
    return null;
  });
  result.fogState = await page.evaluate(() => {
    const call = (name) => (typeof Module[name] === "function" ? Module[name]() : null);
    return {
      enabled: call("_wyd_d3d9_fog_enabled"),
      start: call("_wyd_d3d9_fog_start"),
      end: call("_wyd_d3d9_fog_end"),
      color: call("_wyd_d3d9_fog_color"),
      skinDraws: call("_wyd_d3d9_fog_skin_draws"),
    };
  });
  result.wireframeDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_wireframe_draws === "function") {
      return Module._wyd_d3d9_wireframe_draws();
    }
    return null;
  });
  result.materialSetCalls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_material_set_calls === "function") {
      return Module._wyd_d3d9_material_set_calls();
    }
    return null;
  });
  result.lightSetCalls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_light_set_calls === "function") {
      return Module._wyd_d3d9_light_set_calls();
    }
    return null;
  });
  result.lightEnableCalls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_light_enable_calls === "function") {
      return Module._wyd_d3d9_light_enable_calls();
    }
    return null;
  });
  result.lightedVertices = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_lighted_vertices === "function") {
      return Module._wyd_d3d9_lighted_vertices();
    }
    return null;
  });
  result.directionalLightedVertices = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_directional_lighted_vertices === "function") {
      return Module._wyd_d3d9_directional_lighted_vertices();
    }
    return null;
  });
  result.pointLightedVertices = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_point_lighted_vertices === "function") {
      return Module._wyd_d3d9_point_lighted_vertices();
    }
    return null;
  });
  result.spotLightedVertices = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_spot_lighted_vertices === "function") {
      return Module._wyd_d3d9_spot_lighted_vertices();
    }
    return null;
  });
  result.specularLightedVertices = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_specular_lighted_vertices === "function") {
      return Module._wyd_d3d9_specular_lighted_vertices();
    }
    return null;
  });
  result.stage0ColorSelectArg1Draws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage0_color_selectarg1_draws === "function") {
      return Module._wyd_d3d9_stage0_color_selectarg1_draws();
    }
    return null;
  });
  result.stage0ColorModulateDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage0_color_modulate_draws === "function") {
      return Module._wyd_d3d9_stage0_color_modulate_draws();
    }
    return null;
  });
  result.stage0AlphaSelectArg1Draws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage0_alpha_selectarg1_draws === "function") {
      return Module._wyd_d3d9_stage0_alpha_selectarg1_draws();
    }
    return null;
  });
  result.stage0AlphaModulateDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage0_alpha_modulate_draws === "function") {
      return Module._wyd_d3d9_stage0_alpha_modulate_draws();
    }
    return null;
  });
  result.cullNoneDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_cull_none_draws === "function") {
      return Module._wyd_d3d9_cull_none_draws();
    }
    return null;
  });
  result.cullCwDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_cull_cw_draws === "function") {
      return Module._wyd_d3d9_cull_cw_draws();
    }
    return null;
  });
  result.cullCcwDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_cull_ccw_draws === "function") {
      return Module._wyd_d3d9_cull_ccw_draws();
    }
    return null;
  });
  result.cullMirrorWorldviewDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_cull_mirror_worldview_draws === "function") {
      return Module._wyd_d3d9_cull_mirror_worldview_draws();
    }
    return null;
  });
  result.cullFrontfaceFlippedDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_cull_frontface_flipped_draws === "function") {
      return Module._wyd_d3d9_cull_frontface_flipped_draws();
    }
    return null;
  });
  result.fvfTop = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf_top_code !== "function") return [];
    const out = [];
    for (let i = 0; i < 3; i += 1) {
      out.push({
        rank: i,
        code: Module._wyd_d3d9_fvf_top_code(i) >>> 0,
        count: Module._wyd_d3d9_fvf_top_count(i) >>> 0,
      });
    }
    return out;
  });
  result.fvfDepthWriteDisabledTop = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf_depth_write_disabled_top_code !== "function") return [];
    const out = [];
    for (let i = 0; i < 3; i += 1) {
      out.push({
        rank: i,
        code: Module._wyd_d3d9_fvf_depth_write_disabled_top_code(i) >>> 0,
        count: Module._wyd_d3d9_fvf_depth_write_disabled_top_count(i) >>> 0,
      });
    }
    return out;
  });
  result.fvfDepthWriteEnabledTop = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf_depth_write_enabled_top_code !== "function") return [];
    const out = [];
    for (let i = 0; i < 3; i += 1) {
      out.push({
        rank: i,
        code: Module._wyd_d3d9_fvf_depth_write_enabled_top_code(i) >>> 0,
        count: Module._wyd_d3d9_fvf_depth_write_enabled_top_count(i) >>> 0,
      });
    }
    return out;
  });
  result.fvf322DrawPrimitiveUp = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf322_draw_primitive_up === "function") {
      return Module._wyd_d3d9_fvf322_draw_primitive_up();
    }
    return null;
  });
  result.fvf322DrawIndexedPrimitiveUp = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf322_draw_indexed_primitive_up === "function") {
      return Module._wyd_d3d9_fvf322_draw_indexed_primitive_up();
    }
    return null;
  });
  result.fvf322DrawIndexedPrimitive = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf322_draw_indexed_primitive === "function") {
      return Module._wyd_d3d9_fvf322_draw_indexed_primitive();
    }
    return null;
  });
  result.fvf322WithStage0Texture = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf322_with_stage0_texture === "function") {
      return Module._wyd_d3d9_fvf322_with_stage0_texture();
    }
    return null;
  });
  result.fvf322WithoutStage0Texture = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf322_without_stage0_texture === "function") {
      return Module._wyd_d3d9_fvf322_without_stage0_texture();
    }
    return null;
  });
  result.fvf322ScreenlikeVertices = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf322_screenlike_vertices === "function") {
      return Module._wyd_d3d9_fvf322_screenlike_vertices();
    }
    return null;
  });
  result.fvf322ScreenlikeDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf322_screenlike_draws === "function") {
      return Module._wyd_d3d9_fvf322_screenlike_draws();
    }
    return null;
  });
  result.fvf322ScreenlikeReplayDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf322_screenlike_replay_draws === "function") {
      return Module._wyd_d3d9_fvf322_screenlike_replay_draws();
    }
    return null;
  });
  result.fvf322ScreenlikeReplaySuppressed = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf322_screenlike_replay_suppressed === "function") {
      return Module._wyd_d3d9_fvf322_screenlike_replay_suppressed();
    }
    return null;
  });
  result.textureDrawsSky = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_texture_draws_sky === "function") {
      return Module._wyd_d3d9_texture_draws_sky();
    }
    return null;
  });
  result.textureDrawsWater = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_texture_draws_water === "function") {
      return Module._wyd_d3d9_texture_draws_water();
    }
    return null;
  });
  result.waterStage1DisableDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_water_stage1_disable_draws === "function") {
      return Module._wyd_d3d9_water_stage1_disable_draws();
    }
    return null;
  });
  result.waterStage1ModulateDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_water_stage1_modulate_draws === "function") {
      return Module._wyd_d3d9_water_stage1_modulate_draws();
    }
    return null;
  });
  result.waterStage1Modulate2xDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_water_stage1_modulate2x_draws === "function") {
      return Module._wyd_d3d9_water_stage1_modulate2x_draws();
    }
    return null;
  });
  result.waterStage1AddDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_water_stage1_add_draws === "function") {
      return Module._wyd_d3d9_water_stage1_add_draws();
    }
    return null;
  });
  result.waterBlendEnabledDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_water_blend_enabled_draws === "function") {
      return Module._wyd_d3d9_water_blend_enabled_draws();
    }
    return null;
  });
  result.waterBlendDisabledDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_water_blend_disabled_draws === "function") {
      return Module._wyd_d3d9_water_blend_disabled_draws();
    }
    return null;
  });
  result.waterDepthWriteDisabledDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_water_depth_write_disabled_draws === "function") {
      return Module._wyd_d3d9_water_depth_write_disabled_draws();
    }
    return null;
  });
  result.waterFogDisabledDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_water_fog_disabled_draws === "function") {
      return Module._wyd_d3d9_water_fog_disabled_draws();
    }
    return null;
  });
  result.textureDrawsBright = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_texture_draws_bright === "function") {
      return Module._wyd_d3d9_texture_draws_bright();
    }
    return null;
  });
  result.fvf322BrightDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf322_bright_draws === "function") {
      return Module._wyd_d3d9_fvf322_bright_draws();
    }
    return null;
  });
  result.effectDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_effect_draws === "function") {
      return Module._wyd_d3d9_effect_draws();
    }
    return null;
  });
  result.fvf322EffectDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf322_effect_draws === "function") {
      return Module._wyd_d3d9_fvf322_effect_draws();
    }
    return null;
  });
  result.stage0ColorOp8Draws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage0_colorop8_draws === "function") return Module._wyd_d3d9_stage0_colorop8_draws();
    return null;
  });
  result.stage0ColorOp8WithTexture = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage0_colorop8_with_texture === "function") return Module._wyd_d3d9_stage0_colorop8_with_texture();
    return null;
  });
  result.stage0ColorOp8WithoutTexture = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage0_colorop8_without_texture === "function") return Module._wyd_d3d9_stage0_colorop8_without_texture();
    return null;
  });
  result.stage0ColorOp8PathlessTexture = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage0_colorop8_pathless_texture === "function") return Module._wyd_d3d9_stage0_colorop8_pathless_texture();
    return null;
  });
  result.stage0ColorOp8LastFvf = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage0_colorop8_last_fvf === "function") return Module._wyd_d3d9_stage0_colorop8_last_fvf();
    return null;
  });
  result.stage0ColorOp8LastWidth = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage0_colorop8_last_width === "function") return Module._wyd_d3d9_stage0_colorop8_last_width();
    return null;
  });
  result.stage0ColorOp8LastHeight = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage0_colorop8_last_height === "function") return Module._wyd_d3d9_stage0_colorop8_last_height();
    return null;
  });
  result.stage0ColorOp8LastPathLen = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_stage0_colorop8_last_path_len === "function") return Module._wyd_d3d9_stage0_colorop8_last_path_len();
    return null;
  });
  result.setStage0ColorOp8Calls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_set_stage0_colorop8_calls === "function") return Module._wyd_d3d9_set_stage0_colorop8_calls();
    return null;
  });
  result.setStage0ColorOp4Calls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_set_stage0_colorop4_calls === "function") return Module._wyd_d3d9_set_stage0_colorop4_calls();
    return null;
  });
  result.setStage0ColorOpLastValue = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_set_stage0_colorop_last_value === "function") return Module._wyd_d3d9_set_stage0_colorop_last_value();
    return null;
  });
  result.setTextureStage0SkyCalls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_set_texture_stage0_sky_calls === "function") return Module._wyd_d3d9_set_texture_stage0_sky_calls();
    return null;
  });
  result.setTextureStage1SkyCalls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_set_texture_stage1_sky_calls === "function") return Module._wyd_d3d9_set_texture_stage1_sky_calls();
    return null;
  });
  result.drawAttemptsWithSkyTexture = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_draw_attempts_with_sky_texture === "function") return Module._wyd_d3d9_draw_attempts_with_sky_texture();
    return null;
  });
  result.drawAttemptsWithSkyTextureIndexed = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_draw_attempts_with_sky_texture_indexed === "function") return Module._wyd_d3d9_draw_attempts_with_sky_texture_indexed();
    return null;
  });
  result.drawAttemptsWithSkyTextureUp = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_draw_attempts_with_sky_texture_up === "function") return Module._wyd_d3d9_draw_attempts_with_sky_texture_up();
    return null;
  });
  result.drawAttemptsWithSkyLastFvf = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_draw_attempts_with_sky_last_fvf === "function") return Module._wyd_d3d9_draw_attempts_with_sky_last_fvf();
    return null;
  });
  Object.assign(result, await page.evaluate(() => {
    const call = (name) => (typeof Module[name] === "function" ? Module[name]() : null);
    return {
      skyClipDraws: call("_wyd_d3d9_sky_clip_draws"),
      skyClipLastVertexCount: call("_wyd_d3d9_sky_clip_last_vertex_count"),
      skyClipLastIndexCount: call("_wyd_d3d9_sky_clip_last_index_count"),
      skyClipLastStableWVertices: call("_wyd_d3d9_sky_clip_last_stable_w_vertices"),
      skyClipLastNegativeWVertices: call("_wyd_d3d9_sky_clip_last_negative_w_vertices"),
      skyClipLastNearWVertices: call("_wyd_d3d9_sky_clip_last_near_w_vertices"),
      skyClipLastInsideVertices: call("_wyd_d3d9_sky_clip_last_inside_vertices"),
      skyClipLastLargeNdcVertices: call("_wyd_d3d9_sky_clip_last_large_ndc_vertices"),
      skyClipLastTriangleCount: call("_wyd_d3d9_sky_clip_last_triangle_count"),
      skyClipLastTrianglesAllStableW: call("_wyd_d3d9_sky_clip_last_triangles_all_stable_w"),
      skyClipLastTrianglesAnyUnstableW: call("_wyd_d3d9_sky_clip_last_triangles_any_unstable_w"),
      skyClipLastTrianglesAnyOutside: call("_wyd_d3d9_sky_clip_last_triangles_any_outside"),
      skyClipLastMinW: call("_wyd_d3d9_sky_clip_last_min_w"),
      skyClipLastMaxW: call("_wyd_d3d9_sky_clip_last_max_w"),
      skyClipLastMinNdcX: call("_wyd_d3d9_sky_clip_last_min_ndc_x"),
      skyClipLastMaxNdcX: call("_wyd_d3d9_sky_clip_last_max_ndc_x"),
      skyClipLastMinNdcY: call("_wyd_d3d9_sky_clip_last_min_ndc_y"),
      skyClipLastMaxNdcY: call("_wyd_d3d9_sky_clip_last_max_ndc_y"),
      skyClipLastMinNdcZ: call("_wyd_d3d9_sky_clip_last_min_ndc_z"),
      skyClipLastMaxNdcZ: call("_wyd_d3d9_sky_clip_last_max_ndc_z"),
    };
  }));
  result.skyRenderCalls = await page.evaluate(() => {
    if (typeof Module._wyd_sky_render_calls === "function") return Module._wyd_sky_render_calls();
    return null;
  });
  result.skyHiddenReturns = await page.evaluate(() => {
    if (typeof Module._wyd_sky_hidden_returns === "function") return Module._wyd_sky_hidden_returns();
    return null;
  });
  result.skyEligibleCalls = await page.evaluate(() => {
    if (typeof Module._wyd_sky_eligible_calls === "function") return Module._wyd_sky_eligible_calls();
    return null;
  });
  result.skyBranchSkipped = await page.evaluate(() => {
    if (typeof Module._wyd_sky_branch_skipped === "function") return Module._wyd_sky_branch_skipped();
    return null;
  });
  result.skyMeshNull = await page.evaluate(() => {
    if (typeof Module._wyd_sky_mesh_null === "function") return Module._wyd_sky_mesh_null();
    return null;
  });
  result.skyMeshDraws = await page.evaluate(() => {
    if (typeof Module._wyd_sky_mesh_draws === "function") return Module._wyd_sky_mesh_draws();
    return null;
  });
  result.skyLastDungeon = await page.evaluate(() => {
    if (typeof Module._wyd_sky_last_dungeon === "function") return Module._wyd_sky_last_dungeon();
    return null;
  });
  result.skyLastState = await page.evaluate(() => {
    if (typeof Module._wyd_sky_last_state === "function") return Module._wyd_sky_last_state();
    return null;
  });
  result.skyLastTextureIndex = await page.evaluate(() => {
    if (typeof Module._wyd_sky_last_texture_index === "function") return Module._wyd_sky_last_texture_index();
    return null;
  });
  result.skyLastMeshTextureIndex = await page.evaluate(() => {
    if (typeof Module._wyd_sky_last_mesh_texture_index === "function") return Module._wyd_sky_last_mesh_texture_index();
    return null;
  });
  result.skyLastMeshHasVB = await page.evaluate(() => {
    if (typeof Module._wyd_sky_last_mesh_has_vb === "function") return Module._wyd_sky_last_mesh_has_vb();
    return null;
  });
  result.skyLastMeshHasIB = await page.evaluate(() => {
    if (typeof Module._wyd_sky_last_mesh_has_ib === "function") return Module._wyd_sky_last_mesh_has_ib();
    return null;
  });
  result.skyLastMeshFVF = await page.evaluate(() => {
    if (typeof Module._wyd_sky_last_mesh_fvf === "function") return Module._wyd_sky_last_mesh_fvf();
    return null;
  });
  result.skyLastMeshAttCount = await page.evaluate(() => {
    if (typeof Module._wyd_sky_last_mesh_att_count === "function") return Module._wyd_sky_last_mesh_att_count();
    return null;
  });
  result.skyLastMeshFaceCount = await page.evaluate(() => {
    if (typeof Module._wyd_sky_last_mesh_face_count === "function") return Module._wyd_sky_last_mesh_face_count();
    return null;
  });
  result.skyLastMeshVertexCount = await page.evaluate(() => {
    if (typeof Module._wyd_sky_last_mesh_vertex_count === "function") return Module._wyd_sky_last_mesh_vertex_count();
    return null;
  });
  result.skyLastMeshRenderResult = await page.evaluate(() => {
    if (typeof Module._wyd_sky_last_mesh_render_result === "function") return Module._wyd_sky_last_mesh_render_result();
    return null;
  });
  result.sunRenderCalls = await page.evaluate(() => {
    if (typeof Module._wyd_sun_render_calls === "function") return Module._wyd_sun_render_calls();
    return null;
  });
  result.sunHiddenReturnsBg = await page.evaluate(() => {
    if (typeof Module._wyd_sun_hidden_returns_bg === "function") return Module._wyd_sun_hidden_returns_bg();
    return null;
  });
  result.sunHiddenReturnsSelf = await page.evaluate(() => {
    if (typeof Module._wyd_sun_hidden_returns_self === "function") return Module._wyd_sun_hidden_returns_self();
    return null;
  });
  result.sunOutOfViewport = await page.evaluate(() => {
    if (typeof Module._wyd_sun_out_of_viewport === "function") return Module._wyd_sun_out_of_viewport();
    return null;
  });
  result.sunFlareDraws = await page.evaluate(() => {
    if (typeof Module._wyd_sun_flare_draws === "function") return Module._wyd_sun_flare_draws();
    return null;
  });
  result.sunLastHide = await page.evaluate(() => {
    if (typeof Module._wyd_sun_last_hide === "function") return Module._wyd_sun_last_hide();
    return null;
  });
  result.sunLastDefSize = await page.evaluate(() => {
    if (typeof Module._wyd_sun_last_def_size === "function") return Module._wyd_sun_last_def_size();
    return null;
  });
  result.sunLastInViewport = await page.evaluate(() => {
    if (typeof Module._wyd_sun_last_in_viewport === "function") return Module._wyd_sun_last_in_viewport();
    return null;
  });
  result.sunLastFlareCount = await page.evaluate(() => {
    if (typeof Module._wyd_sun_last_flare_count === "function") return Module._wyd_sun_last_flare_count();
    return null;
  });
  result.sunLastScreenX = await page.evaluate(() => {
    if (typeof Module._wyd_sun_last_screen_x === "function") return Module._wyd_sun_last_screen_x();
    return null;
  });
  result.sunLastScreenY = await page.evaluate(() => {
    if (typeof Module._wyd_sun_last_screen_y === "function") return Module._wyd_sun_last_screen_y();
    return null;
  });
  result.sunLastScreenZ = await page.evaluate(() => {
    if (typeof Module._wyd_sun_last_screen_z === "function") return Module._wyd_sun_last_screen_z();
    return null;
  });
  result.fvf322AutoClipWDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf322_auto_clipw_draws === "function") {
      return Module._wyd_d3d9_fvf322_auto_clipw_draws();
    }
    return null;
  });
  result.fvf322AutoClipWRejectDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf322_auto_clipw_reject_draws === "function") {
      return Module._wyd_d3d9_fvf322_auto_clipw_reject_draws();
    }
    return null;
  });
  Object.assign(result, await page.evaluate(() => {
    const call = (name) => (typeof Module[name] === "function" ? Module[name]() : null);
    return {
      fvf530AutoClipWDraws: call("_wyd_d3d9_fvf530_auto_clipw_draws"),
      fvf530AutoClipWRejectDraws: call("_wyd_d3d9_fvf530_auto_clipw_reject_draws"),
      fvf530Draws: call("_wyd_d3d9_fvf530_draws"),
      fvf530LargeBoundsDraws: call("_wyd_d3d9_fvf530_large_bounds_draws"),
      fvf530LargeBoundsSkipDraws: call("_wyd_d3d9_fvf530_large_bounds_skip_draws"),
      fvf530LargeBoundSamples: (() => {
        if (typeof Module._wyd_d3d9_fvf530_large_bound_sample_count !== "function") return [];
        if (typeof Module._wyd_d3d9_fvf530_large_bound_sample !== "function") return [];
        const out = [];
        const count = Module._wyd_d3d9_fvf530_large_bound_sample_count() >>> 0;
        for (let i = 0; i < Math.min(count, 24); i += 1) {
          const ptr = Module._wyd_d3d9_fvf530_large_bound_sample(i >>> 0);
          const text = (typeof Module.UTF8ToString === "function" && ptr) ? Module.UTF8ToString(ptr >>> 0) : "";
          if (text) out.push(text);
        }
        return out;
      })(),
      fvf530UnstableWDraws: call("_wyd_d3d9_fvf530_unstable_w_draws"),
      fvf530Vertices: call("_wyd_d3d9_fvf530_vertices"),
      fvf530InsideVertices: call("_wyd_d3d9_fvf530_inside_vertices"),
      fvf530LastVertexCount: call("_wyd_d3d9_fvf530_last_vertex_count"),
      fvf530LastIndexCount: call("_wyd_d3d9_fvf530_last_index_count"),
      fvf530LastStableWVertices: call("_wyd_d3d9_fvf530_last_stable_w_vertices"),
      fvf530LastInsideVertices: call("_wyd_d3d9_fvf530_last_inside_vertices"),
      fvf530LastLargeNdcVertices: call("_wyd_d3d9_fvf530_last_large_ndc_vertices"),
      fvf530LastMinNdcX: call("_wyd_d3d9_fvf530_last_min_ndc_x"),
      fvf530LastMaxNdcX: call("_wyd_d3d9_fvf530_last_max_ndc_x"),
      fvf530LastMinNdcY: call("_wyd_d3d9_fvf530_last_min_ndc_y"),
      fvf530LastMaxNdcY: call("_wyd_d3d9_fvf530_last_max_ndc_y"),
      fvf530LastMinNdcZ: call("_wyd_d3d9_fvf530_last_min_ndc_z"),
      fvf530LastMaxNdcZ: call("_wyd_d3d9_fvf530_last_max_ndc_z"),
    };
  }));
  result.fvf594AutoClipWDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf594_auto_clipw_draws === "function") {
      return Module._wyd_d3d9_fvf594_auto_clipw_draws();
    }
    return null;
  });
  result.fvf594AutoClipWRejectDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_fvf594_auto_clipw_reject_draws === "function") {
      return Module._wyd_d3d9_fvf594_auto_clipw_reject_draws();
    }
    return null;
  });
  result.upResetStream0Calls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_up_reset_stream0_calls === "function") {
      return Module._wyd_d3d9_up_reset_stream0_calls();
    }
    return null;
  });
  result.upResetIndicesCalls = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_up_reset_indices_calls === "function") {
      return Module._wyd_d3d9_up_reset_indices_calls();
    }
    return null;
  });
  result.invalidIndexedDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_invalid_indexed_draws === "function") {
      return Module._wyd_d3d9_invalid_indexed_draws();
    }
    return null;
  });
  result.invalidIndicesTotal = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_invalid_indices_total === "function") {
      return Module._wyd_d3d9_invalid_indices_total();
    }
    return null;
  });
  result.clipWRejectDraws = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_clip_w_reject_draws === "function") {
      return Module._wyd_d3d9_clip_w_reject_draws();
    }
    return null;
  });
  result.clipWRejectTriangles = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_clip_w_reject_triangles === "function") {
      return Module._wyd_d3d9_clip_w_reject_triangles();
    }
    return null;
  });
  result.clipWKeepTriangles = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_clip_w_keep_triangles === "function") {
      return Module._wyd_d3d9_clip_w_keep_triangles();
    }
    return null;
  });
  Object.assign(result, await page.evaluate(() => {
    const call = (name, ...args) => (typeof Module[name] === "function" ? Module[name](...args) : null);
    const text = (ptr) => (typeof Module.UTF8ToString === "function" && ptr ? Module.UTF8ToString(ptr >>> 0) : "");

    const clipWEmptySignatures = [];
    const clipWEmptyCount = call("_wyd_d3d9_clipw_empty_signature_count");
    if (Number.isFinite(clipWEmptyCount)) {
      for (let i = 0; i < Math.min(clipWEmptyCount >>> 0, 32); i += 1) {
        const sample = text(call("_wyd_d3d9_clipw_empty_signature_sample", i >>> 0));
        if (sample) clipWEmptySignatures.push(sample);
      }
    }

    const fvf322ClassMax = call("_wyd_d3d9_fvf322_class_max");
    const fvf322ClassCounts = [];
    const maxClassId = Number.isFinite(fvf322ClassMax) ? (fvf322ClassMax >>> 0) : 8;
    for (let i = 0; i < maxClassId; i += 1) {
      fvf322ClassCounts.push({
        id: i,
        name: text(call("_wyd_d3d9_fvf322_class_name", i >>> 0)),
        count: call("_wyd_d3d9_fvf322_class_count", i >>> 0),
      });
    }

    const skinSuspiciousTextureSamples = [];
    const skinSampleCount = call("_wyd_d3d9_skin_suspicious_texture_sample_count");
    if (Number.isFinite(skinSampleCount)) {
      for (let i = 0; i < Math.min(skinSampleCount >>> 0, 32); i += 1) {
        const sample = text(call("_wyd_d3d9_skin_suspicious_texture_sample", i >>> 0));
        if (sample) skinSuspiciousTextureSamples.push(sample);
      }
    }

    const selServerHumans = [];
    const humanCount = call("_wyd_selserver_human_count");
    if (Number.isFinite(humanCount)) {
      for (let i = 0; i < Math.min(humanCount >>> 0, 50); i += 1) {
        if ((call("_wyd_selserver_human_present", i >>> 0) >>> 0) !== 1) continue;
        selServerHumans.push({
          index: i,
          posX: call("_wyd_selserver_human_pos_x", i >>> 0),
          posY: call("_wyd_selserver_human_pos_y", i >>> 0),
          wantHeight: call("_wyd_selserver_human_want_height", i >>> 0),
          height: call("_wyd_selserver_human_height", i >>> 0),
          groundMask: call("_wyd_selserver_human_ground_mask", i >>> 0),
          groundHeight: call("_wyd_selserver_human_ground_height", i >>> 0),
          skinX: call("_wyd_selserver_human_skin_x", i >>> 0),
          skinY: call("_wyd_selserver_human_skin_y", i >>> 0),
          skinZ: call("_wyd_selserver_human_skin_z", i >>> 0),
          skinMeshType: call("_wyd_selserver_human_skin_mesh_type", i >>> 0),
          objType: call("_wyd_selserver_human_obj_type", i >>> 0),
          visible: call("_wyd_selserver_human_visible", i >>> 0),
          mount: call("_wyd_selserver_human_mount", i >>> 0),
          mountSkinMeshType: call("_wyd_selserver_human_mount_skin_mesh_type", i >>> 0),
          motion: call("_wyd_selserver_human_motion", i >>> 0),
          sentMotion: call("_wyd_selserver_human_sent_motion", i >>> 0),
          loop: call("_wyd_selserver_human_loop", i >>> 0),
          skinAni: call("_wyd_selserver_human_skin_ani", i >>> 0),
          skinFps: call("_wyd_selserver_human_skin_fps", i >>> 0),
          skinOffset: call("_wyd_selserver_human_skin_offset", i >>> 0),
          skinBoneAni: call("_wyd_selserver_human_skin_bone_ani", i >>> 0),
          skinBaseMat: call("_wyd_selserver_human_skin_base_mat", i >>> 0),
          skinAniBaseIndex: call("_wyd_selserver_human_skin_ani_base_index", i >>> 0),
          skinAniCut: call("_wyd_selserver_human_skin_ani_cut", i >>> 0),
          skinGenerated: call("_wyd_selserver_human_skin_generated", i >>> 0),
          skinFrameMeshes: call("_wyd_selserver_human_skin_frame_meshes", i >>> 0),
          mountPresent: call("_wyd_selserver_human_mount_present", i >>> 0),
          mountAni: call("_wyd_selserver_human_mount_ani", i >>> 0),
          mountFps: call("_wyd_selserver_human_mount_fps", i >>> 0),
          mountOffset: call("_wyd_selserver_human_mount_offset", i >>> 0),
          mountAniCut: call("_wyd_selserver_human_mount_ani_cut", i >>> 0),
          mountGenerated: call("_wyd_selserver_human_mount_generated", i >>> 0),
          mountFrameMeshes: call("_wyd_selserver_human_mount_frame_meshes", i >>> 0),
          weaponTypeIndex: call("_wyd_selserver_human_weapon_type_index", i >>> 0),
          headIndex: call("_wyd_selserver_human_head_index", i >>> 0),
          bodyCurrentTable: call("_wyd_selserver_human_body_current_table", i >>> 0),
          bodyCurrentResolvedClip: call("_wyd_selserver_human_body_current_resolved_clip", i >>> 0),
          bodyMountedCurrentTable: call("_wyd_selserver_human_body_mounted_current_table", i >>> 0),
          bodyMountedCurrentResolvedClip: call("_wyd_selserver_human_body_mounted_current_resolved_clip", i >>> 0),
          bodySeatingTable: call("_wyd_selserver_human_body_seating_table", i >>> 0),
          bodySeatingResolvedClip: call("_wyd_selserver_human_body_seating_resolved_clip", i >>> 0),
          bodyMountedSeatingTable: call("_wyd_selserver_human_body_mounted_seating_table", i >>> 0),
          bodyMountedSeatingResolvedClip: call("_wyd_selserver_human_body_mounted_seating_resolved_clip", i >>> 0),
          demoAni: call("_wyd_selserver_human_demo_ani", i >>> 0),
          moving: call("_wyd_selserver_human_moving", i >>> 0),
          progressRate: call("_wyd_selserver_human_progress_rate", i >>> 0),
          maxSpeed: call("_wyd_selserver_human_max_speed", i >>> 0),
          sliding: call("_wyd_selserver_human_sliding", i >>> 0),
          lastRouteIndex: call("_wyd_selserver_human_last_route_index", i >>> 0),
          maxRouteIndex: call("_wyd_selserver_human_max_route_index", i >>> 0),
          routeOutCount: call("_wyd_selserver_human_route_out_count", i >>> 0),
          packetInCount: call("_wyd_selserver_human_packet_in_count", i >>> 0),
          routeOutSpeed: call("_wyd_selserver_human_route_out_speed", i >>> 0),
          routeOutTargetX: call("_wyd_selserver_human_route_out_target_x", i >>> 0),
          routeOutTargetY: call("_wyd_selserver_human_route_out_target_y", i >>> 0),
          routeOutRouteLen: call("_wyd_selserver_human_route_out_route_len", i >>> 0),
          packetInSpeed: call("_wyd_selserver_human_packet_in_speed", i >>> 0),
          packetInTargetX: call("_wyd_selserver_human_packet_in_target_x", i >>> 0),
          packetInTargetY: call("_wyd_selserver_human_packet_in_target_y", i >>> 0),
          packetBeforeSpeed: call("_wyd_selserver_human_packet_before_speed", i >>> 0),
          packetAfterSpeed: call("_wyd_selserver_human_packet_after_speed", i >>> 0),
          targetX: call("_wyd_selserver_human_target_x", i >>> 0),
          targetY: call("_wyd_selserver_human_target_y", i >>> 0),
          deltaX: call("_wyd_selserver_human_delta_x", i >>> 0),
          deltaY: call("_wyd_selserver_human_delta_y", i >>> 0),
        });
      }
    }

    return {
      clipWEmptySignatures,
      selServerSetAnimation: {
        version: call("_wyd_selserver_set_animation_version"),
        count: call("_wyd_selserver_set_animation_count"),
        attackCount: call("_wyd_selserver_set_animation_attack_count"),
        lastMotion: call("_wyd_selserver_set_animation_last_motion"),
        lastLoop: call("_wyd_selserver_set_animation_last_loop"),
        lastSkinMeshType: call("_wyd_selserver_set_animation_last_skin_mesh_type"),
        lastWeaponTypeIndex: call("_wyd_selserver_set_animation_last_weapon_type_index"),
        lastMount: call("_wyd_selserver_set_animation_last_mount"),
        lastMountPresent: call("_wyd_selserver_set_animation_last_mount_present"),
        lastRouteIndex: call("_wyd_selserver_set_animation_last_route_index"),
        lastMaxRouteIndex: call("_wyd_selserver_set_animation_last_max_route_index"),
      },
      selServerMovePacketVersion: call("_wyd_selserver_move_packet_version"),
      drawOrder: {
        firstSky: call("_wyd_d3d9_draw_order_first_sky"),
        firstSkin: call("_wyd_d3d9_draw_order_first_skin"),
        firstTerrain594: call("_wyd_d3d9_draw_order_first_terrain594"),
        firstWater578: call("_wyd_d3d9_draw_order_first_water578"),
        firstFvf530: call("_wyd_d3d9_draw_order_first_fvf530"),
        firstFvf322: call("_wyd_d3d9_draw_order_first_fvf322"),
        countSky: call("_wyd_d3d9_draw_order_count_sky"),
        countSkin: call("_wyd_d3d9_draw_order_count_skin"),
        countTerrain594: call("_wyd_d3d9_draw_order_count_terrain594"),
        countWater578: call("_wyd_d3d9_draw_order_count_water578"),
        countFvf530: call("_wyd_d3d9_draw_order_count_fvf530"),
        countFvf322: call("_wyd_d3d9_draw_order_count_fvf322"),
        frame: {
          firstSky: call("_wyd_d3d9_draw_order_frame_first_sky"),
          firstSkin: call("_wyd_d3d9_draw_order_frame_first_skin"),
          firstTerrain594: call("_wyd_d3d9_draw_order_frame_first_terrain594"),
          firstWater578: call("_wyd_d3d9_draw_order_frame_first_water578"),
          firstFvf530: call("_wyd_d3d9_draw_order_frame_first_fvf530"),
          firstFvf322: call("_wyd_d3d9_draw_order_frame_first_fvf322"),
          countSky: call("_wyd_d3d9_draw_order_frame_count_sky"),
          countSkin: call("_wyd_d3d9_draw_order_frame_count_skin"),
          countTerrain594: call("_wyd_d3d9_draw_order_frame_count_terrain594"),
          countWater578: call("_wyd_d3d9_draw_order_frame_count_water578"),
          countFvf530: call("_wyd_d3d9_draw_order_frame_count_fvf530"),
          countFvf322: call("_wyd_d3d9_draw_order_frame_count_fvf322"),
        },
      },
      fvf322ClassCounts,
      fvf322ClassCountMax: fvf322ClassMax,
      skinSuspiciousTextureDraws: call("_wyd_d3d9_skin_suspicious_texture_draws"),
      skinSuspiciousTextureSamples,
      selServerHumanVersion: call("_wyd_selserver_human_version"),
      selServerHumans,
    };
  }));
  result.font2 = await page.evaluate(() => {
    const call = (name) => (typeof Module[name] === "function" ? Module[name]() : null);
    const textPtr = call("_wyd_font2_last_text");
    const nonEmptyTextPtr = call("_wyd_font2_last_nonempty_text");
    return {
      setTextCalls: call("_wyd_font2_set_text_calls"),
      setTextNonEmpty: call("_wyd_font2_set_text_nonempty"),
      renderCalls: call("_wyd_font2_render_calls"),
      renderNonEmpty: call("_wyd_font2_render_nonempty"),
      textureCreateFail: call("_wyd_font2_texture_create_fail"),
      lockCalls: call("_wyd_font2_lock_calls"),
      lastTextLen: call("_wyd_font2_last_text_len"),
      lastLineCount: call("_wyd_font2_last_line_count"),
      lastSize0: call("_wyd_font2_last_size0"),
      lastSize1: call("_wyd_font2_last_size1"),
      lastSize2: call("_wyd_font2_last_size2"),
      lastAlphaPixels: call("_wyd_font2_last_alpha_pixels"),
      lastHasBitmap: call("_wyd_font2_last_has_bitmap"),
      lastNonEmptyAlphaPixels: call("_wyd_font2_last_nonempty_alpha_pixels"),
      lastNonEmptyHasBitmap: call("_wyd_font2_last_nonempty_has_bitmap"),
      lastNonEmptySize0: call("_wyd_font2_last_nonempty_size0"),
      maxAlphaPixels: call("_wyd_font2_max_alpha_pixels"),
      maxSize0: call("_wyd_font2_max_size0"),
      lastRenderX: call("_wyd_font2_last_render_x"),
      lastRenderY: call("_wyd_font2_last_render_y"),
      lastRenderType: call("_wyd_font2_last_render_type"),
      lastNonEmptyRenderX: call("_wyd_font2_last_nonempty_render_x"),
      lastNonEmptyRenderY: call("_wyd_font2_last_nonempty_render_y"),
      lastNonEmptyRenderType: call("_wyd_font2_last_nonempty_render_type"),
      lastText: typeof Module.UTF8ToString === "function" && textPtr ? Module.UTF8ToString(textPtr >>> 0) : "",
      lastNonEmptyText: typeof Module.UTF8ToString === "function" && nonEmptyTextPtr ? Module.UTF8ToString(nonEmptyTextPtr >>> 0) : "",
    };
  });
  result.debugFlags = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_get_debug_flags === "function") {
      return Module._wyd_d3d9_get_debug_flags();
    }
    return null;
  });
  result.debugSkipFvf = await page.evaluate(() => {
    if (typeof Module._wyd_d3d9_get_debug_skip_fvf === "function") {
      return Module._wyd_d3d9_get_debug_skip_fvf();
    }
    return null;
  });
  result.gameState = await page.evaluate(() => {
    if (typeof Module._wyd_get_game_state === "function") {
      return Module._wyd_get_game_state();
    }
    return null;
  });
  result.stateDebug = await page.evaluate(() => {
    const M = window.Module || {};
    const call = (name, ...args) => (typeof M[name] === "function" ? M[name](...args) : null);
    const str = (name, ...args) => {
      if (typeof M[name] !== "function" || typeof M.UTF8ToString !== "function") return null;
      const ptr = M[name](...args);
      return ptr ? M.UTF8ToString(ptr >>> 0) : "";
    };
    return {
      gameState: call("_wyd_get_game_state"),
      sceneType: call("_wyd_get_scene_type"),
      placeholder: call("_wyd_state_is_placeholder"),
      label: str("_wyd_get_state_debug_label"),
      stateName: str("_wyd_get_state_name", call("_wyd_get_game_state") ?? -999),
    };
  });
  result.field = await page.evaluate(() => {
    const M = window.Module || {};
    const call = (name) => (typeof M[name] === "function" ? M[name]() : null);
    return {
      mode: call("_wyd_get_field_mode"),
      debugFixtureUsed: call("_wyd_field_debug_fixture_used"),
      initialized: call("_wyd_field_initialized"),
      hasGround: call("_wyd_field_has_ground"),
      hasMyHuman: call("_wyd_field_has_my_human"),
      criticalError: call("_wyd_field_critical_error"),
      mapX: call("_wyd_field_map_x"),
      mapY: call("_wyd_field_map_y"),
      myHumanX: call("_wyd_field_myhuman_x"),
      myHumanY: call("_wyd_field_myhuman_y"),
      motion: call("_wyd_field_myhuman_motion"),
      sentMotion: call("_wyd_field_myhuman_sent_motion"),
      moving: call("_wyd_field_myhuman_moving"),
      progressRate: call("_wyd_field_myhuman_progress_rate"),
      lastRouteIndex: call("_wyd_field_myhuman_last_route_index"),
      maxRouteIndex: call("_wyd_field_myhuman_max_route_index"),
      targetX: call("_wyd_field_myhuman_target_x"),
      targetY: call("_wyd_field_myhuman_target_y"),
      moveToX: call("_wyd_field_myhuman_move_to_x"),
      moveToY: call("_wyd_field_myhuman_move_to_y"),
      groundHeightUnderPlayer: call("_wyd_field_ground_height_under_player"),
      groundMaskUnderPlayer: call("_wyd_field_ground_mask_under_player"),
      myHumanHeight: call("_wyd_field_myhuman_height"),
      myHumanWantHeight: call("_wyd_field_myhuman_want_height"),
      heightDelta: call("_wyd_field_myhuman_height_delta"),
      groundNormalX: call("_wyd_field_ground_normal_under_player_x"),
      groundNormalY: call("_wyd_field_ground_normal_under_player_y"),
      groundNormalZ: call("_wyd_field_ground_normal_under_player_z"),
      debugWeatherMode: call("_wyd_debug_get_weather_mode"),
      weatherActive: call("_wyd_field_weather_active"),
      rainVisible: call("_wyd_field_rain_visible"),
      snowVisible: call("_wyd_field_snow_visible"),
      snow2Visible: call("_wyd_field_snow2_visible"),
      objects: {
        count: call("_wyd_field_object_count"),
        failed: call("_wyd_field_object_failed"),
        checksumFailed: call("_wyd_field_object_checksum_failed"),
        sea: call("_wyd_field_object_sea_count"),
        tree: call("_wyd_field_object_tree_count"),
        house: call("_wyd_field_object_house_count"),
        light: call("_wyd_field_object_light_count"),
        generic: call("_wyd_field_object_generic_count"),
        lastMaskIndex: call("_wyd_field_object_last_mask_index"),
        staticDraws: call("_wyd_field_static_object_draws"),
      },
      objectCount: call("_wyd_field_object_count"),
      staticObjectDraws: call("_wyd_field_static_object_draws"),
      lastCritical: {
        type: call("_wyd_debug_last_critical_type"),
        id: call("_wyd_debug_last_critical_id"),
        mesh: call("_wyd_debug_last_critical_mesh"),
        x: call("_wyd_debug_last_critical_x"),
        y: call("_wyd_debug_last_critical_y"),
        mobX: call("_wyd_debug_last_critical_mob_x"),
        mobY: call("_wyd_debug_last_critical_mob_y"),
      },
      inputMouse: {
        x: call("_wyd_input_mouse_x"),
        y: call("_wyd_input_mouse_y"),
        left: call("_wyd_input_mouse_left_down"),
        right: call("_wyd_input_mouse_right_down"),
        middle: call("_wyd_input_mouse_middle_down"),
        events: call("_wyd_input_mouse_event_count"),
        lastMsg: call("_wyd_input_mouse_last_msg"),
        lastWParam: call("_wyd_input_mouse_last_wparam"),
      },
      controlEvent: {
        count: call("_wyd_control_event_count"),
        lastId: call("_wyd_control_last_event_id"),
        lastType: call("_wyd_control_last_event_type"),
        bCharSelected: call("_wyd_control_selected", 65790),
      },
    };
  });
  result.fieldVisual = await page.evaluate(() => {
    const M = window.Module || {};
    const call = (name, ...args) => (typeof M[name] === "function" ? M[name](...args) : null);
    return {
      totalDraws: call("_wyd_field_visual_total_draws"),
      terrainDraws: call("_wyd_field_visual_terrain_draws"),
      groundDraws: call("_wyd_field_visual_ground_draws"),
      waterDraws: call("_wyd_field_visual_water_draws"),
      skyDraws: call("_wyd_field_visual_sky_draws"),
      humanDraws: call("_wyd_field_visual_human_draws"),
      objectDraws: call("_wyd_field_visual_object_draws"),
      effectDraws: call("_wyd_field_visual_effect_draws"),
      hudDraws: call("_wyd_field_visual_hud_draws"),
      hudArtDraws: call("_wyd_field_visual_hud_art_draws"),
      texBuckets: {
        env: call("_wyd_field_visual_tex_bucket_env"),
        effect: call("_wyd_field_visual_tex_bucket_effect"),
        ui: call("_wyd_field_visual_tex_bucket_ui"),
        char: call("_wyd_field_visual_tex_bucket_char"),
        sky: call("_wyd_field_visual_tex_bucket_sky"),
        water: call("_wyd_field_visual_tex_bucket_water"),
        other: call("_wyd_field_visual_tex_bucket_other"),
      },
      fvfBuckets: (() => {
        const size = call("_wyd_field_visual_fvf_bucket_size") || 0;
        const arr = [];
        for (let i = 0; i < size && i < 32; ++i) {
          arr.push({
            fvf: call("_wyd_field_visual_fvf_bucket_code", i),
            count: call("_wyd_field_visual_fvf_bucket_count", i),
          });
        }
        return arr;
      })(),
    };
  });
  result.selChar = await page.evaluate(() => {
    const M = window.Module || {};
    const call = (name, ...args) => (typeof M[name] === "function" ? M[name](...args) : null);
    const str = (name, ...args) => {
      if (typeof M[name] !== "function" || typeof M.UTF8ToString !== "function") return null;
      const ptr = M[name](...args);
      return ptr ? M.UTF8ToString(ptr >>> 0) : "";
    };
    return {
      initialized: call("_wyd_selchar_initialized"),
      charCount: call("_wyd_selchar_char_count"),
      humans: [0, 1, 2, 3].map((slot) => call("_wyd_selchar_human_present", slot)),
      names: [0, 1, 2, 3].map((slot) => str("_wyd_selchar_name", slot)),
      serverListSample: [
        str("_wyd_serverlist_entry", 0, 0),
        str("_wyd_serverlist_entry", 0, 1),
        str("_wyd_serverlist_entry", 1, 0),
        str("_wyd_serverlist_entry", 2, 1),
      ],
    };
  });
  result.socket = await page.evaluate(() => {
    const M = window.Module || {};
    const call = (name) => (typeof M[name] === "function" ? M[name]() : null);
    const str = (name) => {
      if (typeof M[name] !== "function" || typeof M.UTF8ToString !== "function") return null;
      const ptr = M[name]();
      return ptr ? M.UTF8ToString(ptr >>> 0) : "";
    };
    return {
      host: str("_wyd_socket_last_host"),
      proxyUrl: str("_wyd_socket_last_proxy_url"),
      port: call("_wyd_socket_last_port"),
      connectResult: call("_wyd_socket_last_connect_result"),
      lastError: call("_wyd_socket_last_error"),
      bytesSent: call("_wyd_socket_bytes_sent"),
      bytesReceived: call("_wyd_socket_bytes_received"),
      lastSentOpcode: call("_wyd_socket_last_sent_opcode"),
      lastRecvOpcode: call("_wyd_socket_last_recv_opcode"),
    };
  });

  await canvas.screenshot({ path: opts.screenshot });
  result.screenshot = opts.screenshot;

  result.shutdown = await page.evaluate(() => Module._wyd_shutdown_client());

  const isPlaceholder = result.stateDebug?.placeholder === 1;
  const expectedTickCount =
    opts.fieldCaptureTicks && opts.fieldCaptureTicks.length > 0
      ? Math.max(...opts.fieldCaptureTicks, opts.ticks)
      : opts.ticks;
  const fieldCapturesAllOk =
    !result.fieldCaptures ||
    result.fieldCaptures.every((c) => typeof c.glErrorTotal !== "number" || c.glErrorTotal === 0);
  const shouldValidateFieldGround = result.field?.initialized === 1;
  const fieldGroundOk =
    !shouldValidateFieldGround ||
    result.field.groundHeightUnderPlayer == null ||
    result.field.groundHeightUnderPlayer > -100;
  const fieldCriticalOk =
    result.field == null || result.field.criticalError === 0 || result.field.criticalError == null;
  const fieldHeightDeltaOk =
    !shouldValidateFieldGround ||
    result.field.heightDelta == null ||
    result.field.heightDelta === -9999 ||
    Math.abs(result.field.heightDelta) < 100;
  const mouseTestsOk =
    !result.mouseTests ||
    result.mouseTests.every((test) => test.ok === true);
  result.ok =
    result.boot === 1 &&
    result.shutdown === 1 &&
    result.renderVisible === true &&
    (typeof result.glErrorTotal !== "number" || result.glErrorTotal === 0) &&
    (typeof result.drawCalls !== "number" || result.drawCalls > 0) &&
    (isPlaceholder || typeof result.texturedDraws !== "number" || result.texturedDraws > 0) &&
    (isPlaceholder ||
      typeof result.texDecodeSuccess !== "number" ||
      result.texDecodeSuccess > 0 ||
      (typeof result.texturedDraws === "number" && result.texturedDraws > 0)) &&
    (result.gameState === null || result.gameState === opts.state) &&
    Array.isArray(result.ticks) &&
    result.ticks.length === expectedTickCount &&
    result.ticks.every((v) => v >= 0) &&
    fieldGroundOk &&
    fieldCriticalOk &&
    fieldHeightDeltaOk &&
    fieldCapturesAllOk &&
    mouseTestsOk;
} catch (err) {
  result.error = err?.message || String(err);
}

result.consoleTail = consoleLog.slice(-40);
result.consoleErrorCount = consoleLog.filter((line) => line.startsWith("[error]") || line.startsWith("[pageerror]")).length;
result.consoleWarnCount = consoleLog.filter((line) => line.startsWith("[warning]")).length;
result.consoleTotal = consoleLog.length;

const output = opts.summaryOnly
  ? {
      ok: result.ok,
      gameState: result.gameState,
      stateDebug: result.stateDebug,
      canvas: result.canvas,
      viewport: result.viewport,
      requestedLogical: result.requestedLogical,
      fit: result.fit,
      layout: result.layout,
      field: result.field,
      fieldVisual: result.fieldVisual,
      mouseTests: result.mouseTests,
      glErrorTotal: result.glErrorTotal,
      consoleErrorCount: result.consoleErrorCount,
      screenshot: result.screenshot,
      fieldCaptures: result.fieldCaptures,
      fieldCapturesDir: result.fieldCapturesDir,
      error: result.error,
    }
  : result;
console.log(JSON.stringify(output, null, 2));
browser.close().catch(() => {});
process.exit(result.ok ? 0 : 1);
