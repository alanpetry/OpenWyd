#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { chromium } from 'playwright';
import { chromiumLaunchOptions } from './playwright_portable_browser.mjs';

const rootDir = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..', '..');
const outputDir = path.join(rootDir, 'output', 'playwright');
const baseUrl = process.env.WYD_WEB_URL || 'http://127.0.0.1:8765/';

async function ensureDir(dirPath) {
  await fs.promises.mkdir(dirPath, { recursive: true });
}

function timestampKey() {
  const now = new Date();
  const pad = (value) => String(value).padStart(2, '0');
  return `${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}-${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`;
}

async function main() {
  await ensureDir(outputDir);
  const stamp = timestampKey();
  const beforeShot = path.join(outputDir, `runtime-before-${stamp}.png`);
  const afterShot = path.join(outputDir, `runtime-after-${stamp}.png`);

  const browser = await chromium.launch(chromiumLaunchOptions({ headless: true }));
  const context = await browser.newContext({ viewport: { width: 1600, height: 980 } });
  const page = await context.newPage();

  try {
    await page.goto(baseUrl, { waitUntil: 'networkidle', timeout: 120000 });
    await page.waitForSelector('#status', { timeout: 120000 });
    await page.waitForTimeout(3500);

    const measure = async () => page.evaluate(() => ({
      scrollHeight: document.documentElement.scrollHeight,
      clientHeight: document.documentElement.clientHeight,
      bodyScrollHeight: document.body.scrollHeight,
      bodyClientHeight: document.body.clientHeight,
      viewport: {
        w: window.innerWidth,
        h: window.innerHeight,
      },
      renderMeta: document.getElementById('render-meta')?.textContent || '',
      playerMeta: document.getElementById('player-meta')?.textContent || '',
    }));

    const before = await measure();
    await page.screenshot({ path: beforeShot, fullPage: true });

    await page.selectOption('#toggle-player-mode', 'on');
    await page.selectOption('#player-camera-mode', 'follow');
    await page.selectOption('#toggle-player-click-move', 'on');
    await page.selectOption('#toggle-player-collision', 'on');

    const minimap = page.locator('#field-preview');
    await minimap.click({ position: { x: 200, y: 190 } });
    await page.waitForTimeout(1200);
    await minimap.click({ position: { x: 140, y: 72 }, modifiers: ['Shift'] });
    await page.waitForTimeout(1800);

    const after = await measure();
    await page.screenshot({ path: afterShot, fullPage: true });

    const status = await page.locator('#status').innerText();
    const playerMeta = await page.locator('#player-meta').innerText();
    const objectMeta = await page.locator('#field-object-mesh-meta').innerText();
    const logText = await page.locator('#log').innerText();
    const wasmLines = logText
      .split('\n')
      .filter((line) => line.includes('[WASM'))
      .slice(0, 8);

    const payload = {
      url: baseUrl,
      status,
      playerMeta,
      objectMeta,
      wasmDetected: wasmLines.length > 0,
      wasmLines,
      before,
      after,
      screenshots: [beforeShot, afterShot],
    };
    process.stdout.write(`${JSON.stringify(payload, null, 2)}\n`);
  } finally {
    await browser.close();
  }
}

main().catch((error) => {
  process.stderr.write(`[playwright_runtime_probe] ${error.message}\n`);
  process.exit(1);
});
