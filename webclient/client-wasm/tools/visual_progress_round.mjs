#!/usr/bin/env node

import fs from "fs";
import path from "path";
import { spawnSync } from "child_process";
import zlib from "zlib";

const REPO_ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname), "../../..");
const DEFAULT_URL = "http://127.0.0.1:8877/webclient/client-wasm/build/link/startup_harness.html";
const DEFAULT_OUT_DIR = path.join(REPO_ROOT, "webclient/client-wasm/build/reports/progress");
const DEFAULT_STABLE_SCREENSHOT = path.join(
  REPO_ROOT,
  "webclient/client-wasm/build/reports/startup-canvas-20260605-stable-t80.png",
);

function parseArgs(argv) {
  const opts = {
    label: "round",
    outDir: DEFAULT_OUT_DIR,
    url: DEFAULT_URL,
    state: 7,
    ticks: 80,
    debugFlags: 0,
    debugSkipFvf: 0,
    trace: false,
    traceDefaultProbes: false,
    traceTop: 24,
    traceProbes: [],
    timeoutMs: 30000,
    references: [],
  };

  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    const next = argv[i + 1];
    if (arg === "--label" && next) {
      opts.label = next;
      i += 1;
    } else if (arg === "--out-dir" && next) {
      opts.outDir = path.resolve(next);
      i += 1;
    } else if (arg === "--url" && next) {
      opts.url = next;
      i += 1;
    } else if (arg === "--state" && next) {
      opts.state = Number.parseInt(next, 10) || opts.state;
      i += 1;
    } else if (arg === "--ticks" && next) {
      opts.ticks = Number.parseInt(next, 10) || opts.ticks;
      i += 1;
    } else if (arg === "--debug-flags" && next) {
      opts.debugFlags = Number.parseInt(next, 10) || 0;
      i += 1;
    } else if (arg === "--debug-skip-fvf" && next) {
      opts.debugSkipFvf = Number.parseInt(next, 10) || 0;
      i += 1;
    } else if (arg === "--trace") {
      opts.trace = true;
    } else if (arg === "--trace-default-probes") {
      opts.trace = true;
      opts.traceDefaultProbes = true;
    } else if (arg === "--trace-top" && next) {
      opts.traceTop = Number.parseInt(next, 10) || opts.traceTop;
      i += 1;
    } else if (arg === "--trace-probe" && next) {
      opts.trace = true;
      opts.traceProbes.push(next);
      i += 1;
    } else if (arg === "--timeout-ms" && next) {
      opts.timeoutMs = Number.parseInt(next, 10) || opts.timeoutMs;
      i += 1;
    } else if (arg === "--reference" && next) {
      const eq = next.indexOf("=");
      if (eq > 0) {
        opts.references.push({
          label: next.slice(0, eq),
          screenshot: path.resolve(next.slice(eq + 1)),
        });
      }
      i += 1;
    }
  }

  return opts;
}

function sanitizeLabel(label) {
  return String(label || "round")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9._-]+/g, "-")
    .replace(/^-+|-+$/g, "")
    .slice(0, 80) || "round";
}

function loadIndex(indexPath) {
  if (!fs.existsSync(indexPath)) return { rounds: [] };
  try {
    const parsed = JSON.parse(fs.readFileSync(indexPath, "utf8"));
    return { rounds: Array.isArray(parsed.rounds) ? parsed.rounds : [] };
  } catch {
    return { rounds: [] };
  }
}

function nextRoundNumber(index, outDir) {
  let maxRound = 0;
  for (const round of index.rounds) {
    if (Number.isFinite(round.round)) maxRound = Math.max(maxRound, round.round);
  }
  if (fs.existsSync(outDir)) {
    for (const entry of fs.readdirSync(outDir, { withFileTypes: true })) {
      if (!entry.isDirectory()) continue;
      const match = /^round-(\d+)/.exec(entry.name);
      if (match) maxRound = Math.max(maxRound, Number.parseInt(match[1], 10));
    }
  }
  return maxRound + 1;
}

