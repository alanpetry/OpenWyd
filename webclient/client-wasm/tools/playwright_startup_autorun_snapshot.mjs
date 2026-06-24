#!/usr/bin/env node

import playwrightPkg from "../../node_modules/playwright/index.js";
import { chromiumLaunchOptions } from "../../tools/playwright_portable_browser.mjs";

const { chromium } = playwrightPkg;

function parseArgs(argv) {
  const opts = {
    url: "http://127.0.0.1:8877/",
    waitMs: 6000,
    timeoutMs: 120000,
    screenshot: "webclient/client-wasm/build/reports/startup-canvas-live.png",
  };

  for (let i = 2; i < argv.length; i += 1) {
    const a = argv[i];
    if (a === "--url" && argv[i + 1]) {
      opts.url = argv[i + 1];
      i += 1;
      continue;
    }
    if (a === "--wait-ms" && argv[i + 1]) {
      opts.waitMs = Number.parseInt(argv[i + 1], 10) || opts.waitMs;
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
  }

  return opts;
}

const opts = parseArgs(process.argv);
const browser = await chromium.launch(chromiumLaunchOptions({ headless: true }));
const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
const consoleTail = [];

page.on("console", (msg) => {
  const line = `[${msg.type()}] ${msg.text()}`;
  consoleTail.push(line);
  if (consoleTail.length > 120) consoleTail.shift();
});

const result = {
  ok: false,
  url: opts.url,
  finalUrl: null,
  gameState: null,
  drawCalls: null,
  primitives: null,
  texDecodeSuccess: null,
  texDecodeFail: null,
  texUploads: null,
  texturedDraws: null,
  shaderDrawsSkipped: null,
  fvfXyzrhwDraws: null,
  fvfWeightedDraws: null,
  fvfTex2PlusDraws: null,
  stage1GeneratedTciDraws: null,
  stage1TextransformDraws: null,
  stage1Tci0Draws: null,
  stage1Tci1Draws: null,
  stage1TciOtherDraws: null,
  screenshot: null,
  error: null,
  consoleTail: [],
};

try {
  await page.goto(opts.url, { waitUntil: "load", timeout: opts.timeoutMs });
  result.finalUrl = page.url();

  await page.waitForFunction(
    () => window.__runtimeReady === true || /runtime initialized/.test(document.getElementById("log")?.textContent || ""),
    { timeout: opts.timeoutMs },
  );

  await page.waitForTimeout(opts.waitMs);

  const stats = await page.evaluate(() => ({
    gameState: typeof Module._wyd_get_game_state === "function" ? Module._wyd_get_game_state() : null,
    drawCalls: typeof Module._wyd_d3d9_draw_calls === "function" ? Module._wyd_d3d9_draw_calls() : null,
    primitives: typeof Module._wyd_d3d9_primitives === "function" ? Module._wyd_d3d9_primitives() : null,
    texDecodeSuccess:
      typeof Module._wyd_d3d9_tex_decode_success === "function" ? Module._wyd_d3d9_tex_decode_success() : null,
    texDecodeFail:
      typeof Module._wyd_d3d9_tex_decode_fail === "function" ? Module._wyd_d3d9_tex_decode_fail() : null,
    texUploads: typeof Module._wyd_d3d9_tex_uploads === "function" ? Module._wyd_d3d9_tex_uploads() : null,
    texturedDraws: typeof Module._wyd_d3d9_textured_draws === "function" ? Module._wyd_d3d9_textured_draws() : null,
    shaderDrawsSkipped:
      typeof Module._wyd_d3d9_shader_draws_skipped === "function" ? Module._wyd_d3d9_shader_draws_skipped() : null,
    fvfXyzrhwDraws:
      typeof Module._wyd_d3d9_fvf_xyzrhw_draws === "function" ? Module._wyd_d3d9_fvf_xyzrhw_draws() : null,
    fvfWeightedDraws:
      typeof Module._wyd_d3d9_fvf_weighted_draws === "function" ? Module._wyd_d3d9_fvf_weighted_draws() : null,
    fvfTex2PlusDraws:
      typeof Module._wyd_d3d9_fvf_tex2plus_draws === "function" ? Module._wyd_d3d9_fvf_tex2plus_draws() : null,
    stage1GeneratedTciDraws:
      typeof Module._wyd_d3d9_stage1_generated_tci_draws === "function"
        ? Module._wyd_d3d9_stage1_generated_tci_draws()
        : null,
    stage1TextransformDraws:
      typeof Module._wyd_d3d9_stage1_textransform_draws === "function"
        ? Module._wyd_d3d9_stage1_textransform_draws()
        : null,
    stage1Tci0Draws:
      typeof Module._wyd_d3d9_stage1_tci0_draws === "function" ? Module._wyd_d3d9_stage1_tci0_draws() : null,
    stage1Tci1Draws:
      typeof Module._wyd_d3d9_stage1_tci1_draws === "function" ? Module._wyd_d3d9_stage1_tci1_draws() : null,
    stage1TciOtherDraws:
      typeof Module._wyd_d3d9_stage1_tci_other_draws === "function"
        ? Module._wyd_d3d9_stage1_tci_other_draws()
        : null,
  }));

  result.gameState = stats.gameState;
  result.drawCalls = stats.drawCalls;
  result.primitives = stats.primitives;
  result.texDecodeSuccess = stats.texDecodeSuccess;
  result.texDecodeFail = stats.texDecodeFail;
  result.texUploads = stats.texUploads;
  result.texturedDraws = stats.texturedDraws;
  result.shaderDrawsSkipped = stats.shaderDrawsSkipped;
  result.fvfXyzrhwDraws = stats.fvfXyzrhwDraws;
  result.fvfWeightedDraws = stats.fvfWeightedDraws;
  result.fvfTex2PlusDraws = stats.fvfTex2PlusDraws;
  result.stage1GeneratedTciDraws = stats.stage1GeneratedTciDraws;
  result.stage1TextransformDraws = stats.stage1TextransformDraws;
  result.stage1Tci0Draws = stats.stage1Tci0Draws;
  result.stage1Tci1Draws = stats.stage1Tci1Draws;
  result.stage1TciOtherDraws = stats.stage1TciOtherDraws;

  await page.locator("#canvas").screenshot({ path: opts.screenshot });
  result.screenshot = opts.screenshot;

  result.ok =
    (result.gameState === null || result.gameState === 7) &&
    (typeof result.drawCalls !== "number" || result.drawCalls > 0) &&
    (typeof result.texturedDraws !== "number" || result.texturedDraws > 0);
} catch (err) {
  result.error = err?.message || String(err);
}

result.consoleTail = consoleTail.slice(-40);

console.log(JSON.stringify(result, null, 2));
browser.close().catch(() => {});
process.exit(result.ok ? 0 : 1);
