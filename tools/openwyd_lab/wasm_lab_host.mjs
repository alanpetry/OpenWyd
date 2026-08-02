import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import process from "node:process";
import { chromium } from "../../webclient/node_modules/playwright/index.mjs";

function argument(name) {
  const index = process.argv.indexOf(name);
  return index >= 0 ? process.argv[index + 1] : "";
}

const repoRoot = path.resolve(argument("--repo-root"));
const controlDir = path.resolve(argument("--control-dir"));
const requestPath = path.join(controlDir, "wasm-request.json");
const responsePath = path.join(controlDir, "wasm-response.json");
const readyPath = path.join(controlDir, "wasm-ready.json");
const traceEnabled = process.env.OPENWYD_LAB_TRACE === "1";
let lastGeneration = 0;
let stopping = false;

function atomicJson(destination, value) {
  const temporary = path.join(
    path.dirname(destination),
    `.${path.basename(destination)}.${process.pid}.tmp`,
  );
  fs.mkdirSync(path.dirname(destination), { recursive: true });
  fs.writeFileSync(temporary, `${JSON.stringify(value, null, 2)}\n`);
  const deadline = Date.now() + 2000;
  for (;;) {
    try {
      fs.renameSync(temporary, destination);
      break;
    } catch (error) {
      if (
        Date.now() >= deadline
        || !["EACCES", "EBUSY", "EPERM"].includes(error?.code)
      ) {
        try { fs.unlinkSync(temporary); } catch {}
        throw error;
      }
      Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 10);
    }
  }
}

const mime = new Map([
  [".html", "text/html; charset=utf-8"],
  [".js", "text/javascript; charset=utf-8"],
  [".wasm", "application/wasm"],
  [".data", "application/octet-stream"],
  [".png", "image/png"],
  [".bin", "application/octet-stream"],
  [".txt", "text/plain; charset=utf-8"],
]);

const server = http.createServer((request, response) => {
  const pathname = decodeURIComponent(new URL(request.url, "http://lab/").pathname);
  const relative = pathname.replace(/^\/+/, "");
  const candidate = path.resolve(repoRoot, relative || "index.html");
  if (!candidate.startsWith(`${repoRoot}${path.sep}`) || !fs.existsSync(candidate)) {
    response.writeHead(404);
    response.end("not found");
    return;
  }
  response.writeHead(200, {
    "Content-Type": mime.get(path.extname(candidate).toLowerCase())
      || "application/octet-stream",
    "Cache-Control": "no-store",
  });
  fs.createReadStream(candidate).pipe(response);
});

await new Promise((resolve, reject) => {
  server.once("error", reject);
  server.listen(0, "127.0.0.1", resolve);
});
const address = server.address();
const browserCandidates = [
  process.env.OPENWYD_LAB_CHROMIUM,
  "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
  "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
].filter(Boolean);
const browserExecutable = browserCandidates.find((candidate) => fs.existsSync(candidate));
const browser = await chromium.launch({
  headless: true,
  ...(browserExecutable ? { executablePath: browserExecutable } : {}),
});
const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
page.on("console", (message) => {
  if (message.type() === "error") {
    process.stdout.write(`[browser:${message.type()}] ${message.text()}\n`);
  }
});
await page.goto(
  `http://127.0.0.1:${address.port}/webclient/client-wasm/build/link/startup_harness.html?autoboot=0&autostart=0`,
  { waitUntil: "load", timeout: 120000 },
);
await page.waitForFunction(
  () => window.Module
    && Module.calledRun === true
    && typeof Module._wyd_boot_client === "function",
  null,
  { timeout: 120000 },
);
const boot = await page.evaluate(() => Module._wyd_boot_client(1));
if (!boot) throw new Error("WASM client boot failed");
atomicJson(readyPath, {
  status: "ready",
  pid: process.pid,
  port: address.port,
});