function parsePng(buffer) {
  const sig = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  if (!buffer.subarray(0, 8).equals(sig)) throw new Error("invalid PNG signature");

  let pos = 8;
  let width = 0;
  let height = 0;
  let bitDepth = 0;
  let colorType = 0;
  let interlace = 0;
  const idat = [];

  while (pos + 8 <= buffer.length) {
    const length = buffer.readUInt32BE(pos);
    const type = buffer.toString("ascii", pos + 4, pos + 8);
    const dataStart = pos + 8;
    const dataEnd = dataStart + length;
    if (dataEnd + 4 > buffer.length) throw new Error(`truncated PNG chunk ${type}`);
    const data = buffer.subarray(dataStart, dataEnd);
    pos = dataEnd + 4;

    if (type === "IHDR") {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      bitDepth = data[8];
      colorType = data[9];
      interlace = data[12];
    } else if (type === "IDAT") {
      idat.push(data);
    } else if (type === "IEND") {
      break;
    }
  }

  if (bitDepth !== 8) throw new Error(`unsupported PNG bit depth ${bitDepth}`);
  if (interlace !== 0) throw new Error("unsupported interlaced PNG");

  const channelsByType = new Map([
    [0, 1],
    [2, 3],
    [4, 2],
    [6, 4],
  ]);
  const channels = channelsByType.get(colorType);
  if (!channels) throw new Error(`unsupported PNG color type ${colorType}`);

  const inflated = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const raw = Buffer.alloc(width * height * channels);
  let inPos = 0;
  let outPos = 0;

  for (let y = 0; y < height; y += 1) {
    const filter = inflated[inPos++];
    const rowStart = outPos;
    const prevRowStart = rowStart - stride;

    for (let x = 0; x < stride; x += 1) {
      const value = inflated[inPos++];
      const left = x >= channels ? raw[rowStart + x - channels] : 0;
      const up = y > 0 ? raw[prevRowStart + x] : 0;
      const upLeft = y > 0 && x >= channels ? raw[prevRowStart + x - channels] : 0;

      let recon;
      if (filter === 0) {
        recon = value;
      } else if (filter === 1) {
        recon = value + left;
      } else if (filter === 2) {
        recon = value + up;
      } else if (filter === 3) {
        recon = value + Math.floor((left + up) / 2);
      } else if (filter === 4) {
        const p = left + up - upLeft;
        const pa = Math.abs(p - left);
        const pb = Math.abs(p - up);
        const pc = Math.abs(p - upLeft);
        recon = value + (pa <= pb && pa <= pc ? left : pb <= pc ? up : upLeft);
      } else {
        throw new Error(`unsupported PNG filter ${filter}`);
      }
      raw[outPos++] = recon & 0xff;
    }
  }

  const rgba = Buffer.alloc(width * height * 4);
  for (let i = 0, j = 0; i < raw.length; i += channels, j += 4) {
    if (colorType === 6) {
      rgba[j] = raw[i];
      rgba[j + 1] = raw[i + 1];
      rgba[j + 2] = raw[i + 2];
      rgba[j + 3] = raw[i + 3];
    } else if (colorType === 2) {
      rgba[j] = raw[i];
      rgba[j + 1] = raw[i + 1];
      rgba[j + 2] = raw[i + 2];
      rgba[j + 3] = 255;
    } else if (colorType === 0) {
      rgba[j] = raw[i];
      rgba[j + 1] = raw[i];
      rgba[j + 2] = raw[i];
      rgba[j + 3] = 255;
    } else {
      rgba[j] = raw[i];
      rgba[j + 1] = raw[i];
      rgba[j + 2] = raw[i];
      rgba[j + 3] = raw[i + 1];
    }
  }

  return { width, height, rgba };
}

const CRC_TABLE = new Uint32Array(256);
for (let n = 0; n < 256; n += 1) {
  let c = n;
  for (let k = 0; k < 8; k += 1) c = (c & 1) ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  CRC_TABLE[n] = c >>> 0;
}

