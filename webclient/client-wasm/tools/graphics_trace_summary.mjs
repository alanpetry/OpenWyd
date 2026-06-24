#!/usr/bin/env node

import fs from "fs";
import path from "path";

function usage() {
  console.error("usage: node webclient/client-wasm/tools/graphics_trace_summary.mjs <smoke.json>");
  process.exit(2);
}

const smokePath = process.argv[2] ? path.resolve(process.argv[2]) : null;
if (!smokePath || !fs.existsSync(smokePath)) usage();

const smoke = JSON.parse(fs.readFileSync(smokePath, "utf8"));

function field(text, name) {
  const re = new RegExp(`${name}=([^ ]+)`);
  const match = re.exec(text || "");
  return match ? match[1] : "";
}

function labelField(text) {
  const marker = " label=";
  const i = String(text || "").indexOf(marker);
  return i >= 0 ? text.slice(i + marker.length) : "";
}

function parseEntry(text) {
  const label = labelField(text);
  const objectMatch = /TMObject type=(\d+) pos=([^ ]+)/.exec(label);
  const meshMatch = /TMMesh file=([^ ]+)/.exec(label);
  return {
    raw: text,
    score: Number.parseFloat(field(text, "score")) || null,
    draw: Number.parseInt(field(text, "draw"), 10) || null,
    fvf: Number.parseInt(field(text, "fvf"), 10) || null,
    coverage: Number.parseFloat(field(text, "coverage")) || null,
    bbox: field(text, "bbox"),
    skipped: field(text, "skipped") === "1",
    reason: field(text, "reason"),
    stage0: field(text, "stage0"),
    stage1: field(text, "stage1"),
    objectType: objectMatch ? Number.parseInt(objectMatch[1], 10) : null,
    objectPos: objectMatch ? objectMatch[2] : "",
    mesh: meshMatch ? meshMatch[1] : "",
    label,
  };
}

const probes = Array.isArray(smoke.drawTraceProbes)
  ? smoke.drawTraceProbes.map((probe) => ({
      index: probe.index,
      label: probe.label,
      x: probe.x,
      y: probe.y,
      draw: probe.draw,
      hit: parseEntry(probe.result || ""),
    }))
  : [];

const top = Array.isArray(smoke.drawTraceTop)
  ? smoke.drawTraceTop.map(parseEntry)
  : [];

const groups = new Map();
for (const entry of top) {
  const key = [
    entry.objectType ?? "no-object",
    entry.mesh || "no-mesh",
    entry.stage0 || "no-stage0",
    entry.reason || "-",
  ].join("|");
  const group = groups.get(key) || {
    key,
    objectType: entry.objectType,
    mesh: entry.mesh,
    stage0: entry.stage0,
    reason: entry.reason,
    count: 0,
    skipped: 0,
    maxScore: 0,
    maxCoverage: 0,
    examples: [],
  };
  group.count += 1;
  if (entry.skipped) group.skipped += 1;
  group.maxScore = Math.max(group.maxScore, entry.score || 0);
  group.maxCoverage = Math.max(group.maxCoverage, entry.coverage || 0);
  if (group.examples.length < 3) group.examples.push(entry.raw);
  groups.set(key, group);
}

const summary = {
  source: smokePath,
  generatedAt: new Date().toISOString(),
  ok: smoke.ok === true,
  drawCalls: smoke.drawCalls ?? null,
  traceEnabled: smoke.drawTraceEnabled === true,
  probeCount: probes.length,
  probeHitCount: probes.filter((probe) => probe.draw > 0 && probe.hit.raw).length,
  topCount: top.length,
  groupCount: groups.size,
  probes,
  probeHits: probes.map((probe) => ({
    label: probe.label,
    x: probe.x,
    y: probe.y,
    draw: probe.draw,
    fvf: probe.hit.fvf,
    stage0: probe.hit.stage0,
    stage1: probe.hit.stage1,
    objectType: probe.hit.objectType,
    objectPos: probe.hit.objectPos,
    mesh: probe.hit.mesh,
    reason: probe.hit.reason,
  })),
  top: top.slice(0, 12),
  groups: [...groups.values()].sort((a, b) => {
    if (b.maxScore !== a.maxScore) return b.maxScore - a.maxScore;
    return b.count - a.count;
  }),
};

console.log(JSON.stringify(summary, null, 2));
