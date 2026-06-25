#!/usr/bin/env node
"use strict";

const fs = require("fs");
const http = require("http");
const path = require("path");
const { chromium } = require("playwright");

function parseArgs(argv) {
  const args = {
    repoRoot: ".",
    startupJs: "webclient/client-wasm/build/link/tmproject_startup.js",
    outDir: "webclient/client-wasm/build/reports/select-server-snapshot",
    width: 1280,
    height: 720,
    frames: 180,
    frameMs: 16.6667,
    timeoutMs: 60000,
  };

  for (let i = 2; i < argv.length; i += 1) {
    const key = argv[i];
    const value = argv[i + 1];
    if (!key.startsWith("--")) {
      throw new Error(`Unexpected argument: ${key}`);
    }
    if (value === undefined || value.startsWith("--")) {
      throw new Error(`Missing value for ${key}`);
    }
    i += 1;

    switch (key) {
      case "--repo-root":
        args.repoRoot = value;
        break;
      case "--startup-js":
        args.startupJs = value;
        break;
      case "--out-dir":
        args.outDir = value;
        break;
      case "--width":
        args.width = Number.parseInt(value, 10);
        break;
      case "--height":
        args.height = Number.parseInt(value, 10);
        break;
      case "--frames":
        args.frames = Number.parseInt(value, 10);
        break;
      case "--frame-ms":
        args.frameMs = Number.parseFloat(value);
        break;
      case "--timeout-ms":
        args.timeoutMs = Number.parseInt(value, 10);
        break;
      default:
        throw new Error(`Unknown argument: ${key}`);
    }
  }

  for (const numericKey of ["width", "height", "frames", "frameMs", "timeoutMs"]) {
    if (!Number.isFinite(args[numericKey]) || args[numericKey] <= 0) {
      throw new Error(`Invalid numeric value for ${numericKey}: ${args[numericKey]}`);
    }
  }

  return args;
}

function toUrlPath(repoRoot, filePath) {
  const rel = path.relative(repoRoot, filePath).split(path.sep).join("/");
  return `/${rel}`;
}

function contentType(filePath) {
  switch (path.extname(filePath).toLowerCase()) {
    case ".html":
      return "text/html; charset=utf-8";
    case ".js":
      return "application/javascript; charset=utf-8";
    case ".wasm":
      return "application/wasm";
    case ".data":
      return "application/octet-stream";
    case ".png":
      return "image/png";
    case ".jpg":
    case ".jpeg":
      return "image/jpeg";
    case ".json":
      return "application/json; charset=utf-8";
    default:
      return "application/octet-stream";
  }
}

function startStaticServer(repoRoot) {
  const server = http.createServer((req, res) => {
    const parsed = new URL(req.url, "http://127.0.0.1");
    const decoded = decodeURIComponent(parsed.pathname);
    const normalized = path.normalize(decoded).replace(/^(\.\.[/\\])+/, "");
    const filePath = path.join(repoRoot, normalized);

    if (!filePath.startsWith(repoRoot)) {
      res.writeHead(403);
      res.end("forbidden");
      return;
    }

    fs.readFile(filePath, (err, data) => {
      if (err) {
        res.writeHead(404);
        res.end("not found");
        return;
      }
      res.writeHead(200, { "Content-Type": contentType(filePath) });
      res.end(data);
    });
  });

  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const address = server.address();
      resolve({ server, origin: `http://127.0.0.1:${address.port}` });
    });
  });
}

function writeHarness(outDir, startupUrlPath, width, height) {
  fs.mkdirSync(outDir, { recursive: true });
  const harnessPath = path.join(outDir, "select-server-snapshot-harness.html");
  const html = `<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>OpenWyd select-server snapshot</title>
  <style>
    html, body { margin: 0; width: 100%; height: 100%; background: #000; overflow: hidden; }
    canvas { display: block; width: ${width}px; height: ${height}px; image-rendering: auto; }
  </style>
</head>
<body>
  <canvas id="canvas" width="${width}" height="${height}"></canvas>
  <script>
    var Module = {
      canvas: document.getElementById("canvas"),
      print: (...args) => console.log("[wasm]", ...args),
      printErr: (...args) => console.warn("[wasm]", ...args),
      onRuntimeInitialized: () => { window.__wydRuntimeReady = true; }
    };
  </script>
  <script src="${startupUrlPath}"></script>
</body>
</html>
`;
  fs.writeFileSync(harnessPath, html, "utf8");
  return harnessPath;
}

async function callOptional(page, name, args = []) {
  return page.evaluate(
    ({ name, args }) => {
      const fn = Module && Module[`_${name}`];
      if (typeof fn !== "function") {
        return { present: false };
      }
      try {
        return { present: true, value: fn(...args) };
      } catch (error) {
        return { present: true, error: String(error && error.message ? error.message : error) };
      }
    },
    { name, args },
  );
}