function crc32(buffer) {
  let c = 0xffffffff;
  for (const byte of buffer) c = CRC_TABLE[(c ^ byte) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function pngChunk(type, data) {
  const typeBuf = Buffer.from(type, "ascii");
  const length = Buffer.alloc(4);
  length.writeUInt32BE(data.length, 0);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(Buffer.concat([typeBuf, data])), 0);
  return Buffer.concat([length, typeBuf, data, crc]);
}

function writePngRgba(filePath, width, height, rgba) {
  const sig = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = 6;
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;

  const scanlines = Buffer.alloc(height * (1 + width * 4));
  for (let y = 0; y < height; y += 1) {
    const dst = y * (1 + width * 4);
    scanlines[dst] = 0;
    rgba.copy(scanlines, dst + 1, y * width * 4, (y + 1) * width * 4);
  }

  fs.writeFileSync(
    filePath,
    Buffer.concat([
      sig,
      pngChunk("IHDR", ihdr),
      pngChunk("IDAT", zlib.deflateSync(scanlines, { level: 6 })),
      pngChunk("IEND", Buffer.alloc(0)),
    ]),
  );
}

function regionDefs(width, height) {
  return [
    { name: "full", x0: 0, y0: 0, x1: width, y1: height },
    {
      name: "sky_candidate",
      x0: Math.floor(width * 0.32),
      y0: 0,
      x1: width,
      y1: Math.floor(height * 0.36),
    },
    {
      name: "water_candidate",
      x0: 0,
      y0: Math.floor(height * 0.36),
      x1: width,
      y1: Math.floor(height * 0.58),
    },
    {
      name: "right_characters",
      x0: Math.floor(width * 0.62),
      y0: Math.floor(height * 0.18),
      x1: width,
      y1: Math.floor(height * 0.88),
    },
  ];
}

function imageStats(image) {
  const stats = {};
  for (const region of regionDefs(image.width, image.height)) {
    let count = 0;
    let brightness = 0;
    let blueBias = 0;
    let dark = 0;
    let bright = 0;
    let nearWhite = 0;
    let redDominant = 0;

    for (let y = region.y0; y < region.y1; y += 1) {
      for (let x = region.x0; x < region.x1; x += 1) {
        const idx = (y * image.width + x) * 4;
        const r = image.rgba[idx];
        const g = image.rgba[idx + 1];
        const b = image.rgba[idx + 2];
        const br = (r + g + b) / 3;
        count += 1;
        brightness += br;
        blueBias += b - (r + g) / 2;
        if (br < 28) dark += 1;
        if (br > 180) bright += 1;
        if (r > 210 && g > 210 && b > 210) nearWhite += 1;
        if (r > 80 && r > g + 35 && r > b + 35) redDominant += 1;
      }
    }

    stats[region.name] = {
      pixels: count,
      brightnessMean: round4(brightness / Math.max(1, count)),
      blueBiasMean: round4(blueBias / Math.max(1, count)),
      darkPct: round4((dark / Math.max(1, count)) * 100),
      brightPct: round4((bright / Math.max(1, count)) * 100),
      nearWhitePct: round4((nearWhite / Math.max(1, count)) * 100),
      redDominantPct: round4((redDominant / Math.max(1, count)) * 100),
    };
  }
  return stats;
}

function round4(value) {
  return Math.round(value * 10000) / 10000;
}

function escapeHtml(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
}

function traceField(text, name) {
  const match = new RegExp(`${name}=([^ ]+)`).exec(text || "");
  return match ? match[1] : "";
}

function traceLabel(text) {
  const marker = " label=";
  const i = String(text || "").indexOf(marker);
  return i >= 0 ? text.slice(i + marker.length) : "";
}

function compactTraceResult(text) {
  if (!text) return "sem hit";
  const label = traceLabel(text);
  const object = /TMObject type=(\d+) pos=([^ ]+)/.exec(label);
  const mesh = /TMMesh file=([^ ]+)/.exec(label);
  const parts = [
    `draw ${traceField(text, "draw") || "?"}`,
    `fvf ${traceField(text, "fvf") || "?"}`,
    traceField(text, "reason") && traceField(text, "reason") !== "-" ? `reason ${traceField(text, "reason")}` : "",
    traceField(text, "stage0") ? `tex ${traceField(text, "stage0")}` : "",
    object ? `obj ${object[1]} @ ${object[2]}` : "",
    mesh ? `mesh ${mesh[1]}` : "",
  ].filter(Boolean);
  return parts.join(" | ");
}

function writeTraceProbeReport(reportPath, smoke, screenshotName) {
  const probes = Array.isArray(smoke?.drawTraceProbes) ? smoke.drawTraceProbes : [];
  const top = Array.isArray(smoke?.drawTraceTop) ? smoke.drawTraceTop : [];
  if (!probes.length && !top.length) return false;

  const markers = probes.map((probe) => {
    const label = `${probe.label || `probe${probe.index}`} (${probe.x},${probe.y})`;
    const summary = compactTraceResult(probe.result || "");
    return [
      `<div class="probe" style="left:${Number(probe.x) || 0}px;top:${Number(probe.y) || 0}px" title="${escapeHtml(summary)}">`,
      `<span>${escapeHtml(probe.index)}</span>`,
      `</div>`,
      `<div class="probe-label" style="left:${(Number(probe.x) || 0) + 12}px;top:${(Number(probe.y) || 0) + 12}px">`,
      `<strong>${escapeHtml(label)}</strong><br>${escapeHtml(summary)}`,
      `</div>`,
    ].join("");
  }).join("\n");

  const probeRows = probes.map((probe) => (
    `<tr><td>${escapeHtml(probe.index)}</td><td>${escapeHtml(probe.label)}</td><td>${escapeHtml(`${probe.x},${probe.y}`)}</td><td>${escapeHtml(compactTraceResult(probe.result || ""))}</td><td><code>${escapeHtml(probe.result || "")}</code></td></tr>`
  )).join("\n");

  const topRows = top.map((entry, index) => (
    `<tr><td>${index}</td><td>${escapeHtml(compactTraceResult(entry))}</td><td><code>${escapeHtml(entry)}</code></td></tr>`
  )).join("\n");

  fs.writeFileSync(reportPath, `<!doctype html>
<html lang="pt-BR">
<meta charset="utf-8">
<title>Graphics trace probes</title>
<style>
  body { margin: 24px; background: #171717; color: #f2efe7; font: 14px/1.45 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
  h1, h2 { font-family: Georgia, serif; font-weight: 700; }
  .stage { position: relative; width: max-content; border: 1px solid #555; box-shadow: 0 20px 60px #0008; }
  .stage img { display: block; }
  .probe { position: absolute; width: 17px; height: 17px; margin: -8px 0 0 -8px; border: 2px solid #ffe45e; border-radius: 999px; background: #0009; box-shadow: 0 0 0 2px #000, 0 0 18px #ffe45e; }
  .probe span { position: absolute; left: 21px; top: -9px; color: #ffe45e; text-shadow: 0 1px 2px #000; font-weight: 700; }
  .probe-label { position: absolute; max-width: 460px; padding: 4px 6px; background: #000b; border: 1px solid #777; color: #fff; pointer-events: none; }
  table { border-collapse: collapse; width: 100%; margin: 16px 0 28px; }
  th, td { border: 1px solid #444; padding: 6px 8px; vertical-align: top; }
  th { background: #252525; text-align: left; }
  code { white-space: pre-wrap; word-break: break-word; color: #d8f3dc; }
</style>
<h1>Graphics Trace</h1>
<p>Arquivo gerado automaticamente para cruzar pontos do screenshot com draw/mesh/textura.</p>
<div class="stage">
  <img src="${escapeHtml(screenshotName)}" alt="canvas">
  ${markers}
</div>
<h2>Probes</h2>
<table>
  <thead><tr><th>#</th><th>label</th><th>x,y</th><th>resumo</th><th>raw</th></tr></thead>
  <tbody>${probeRows}</tbody>
</table>
<h2>Top Suspects</h2>
<table>
  <thead><tr><th>#</th><th>resumo</th><th>raw</th></tr></thead>
  <tbody>${topRows}</tbody>
</table>
</html>
`);
  return true;
}

function compareImages(currentPath, referencePath, outDiffPng) {
  const cur = parsePng(fs.readFileSync(currentPath));
  const ref = parsePng(fs.readFileSync(referencePath));
  const width = Math.min(cur.width, ref.width);
  const height = Math.min(cur.height, ref.height);
  const diffRgba = Buffer.alloc(width * height * 4);

  let changed = 0;
  let sumAbs = 0;
  let sumSq = 0;
  let maxDelta = 0;
  const tolerance = 4;

  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const curIdx = (y * cur.width + x) * 4;
      const refIdx = (y * ref.width + x) * 4;
      const outIdx = (y * width + x) * 4;
      const dr = Math.abs(cur.rgba[curIdx] - ref.rgba[refIdx]);
      const dg = Math.abs(cur.rgba[curIdx + 1] - ref.rgba[refIdx + 1]);
      const db = Math.abs(cur.rgba[curIdx + 2] - ref.rgba[refIdx + 2]);
      const max = Math.max(dr, dg, db);
      const mean = (dr + dg + db) / 3;
      if (max > tolerance) changed += 1;
      sumAbs += mean;
      sumSq += mean * mean;
      maxDelta = Math.max(maxDelta, max);

      diffRgba[outIdx] = max;
      diffRgba[outIdx + 1] = Math.floor(Math.max(0, db - (dr + dg) / 2));
      diffRgba[outIdx + 2] = Math.floor(Math.max(0, dr - (dg + db) / 2));
      diffRgba[outIdx + 3] = 255;
    }
  }

  if (outDiffPng) writePngRgba(outDiffPng, width, height, diffRgba);

  const pixels = width * height;
  return {
    reference: referencePath,
    diffImage: outDiffPng || null,
    width,
    height,
    dimensionMismatch: cur.width !== ref.width || cur.height !== ref.height,
    changedPixels: changed,
    changedPct: round4((changed / Math.max(1, pixels)) * 100),
    meanAbsRgb: round4(sumAbs / Math.max(1, pixels)),
    rmsRgb: round4(Math.sqrt(sumSq / Math.max(1, pixels))),
    maxDelta,
  };
}

