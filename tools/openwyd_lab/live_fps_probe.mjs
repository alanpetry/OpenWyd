import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import process from "node:process";
import { chromium } from "../../webclient/node_modules/playwright/index.mjs";

const repoRoot = path.resolve(process.argv[2] || ".");
const server = http.createServer((request, response) => {
  const relative = decodeURIComponent(
    new URL(request.url, "http://probe/").pathname,
  ).replace(/^\/+/, "");
  const candidate = path.resolve(repoRoot, relative);
  if (
    !candidate.startsWith(`${repoRoot}${path.sep}`)
    || !fs.existsSync(candidate)
  ) {
    response.writeHead(404);
    response.end();
    return;
  }
  const contentTypes = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".wasm": "application/wasm",
    ".data": "application/octet-stream",
    ".png": "image/png",
    ".bin": "application/octet-stream",
    ".txt": "text/plain; charset=utf-8",
  };
  response.writeHead(200, {
    "Content-Type": contentTypes[path.extname(candidate)] || "application/octet-stream",
    "Cache-Control": "no-store",
  });
  fs.createReadStream(candidate).pipe(response);
});

await new Promise((resolve, reject) => {
  server.once("error", reject);
  server.listen(0, "127.0.0.1", resolve);
});

const edgeCandidates = [
  "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
  "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
];
const executablePath = edgeCandidates.find((candidate) => fs.existsSync(candidate));
const browser = await chromium.launch({
  headless: true,
  ...(executablePath ? { executablePath } : {}),
});

try {
  const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
  const port = server.address().port;
  await page.goto(
    `http://127.0.0.1:${port}/webclient/client-wasm/build/link/startup_harness.html?autoboot=0&autostart=0`,
    { waitUntil: "load", timeout: 120000 },
  );
  await page.waitForFunction(
    () => window.Module
      && Module.calledRun === true
      && typeof Module._wyd_boot_client === "function",
    null,
    { timeout: 120000 },
  );
  const booted = await page.evaluate(() => {
    const result = Module._wyd_boot_client(1);
    if (result) window.startAutoTick();
    return result;
  });
  if (!booted) throw new Error("WASM client boot failed");

  const samples = [];
  for (let index = 0; index < 24; index += 1) {
    await new Promise((resolve) => setTimeout(resolve, 250));
    samples.push(await page.evaluate(() => ({
      wall_ms: Math.round(performance.now()),
      client_ms: Module._wyd_debug_get_time() >>> 0,
      render_fps: Module._wyd_lab_render_fps(),
      game_state: Module._wyd_get_game_state?.(),
    })));
  }
  process.stdout.write(`${JSON.stringify(samples, null, 2)}\n`);
} finally {
  await browser.close();
  await new Promise((resolve) => server.close(resolve));
}
