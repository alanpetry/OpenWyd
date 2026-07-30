#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import playwrightPkg from "../../node_modules/playwright/index.js";
import { chromiumLaunchOptions } from "../../tools/playwright_portable_browser.mjs";

const { chromium } = playwrightPkg;
const args = new Map();
for (let i = 2; i < process.argv.length; i += 2) {
  args.set(process.argv[i], process.argv[i + 1]);
}

const url = args.get("--url");
if (!url) {
  throw new Error("--url is required");
}

const profile = path.resolve(args.get("--profile") || "artifacts/live-login/chromium");
const screenshot = path.resolve(args.get("--screenshot") || "artifacts/live-login/canvas.png");
const timeout = Number.parseInt(args.get("--timeout-ms") || "300000", 10);
const account = args.get("--account") || "ADMIN";
const password = args.get("--password") || "admin";
const host = args.get("--host") || "tmsrv";
const autoDemo = args.get("--auto-demo") === "1";
fs.mkdirSync(profile, { recursive: true });
fs.mkdirSync(path.dirname(screenshot), { recursive: true });

const target = new URL(url);
target.searchParams.set("mode", "play");
target.searchParams.set("state", "7");
target.searchParams.set("logical", "800x600");
target.searchParams.set("fit", "actual");
target.searchParams.set("fieldMode", "real");
target.searchParams.set("autoboot", "1");
target.searchParams.set("autostart", "1");
if (autoDemo) {
  target.searchParams.set("demo", "1");
} else {
  target.searchParams.set("host", host);
  target.searchParams.set("account", account);
  target.searchParams.set("password", password);
}

const context = await chromium.launchPersistentContext(profile, chromiumLaunchOptions({
  headless: true,
  viewport: { width: 800, height: 600 },
  args: [
    "--enable-webgl",
    "--ignore-gpu-blocklist",
    "--use-angle=swiftshader",
    "--enable-unsafe-swiftshader",
  ],
}));
const pages = context.pages();
const page = pages[0] || await context.newPage();
const consoleErrors = [];
page.on("console", message => {
  if (message.type() === "error") consoleErrors.push(message.text());
});
page.on("pageerror", error => consoleErrors.push(error?.message || String(error)));

const readState = () => page.evaluate(() => {
  const M = window.Module || {};
  const call = (name) => typeof M[name] === "function" ? M[name]() : null;
  const str = (name) => {
    if (typeof M[name] !== "function" || typeof M.UTF8ToString !== "function") return null;
    const pointer = M[name]();
    return pointer ? M.UTF8ToString(pointer >>> 0) : "";
  };
  return {
    gameState: call("_wyd_get_game_state"),
    sceneType: call("_wyd_get_scene_type"),
    glErrorTotal: call("_wyd_d3d9_gl_error_total"),
    accountLockDialogVisible: typeof M._wyd_control_visible === "function"
      ? M._wyd_control_visible(66432)
      : null,
    socket: {
      host: str("_wyd_socket_last_host"),
      proxy: str("_wyd_socket_last_proxy_url"),
      connect: call("_wyd_socket_last_connect_result"),
      error: call("_wyd_socket_last_error"),
      bytesSent: call("_wyd_socket_bytes_sent"),
      bytesReceived: call("_wyd_socket_bytes_received"),
      lastSentOpcode: call("_wyd_socket_last_sent_opcode"),
    },
  };
});

const result = {
  ok: false,
  url: target.toString(),
  loginReturn: null,
  before: null,
  after: null,
  demoCredentials: null,
  consoleErrors,
  screenshot,
};

try {
  await page.goto(target.toString(), { waitUntil: "load", timeout });
  await page.waitForFunction(() => window.__runtimeReady === true, null, { timeout });
  result.before = await readState();
  if (!autoDemo) {
    await page.waitForFunction(
      () => window.Module && typeof Module._wyd_get_game_state === "function" &&
        Module._wyd_get_game_state() === 7,
      null,
      { timeout: 30000 },
    );
    result.loginReturn = await page.evaluate(() => window.runDummyLogin());
  }

  const deadline = Date.now() + 15000;
  while (Date.now() < deadline) {
    result.after = await readState();
    if (result.after.gameState === 5) break;
    await page.waitForTimeout(100);
  }
  if (autoDemo) {
    result.demoCredentials = await page.evaluate(() => {
      try {
        return JSON.parse(localStorage.getItem("openwyd.publicDemoAccount.v1") || "null");
      } catch {
        return null;
      }
    });
  }

  const canvas = page.locator("#canvas");
  await canvas.screenshot({ path: screenshot });
  result.ok = (autoDemo || result.loginReturn === 1) &&
    result.after?.gameState === 5 &&
    result.after?.glErrorTotal === 0 &&
    consoleErrors.length === 0 &&
    (!autoDemo || (
      /^DEMO[A-Z2-9]{8}$/.test(result.demoCredentials?.account || "") &&
      result.after?.accountLockDialogVisible === 0
    ));
} catch (error) {
  result.error = error?.message || String(error);
}

console.log(JSON.stringify(result, null, 2));
await context.close();
process.exit(result.ok ? 0 : 1);