function extractSmokeSummary(smoke) {
  const keys = [
    "ok",
    "drawCalls",
    "textureDrawsSky",
    "textureDrawsWater",
    "textureDrawsBright",
    "effectDraws",
    "fvf322EffectDraws",
    "fvf322ScreenlikeReplaySuppressed",
    "stage0ColorOp8Draws",
    "setStage0ColorOp8Calls",
    "setStage0ColorOp4Calls",
    "setStage0ColorOpLastValue",
    "setTextureStage0SkyCalls",
    "setTextureStage1SkyCalls",
    "drawAttemptsWithSkyTexture",
    "drawAttemptsWithSkyTextureIndexed",
    "drawAttemptsWithSkyTextureUp",
    "drawAttemptsWithSkyLastFvf",
    "skyClipDraws",
    "skyClipLastVertexCount",
    "skyClipLastIndexCount",
    "skyClipLastStableWVertices",
    "skyClipLastNegativeWVertices",
    "skyClipLastNearWVertices",
    "skyClipLastInsideVertices",
    "skyClipLastLargeNdcVertices",
    "skyClipLastTriangleCount",
    "skyClipLastTrianglesAllStableW",
    "skyClipLastTrianglesAnyUnstableW",
    "skyClipLastTrianglesAnyOutside",
    "skyClipLastMinW",
    "skyClipLastMaxW",
    "skyClipLastMinNdcX",
    "skyClipLastMaxNdcX",
    "skyClipLastMinNdcY",
    "skyClipLastMaxNdcY",
    "skyClipLastMinNdcZ",
    "skyClipLastMaxNdcZ",
    "skyRenderCalls",
    "skyEligibleCalls",
    "skyMeshDraws",
    "skyLastState",
    "skyLastTextureIndex",
    "skyLastMeshTextureIndex",
    "skyLastMeshHasVB",
    "skyLastMeshHasIB",
    "skyLastMeshFVF",
    "skyLastMeshAttCount",
    "skyLastMeshFaceCount",
    "skyLastMeshVertexCount",
    "skyLastMeshRenderResult",
    "fvf530AutoClipWDraws",
    "fvf530AutoClipWRejectDraws",
    "fvf530Draws",
    "fvf530LargeBoundsDraws",
    "fvf530LargeBoundsSkipDraws",
    "fvf530LargeBoundSamples",
    "fvf530UnstableWDraws",
    "fvf530Vertices",
    "fvf530InsideVertices",
    "fvf530LastVertexCount",
    "fvf530LastIndexCount",
    "fvf530LastStableWVertices",
    "fvf530LastInsideVertices",
    "fvf530LastLargeNdcVertices",
    "fvf530LastMinNdcX",
    "fvf530LastMaxNdcX",
    "fvf530LastMinNdcY",
    "fvf530LastMaxNdcY",
    "fvf530LastMinNdcZ",
    "fvf530LastMaxNdcZ",
    "clipWRejectDraws",
    "drawTraceEnabled",
    "drawTraceProbes",
    "drawTraceTop",
    "glErrorTotal",
    "consoleErrorCount",
    "consoleWarnCount",
  ];
  const out = {};
  for (const key of keys) out[key] = smoke?.[key] ?? null;
  return out;
}

