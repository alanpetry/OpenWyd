import fs from "node:fs";
import path from "node:path";
import { chromium } from "../../webclient/node_modules/playwright/index.mjs";
import { chromiumLaunchOptions } from "../../webclient/tools/playwright_portable_browser.mjs";

const url = process.argv[2] ||
  "http://127.0.0.1:8877/webclient/client-wasm/build/link/startup_harness.html?mode=play&demo=1&autoboot=1&autostart=1";
const output = path.resolve(process.argv[3] || "artifacts/loading-smoke/loading-1365x768.png");
fs.mkdirSync(path.dirname(output), { recursive: true });

const browser = await chromium.launch(chromiumLaunchOptions({ headless: true }));
try {
  const page = await browser.newPage({
    viewport: { width: 1365, height: 768 },
    deviceScaleFactor: 1,
  });
  await page.goto(url, { waitUntil: "domcontentloaded", timeout: 30_000 });
  await page.waitForTimeout(250);
  await page.screenshot({ path: output });
  const state = await page.evaluate(() => {
    const overlay = document.querySelector(".loading-overlay");
    const style = getComputedStyle(overlay);
    const bounds = overlay.getBoundingClientRect();
    return {
      bodyClass: document.body.className,
      overlay: {
        display: style.display,
        width: bounds.width,
        height: bounds.height,
        backgroundSize: style.backgroundSize,
        backgroundPosition: style.backgroundPosition,
      },
    };
  });
  process.stdout.write(`${JSON.stringify({ output, ...state }, null, 2)}\n`);
} finally {
  await browser.close();
}