async function executeShow(request) {
  atomicJson(responsePath, {
    generation: request.generation,
    status: "accepted",
  });
  const scenario = fs.readFileSync(request.scenario);
  const result = await page.evaluate(
    ({ bytes, frame, traceEnabled }) => {
      const data = Uint8Array.from(bytes);
      const pointer = Module._malloc(data.length);
      Module.HEAPU8.set(data, pointer);
      const loaded = Module._wyd_lab_load_scenario(pointer, data.length);
      Module._free(pointer);
      if (!loaded) {
        return {
          status: Module.UTF8ToString(Module._wyd_lab_status()),
          result: Module._wyd_lab_last_result(),
        };
      }
      if (!Module._wyd_lab_show(frame)) {
        return { status: "show-rejected", result: -1 };
      }
      if (traceEnabled) {
        Module._wyd_d3d9_set_detailed_telemetry(1);
        Module._wyd_d3d9_trace_set_enabled(1);
        Module._wyd_d3d9_trace_reset();
      }
      for (let tick = 0; tick < frame + 32; tick += 1) {
        const tickResult = Module._wyd_tick_client();
        if (tickResult < 0) {
          return { status: "tick-failed", result: tickResult };
        }
        if (!Module._wyd_lab_is_pending()) break;
      }
      const canvas = document.getElementById("canvas");
      return {
        status: Module.UTF8ToString(Module._wyd_lab_status()),
        result: Module._wyd_lab_last_result(),
        frame: Module._wyd_lab_current_frame() >>> 0,
        clock_ms: Module._wyd_lab_clock_ms() >>> 0,
        scenario_hash: (Module._wyd_lab_scenario_hash() >>> 0)
          .toString(16).padStart(8, "0"),
        packet_hash: (Module._wyd_lab_packet_hash() >>> 0)
          .toString(16).padStart(8, "0"),
        scene_type: Module._wyd_lab_scene_type(),
        screen_width: Module._wyd_lab_screen_width() >>> 0,
        screen_height: Module._wyd_lab_screen_height() >>> 0,
        player_x: Module._wyd_lab_player_x(),
        player_y: Module._wyd_lab_player_y(),
        player_height: Module._wyd_lab_player_height(),
        player_visible: Module._wyd_lab_player_visible(),
        player_hidden: Module._wyd_lab_player_hidden(),
        player_has_skin: Module._wyd_lab_player_has_skin(),
        player_familiar_item: Module._wyd_lab_player_familiar_item(),
        player_has_familiar: Module._wyd_lab_player_has_familiar(),
        player_familiar_visible: Module._wyd_lab_player_familiar_visible(),
        player_familiar_has_skin: Module._wyd_lab_player_familiar_has_skin(),
        player_familiar_visibility_reason: Module._wyd_lab_player_familiar_visibility_reason(),
        player_class: Module._wyd_lab_player_class(),
        player_motion: Module._wyd_lab_player_motion(),
        player_skin_type: Module._wyd_lab_player_skin_type(),
        player_speed: Module._wyd_lab_player_speed(),
        player_progress: Module._wyd_lab_player_progress(),
        player_moving: Module._wyd_lab_player_moving(),
        player_last_route: Module._wyd_lab_player_last_route(),
        player_max_route: Module._wyd_lab_player_max_route(),
        player_move_started_ms: Module._wyd_lab_player_move_started_ms() >>> 0,
        player_animation_started_ms:
          Module._wyd_lab_player_animation_started_ms() >>> 0,
        player_animation_index: Module._wyd_lab_player_animation_index(),
        player_animation_last_index:
          Module._wyd_lab_player_animation_last_index(),
        player_skin_fps: Module._wyd_lab_player_skin_fps() >>> 0,
        player_skin_offset: Module._wyd_lab_player_skin_offset() >>> 0,
        player_skin_start_offset:
          Module._wyd_lab_player_skin_start_offset() >>> 0,
        player_skin_tick_last: Module._wyd_lab_player_skin_tick_last(),
        player_skin_animation_base:
          Module._wyd_lab_player_skin_animation_base(),
        player_pose_hash: (Module._wyd_lab_player_pose_hash() >>> 0)
          .toString(16).padStart(8, "0"),
        render_fps: Module._wyd_lab_render_fps(),
        camera_x: Module._wyd_lab_camera_x(),
        camera_y: Module._wyd_lab_camera_y(),
        camera_z: Module._wyd_lab_camera_z(),
        camera_horizon: Module._wyd_lab_camera_horizon(),
        camera_vertical: Module._wyd_lab_camera_vertical(),
        camera_length: Module._wyd_lab_camera_length(),
        camera_height: Module._wyd_lab_camera_height(),
        human_draws: Module._wyd_field_visual_human_draws(),
        asset_open_failures:
          Module._wyd_d3d9_asset_file_open_fail() >>> 0,
        asset_open_failure_samples: Array.from(
          { length: Module._wyd_d3d9_asset_file_open_fail_sample_count() >>> 0 },
          (_, index) => Module.UTF8ToString(
            Module._wyd_d3d9_asset_file_open_fail_sample(index),
          ),
        ),
        gl_error_total: Module._wyd_d3d9_gl_error_total() >>> 0,
        trace_top: traceEnabled ? Array.from(
          { length: Module._wyd_d3d9_trace_top_count() >>> 0 },
          (_, index) => Module.UTF8ToString(Module._wyd_d3d9_trace_top_sample(index)),
        ) : [],
        png: canvas.toDataURL("image/png"),
      };
    },
    { bytes: [...scenario], frame: request.frame >>> 0, traceEnabled },
  );
  if (result.png) {
    const encoded = result.png.replace(/^data:image\/png;base64,/, "");
    fs.mkdirSync(path.dirname(request.capture), { recursive: true });
    fs.writeFileSync(request.capture, Buffer.from(encoded, "base64"));
    delete result.png;
  }
  atomicJson(responsePath, {
    generation: request.generation,
    ...result,
    capture: request.capture,
  });
}

async function poll() {
  if (stopping) return;
  try {
    const request = JSON.parse(fs.readFileSync(requestPath, "utf8"));
    if (request.generation > lastGeneration) {
      lastGeneration = request.generation;
      if (request.command === "quit") {
        stopping = true;
        atomicJson(responsePath, {
          generation: request.generation,
          status: "quitting",
        });
        await browser.close();
        await new Promise((resolve) => server.close(resolve));
        process.exit(0);
      } else if (request.command === "show") {
        await executeShow(request);
      }
    }
  } catch (error) {
    if (error?.code !== "ENOENT" && !(error instanceof SyntaxError)) {
      atomicJson(responsePath, {
        generation: lastGeneration,
        status: "host-error",
        error: String(error?.stack || error),
      });
    }
  }
  setTimeout(poll, 25);
}

poll();