const opts = parseArgs(process.argv);
fs.mkdirSync(opts.outDir, { recursive: true });

const indexPath = path.join(opts.outDir, "index.json");
const index = loadIndex(indexPath);
const round = nextRoundNumber(index, opts.outDir);
const label = sanitizeLabel(opts.label);
const roundDir = path.join(opts.outDir, `round-${String(round).padStart(3, "0")}-${label}`);
fs.mkdirSync(roundDir, { recursive: true });

const screenshotPath = path.join(roundDir, "canvas.png");
const smokePath = path.join(roundDir, "smoke.json");
const visualStatsPath = path.join(roundDir, "visual-stats.json");
const diffSummaryPath = path.join(roundDir, "diff-summary.json");
const traceProbeReportPath = path.join(roundDir, "trace-probes.html");
const traceSummaryPath = path.join(roundDir, "trace-summary.json");

const smokeScript = path.join(REPO_ROOT, "webclient/client-wasm/tools/playwright_startup_smoke.mjs");
const smokeArgs = [
  smokeScript,
  "--url",
  opts.url,
  "--state",
  String(opts.state),
  "--ticks",
  String(opts.ticks),
  "--debug-flags",
  String(opts.debugFlags),
  "--debug-skip-fvf",
  String(opts.debugSkipFvf),
  "--timeout-ms",
  String(opts.timeoutMs),
  "--screenshot",
  screenshotPath,
];
if (opts.trace) smokeArgs.push("--trace");
if (opts.traceDefaultProbes) smokeArgs.push("--trace-default-probes");
if (opts.traceTop) smokeArgs.push("--trace-top", String(opts.traceTop));
for (const probe of opts.traceProbes) smokeArgs.push("--trace-probe", probe);

