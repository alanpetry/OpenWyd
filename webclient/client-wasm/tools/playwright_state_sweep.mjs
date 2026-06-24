#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { chromium } from 'playwright';
import { chromiumLaunchOptions } from '../../tools/playwright_portable_browser.mjs';

function parseArgs(argv) {
  const out = {
    url: 'http://127.0.0.1:8877/startup_harness.html',
    ticks: 45,
    timeoutMs: 120000,
    states: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
    outJson: 'webclient/client-wasm/build/reports/state-sweep.json',
    screenshotDir: 'webclient/client-wasm/build/reports/state-sweep-shots'
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--url') out.url = argv[++i];
    else if (arg === '--ticks') out.ticks = Number(argv[++i]);
    else if (arg === '--timeout-ms') out.timeoutMs = Number(argv[++i]);
    else if (arg === '--states') out.states = argv[++i].split(',').map((v) => Number(v.trim())).filter((v) => Number.isFinite(v));
    else if (arg === '--out') out.outJson = argv[++i];
    else if (arg === '--screenshot-dir') out.screenshotDir = argv[++i];
  }
  return out;
}

async function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function tickMany(page, count) {
  for (let i = 0; i < count; i += 1) {
    const rc = await page.evaluate(() => window.tickClient(true));
    if (rc < 0) return rc;
  }
  return 1;
}

async function readStats(page) {
  return page.evaluate(() => {
    const M = window.Module || {};
    const call = (name) => (typeof M[name] === 'function' ? M[name]() : null);
    return {
      gameState: call('_wyd_get_game_state'),
      drawCalls: call('_wyd_d3d9_draw_calls'),
      primitives: call('_wyd_d3d9_primitives'),
      shaderDrawsSkipped: call('_wyd_d3d9_shader_draws_skipped'),
      texUploads: call('_wyd_d3d9_tex_uploads'),
      texturedDraws: call('_wyd_d3d9_textured_draws'),
      vsUniqueShaders: call('_wyd_d3d9_vs_unique_shaders'),
      psUniqueShaders: call('_wyd_d3d9_ps_unique_shaders'),
      vsBindCalls: call('_wyd_d3d9_vs_bind_calls'),
      psBindCalls: call('_wyd_d3d9_ps_bind_calls'),
      drawsWithVs: call('_wyd_d3d9_draws_with_vs'),
      drawsWithPs: call('_wyd_d3d9_draws_with_ps'),
      shaderFileOpenAttempts: call('_wyd_d3d9_shader_file_open_attempts'),
      shaderFileOpenSuccess: call('_wyd_d3d9_shader_file_open_success'),
      shaderFileOpenFail: call('_wyd_d3d9_shader_file_open_fail'),
      shaderFileOpenSkinmesh: call('_wyd_d3d9_shader_file_open_skinmesh'),
      shaderFileOpenVsEffect: call('_wyd_d3d9_shader_file_open_vseffect'),
      shaderFileOpenPsEffect: call('_wyd_d3d9_shader_file_open_pseffect'),
      fogEnabledDraws: call('_wyd_d3d9_fog_enabled_draws')
    };
  });
}

async function samplePixel(page) {
  return page.evaluate(() => {
    const canvas = document.getElementById('canvas');
    if (!canvas) return null;
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    if (!ctx) return null;
    const x = Math.floor(canvas.width * 0.5);
    const y = Math.floor(canvas.height * 0.5);
    const px = ctx.getImageData(x, y, 1, 1).data;
    return { x, y, rgba: [px[0], px[1], px[2], px[3]], width: canvas.width, height: canvas.height };
  });
}

async function main() {
  const cfg = parseArgs(process.argv);
  fs.mkdirSync(path.dirname(cfg.outJson), { recursive: true });
  fs.mkdirSync(cfg.screenshotDir, { recursive: true });

  const browser = await chromium.launch(chromiumLaunchOptions({ headless: true }));
  const page = await browser.newPage({ viewport: { width: 1002, height: 874 } });

  const result = {
    ok: false,
    url: cfg.url,
    ticksPerState: cfg.ticks,
    states: cfg.states,
    runs: [],
    error: null
  };

  try {
    await page.goto(cfg.url, { waitUntil: 'load', timeout: cfg.timeoutMs });
    await page.waitForFunction(
      () => window.__runtimeReady === true || /runtime initialized/.test(document.getElementById('log')?.textContent || ''),
      { timeout: cfg.timeoutMs }
    );
    await page.evaluate(() => {
      if (typeof window.stopAutoTick === 'function') window.stopAutoTick();
      if (typeof window.write === 'function') window.write = () => {};
      if (window.Module) {
        Module.print = () => {};
        Module.printErr = () => {};
      }
      const log = document.getElementById('log');
      if (log) log.textContent = '';
    });

    const boot = await page.evaluate(() => Module._wyd_boot_client(0));
    if (!boot) throw new Error('boot failed');

    for (const state of cfg.states) {
      const run = { state, setRc: null, tickRc: null, stats: null, pixel: null, screenshot: null, error: null };
      try {
        await page.evaluate(() => {
          if (typeof window.Module._wyd_d3d9_reset_debug_counters === 'function') {
            window.Module._wyd_d3d9_reset_debug_counters();
          }
        });

        run.setRc = await page.evaluate((s) => window.Module._wyd_set_game_state(s), state);
        await sleep(30);
        run.tickRc = await tickMany(page, cfg.ticks);
        run.stats = await readStats(page);
        run.pixel = await samplePixel(page);
      } catch (err) {
        run.error = String(err?.message || err);
      }

      const shot = path.join(cfg.screenshotDir, `state-${state}.png`);
      await page.screenshot({ path: shot, fullPage: false });
      run.screenshot = shot;
      result.runs.push(run);
    }

    await page.evaluate(() => window.Module._wyd_shutdown_client());
    result.ok = true;
  } catch (err) {
    result.error = String(err?.message || err);
  } finally {
    await browser.close();
  }

  fs.writeFileSync(cfg.outJson, JSON.stringify(result, null, 2));
  console.log(JSON.stringify(result, null, 2));
  process.exit(result.ok ? 0 : 1);
}

main();