async function readTelemetry(page) {
  const scalarNames = [
    "wyd_get_game_state",
    "wyd_d3d9_draw_calls",
    "wyd_d3d9_primitives",
    "wyd_d3d9_gl_error_total",
    "wyd_d3d9_depth_write_guard_forced_draws",
    "wyd_d3d9_shader_draws_skipped",
    "wyd_d3d9_skin_suspicious_texture_draws",
    "wyd_d3d9_fog_enabled",
    "wyd_selserver_human_count",
    "wyd_debug_camera_valid",
    "wyd_debug_camera_x",
    "wyd_debug_camera_y",
    "wyd_debug_camera_z",
    "wyd_debug_camera_h",
    "wyd_debug_camera_v",
  ];

  const telemetry = {};
  for (const name of scalarNames) {
    const result = await callOptional(page, name);
    if (result.present && !result.error) {
      telemetry[name] = result.value;
    }
  }

  const humanCount = Number.isFinite(telemetry.wyd_selserver_human_count)
    ? Math.max(0, Math.min(16, telemetry.wyd_selserver_human_count))
    : 0;
  telemetry.humans = [];
  for (let i = 0; i < humanCount; i += 1) {
    const human = { index: i };
    for (const name of [
      "wyd_selserver_human_present",
      "wyd_selserver_human_visible",
      "wyd_selserver_human_pos_x",
      "wyd_selserver_human_pos_y",
      "wyd_selserver_human_height",
      "wyd_selserver_human_mount",
      "wyd_selserver_human_motion",
      "wyd_selserver_human_sent_motion",
      "wyd_selserver_human_skin_ani",
      "wyd_selserver_human_demo_ani",
      "wyd_selserver_human_weapon_type_index",
      "wyd_selserver_human_delta_x",
      "wyd_selserver_human_delta_y",
    ]) {
      const result = await callOptional(page, name, [i]);
      if (result.present && !result.error) {
        human[name.replace("wyd_selserver_human_", "")] = result.value;
      }
    }
    telemetry.humans.push(human);
  }

  return telemetry;
}

async function main() {
  const args = parseArgs(process.argv);
  const repoRoot = path.resolve(args.repoRoot);
  const startupJs = path.resolve(repoRoot, args.startupJs);
  const outDir = path.resolve(repoRoot, args.outDir);

  if (!fs.existsSync(startupJs)) {
    throw new Error(`Startup artifact not found: ${startupJs}`);
  }

  const harnessPath = writeHarness(outDir, toUrlPath(repoRoot, startupJs), args.width, args.height);
  const { server, origin } = await startStaticServer(repoRoot);
  const browser = await chromium.launch({ headless: true });
  const consoleLines = [];

  try {
    const page = await browser.newPage({ viewport: { width: args.width, height: args.height } });
    page.on("console", (msg) => consoleLines.push({ type: msg.type(), text: msg.text() }));
    page.on("pageerror", (err) => consoleLines.push({ type: "pageerror", text: String(err) }));

    await page.goto(`${origin}${toUrlPath(repoRoot, harnessPath)}`, {
      waitUntil: "load",
      timeout: args.timeoutMs,
    });
    await page.waitForFunction(
      () => window.__wydRuntimeReady === true || (window.Module && Module.calledRun === true),
      null,
      { timeout: args.timeoutMs },
    );

    await callOptional(page, "wyd_boot_client");
    for (let frame = 0; frame < args.frames; frame += 1) {
      await callOptional(page, "wyd_tick_client", [args.frameMs]);
      await page.waitForTimeout(0);
    }

    const screenshotPath = path.join(outDir, "select-server-1280x720.png");
    const telemetryPath = path.join(outDir, "select-server-telemetry.json");
    await page.locator("canvas").screenshot({ path: screenshotPath });

    const telemetry = await readTelemetry(page);
    telemetry.capture = {
      width: args.width,
      height: args.height,
      frames: args.frames,
      frameMs: args.frameMs,
      startupJs: path.relative(repoRoot, startupJs).split(path.sep).join("/"),
      screenshot: path.relative(repoRoot, screenshotPath).split(path.sep).join("/"),
    };
    telemetry.console = consoleLines.slice(-200);
    fs.writeFileSync(telemetryPath, `${JSON.stringify(telemetry, null, 2)}\n`, "utf8");

    console.log(`[select-server-snapshot] screenshot=${path.relative(repoRoot, screenshotPath)}`);
    console.log(`[select-server-snapshot] telemetry=${path.relative(repoRoot, telemetryPath)}`);
  } finally {
    await browser.close();
    await new Promise((resolve) => server.close(resolve));
  }
}

main().catch((error) => {
  console.error(`[select-server-snapshot] ${error.stack || error.message || error}`);
  process.exit(1);
});