const startedAt = new Date().toISOString();
const run = spawnSync(process.execPath, smokeArgs, {
  cwd: REPO_ROOT,
  encoding: "utf8",
  maxBuffer: 40 * 1024 * 1024,
});

let smoke = null;
try {
  smoke = JSON.parse(run.stdout || "{}");
} catch (err) {
  smoke = {
    ok: false,
    error: `failed to parse smoke stdout: ${err?.message || String(err)}`,
    stdoutTail: (run.stdout || "").slice(-4000),
    stderrTail: (run.stderr || "").slice(-4000),
  };
}
smoke.exitCode = run.status;
smoke.stderrTail = (run.stderr || "").slice(-4000);
fs.writeFileSync(smokePath, `${JSON.stringify(smoke, null, 2)}\n`);

let traceSummaryError = null;
if (opts.trace) {
  const summaryScript = path.join(REPO_ROOT, "webclient/client-wasm/tools/graphics_trace_summary.mjs");
  const summaryRun = spawnSync(process.execPath, [summaryScript, smokePath], {
    cwd: REPO_ROOT,
    encoding: "utf8",
    maxBuffer: 20 * 1024 * 1024,
  });
  if (summaryRun.status === 0 && summaryRun.stdout) {
    fs.writeFileSync(traceSummaryPath, summaryRun.stdout.endsWith("\n") ? summaryRun.stdout : `${summaryRun.stdout}\n`);
  } else {
    traceSummaryError = {
      exitCode: summaryRun.status,
      stderrTail: (summaryRun.stderr || "").slice(-2000),
      stdoutTail: (summaryRun.stdout || "").slice(-2000),
    };
  }
}

const result = {
  round,
  label,
  startedAt,
  finishedAt: new Date().toISOString(),
  options: {
    url: opts.url,
    state: opts.state,
    ticks: opts.ticks,
    debugFlags: opts.debugFlags,
    debugSkipFvf: opts.debugSkipFvf,
    trace: opts.trace,
    traceDefaultProbes: opts.traceDefaultProbes,
    traceTop: opts.traceTop,
    traceProbes: opts.traceProbes,
    timeoutMs: opts.timeoutMs,
  },
  directory: roundDir,
  screenshot: screenshotPath,
  smoke: smokePath,
  visualStats: null,
  traceProbeReport: null,
  traceSummary: fs.existsSync(traceSummaryPath) ? traceSummaryPath : null,
  traceSummaryError,
  diffs: [],
  smokeSummary: extractSmokeSummary(smoke),
};

if (fs.existsSync(screenshotPath)) {
  const currentImage = parsePng(fs.readFileSync(screenshotPath));
  const stats = imageStats(currentImage);
  fs.writeFileSync(visualStatsPath, `${JSON.stringify(stats, null, 2)}\n`);
  result.visualStats = visualStatsPath;
  result.visualSummary = {
    full: stats.full,
    skyCandidate: stats.sky_candidate,
    waterCandidate: stats.water_candidate,
    rightCharacters: stats.right_characters,
  };

  if (writeTraceProbeReport(traceProbeReportPath, smoke, path.basename(screenshotPath))) {
    result.traceProbeReport = traceProbeReportPath;
  }

  const references = [];
  const previous = index.rounds.at(-1);
  if (previous?.screenshot && fs.existsSync(previous.screenshot)) {
    references.push({ label: "previous", screenshot: previous.screenshot });
  }
  const fifthBack = index.rounds.length >= 5 ? index.rounds[index.rounds.length - 5] : null;
  if (fifthBack?.screenshot && fs.existsSync(fifthBack.screenshot)) {
    references.push({ label: "minus5", screenshot: fifthBack.screenshot });
  }
  if (fs.existsSync(DEFAULT_STABLE_SCREENSHOT)) {
    references.push({ label: "stable-20260605", screenshot: DEFAULT_STABLE_SCREENSHOT });
  }
  references.push(...opts.references.filter((ref) => fs.existsSync(ref.screenshot)));

  const seen = new Set();
  for (const ref of references) {
    const refPath = path.resolve(ref.screenshot);
    if (seen.has(`${ref.label}:${refPath}`)) continue;
    seen.add(`${ref.label}:${refPath}`);
    const diffPng = path.join(roundDir, `diff-${sanitizeLabel(ref.label)}.png`);
    try {
      result.diffs.push({
        label: ref.label,
        ...compareImages(screenshotPath, refPath, diffPng),
      });
    } catch (err) {
      result.diffs.push({
        label: ref.label,
        reference: refPath,
        error: err?.message || String(err),
      });
    }
  }
}

fs.writeFileSync(diffSummaryPath, `${JSON.stringify(result.diffs, null, 2)}\n`);
result.diffSummary = diffSummaryPath;

index.rounds.push(result);
fs.writeFileSync(indexPath, `${JSON.stringify(index, null, 2)}\n`);
fs.appendFileSync(path.join(opts.outDir, "progress-log.jsonl"), `${JSON.stringify(result)}\n`);

const printable = {
  round: result.round,
  label: result.label,
  ok: result.smokeSummary.ok,
  screenshot: result.screenshot,
  smoke: result.smoke,
  visualStats: result.visualStats,
  traceProbeReport: result.traceProbeReport,
  traceSummary: result.traceSummary,
  traceSummaryError: result.traceSummaryError,
  diffSummary: result.diffSummary,
  smokeSummary: result.smokeSummary,
  diffs: result.diffs.map((diff) => ({
    label: diff.label,
    changedPct: diff.changedPct,
    meanAbsRgb: diff.meanAbsRgb,
    rmsRgb: diff.rmsRgb,
    maxDelta: diff.maxDelta,
    error: diff.error,
  })),
  visualSummary: result.visualSummary,
};

console.log(JSON.stringify(printable, null, 2));
process.exit(run.status === 0 ? 0 : 1);
