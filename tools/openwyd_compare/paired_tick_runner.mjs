#!/usr/bin/env node

/*
 * Drives one source-built native OPENWYD_COMPARE client and one WASM client
 * through the same monotonically numbered frames and controlled millisecond
 * clock. The native process is supplied by the outer controller; this runner
 * never discovers or falls back to another game executable.
 */

import { Buffer } from "node:buffer";
import { randomUUID } from "node:crypto";
import { createRequire } from "node:module";
import net from "node:net";
import {
  lstat,
  mkdir,
  readFile,
  readdir,
  stat,
  writeFile,
} from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";

const CAPTURE_WIDTH = 800;
const CAPTURE_HEIGHT = 600;
const INPUT_PROTOCOL_VERSION = 1;
const MAX_FRAME_ID = (1n << 64n) - 1n;
const MAX_TIME_MS = 0xffff_ffff;
const MAX_RANDOM_SEED = 0xffff_ffff;
const DEFAULT_MAX_WASM_PUMPS = 4096;

const WM_KEYDOWN = 0x0100;
const WM_KEYUP = 0x0101;
const WM_CHAR = 0x0102;
const WM_MOUSEMOVE = 0x0200;
const WM_LBUTTONDOWN = 0x0201;
const WM_LBUTTONUP = 0x0202;
const WM_RBUTTONDOWN = 0x0204;
const WM_RBUTTONUP = 0x0205;
const MK_LBUTTON = 0x0001;
const MK_RBUTTON = 0x0002;

const CP1252_SPECIAL = new Map([
  [0x20ac, 0x80],
  [0x201a, 0x82],
  [0x0192, 0x83],
  [0x201e, 0x84],
  [0x2026, 0x85],
  [0x2020, 0x86],
  [0x2021, 0x87],
  [0x02c6, 0x88],
  [0x2030, 0x89],
  [0x0160, 0x8a],
  [0x2039, 0x8b],
  [0x0152, 0x8c],
  [0x017d, 0x8e],
  [0x2018, 0x91],
  [0x2019, 0x92],
  [0x201c, 0x93],
  [0x201d, 0x94],
  [0x2022, 0x95],
  [0x2013, 0x96],
  [0x2014, 0x97],
  [0x02dc, 0x98],
  [0x2122, 0x99],
  [0x0161, 0x9a],
  [0x203a, 0x9b],
  [0x0153, 0x9c],
  [0x017e, 0x9e],
  [0x0178, 0x9f],
]);

function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

function unsignedInteger(value, maximum, name) {
  if (!Number.isSafeInteger(value) || value < 0 || value > maximum) {
    throw new Error(`${name} must be an integer in range 0..${maximum}`);
  }
  return value;
}

export function parseRandomSeed(value) {
  if (
    (typeof value !== "string" && typeof value !== "number") ||
    !/^[0-9]+$/u.test(String(value))
  ) {
    throw new Error("random seed must be an unsigned decimal integer");
  }
  const parsed = BigInt(value);
  if (parsed > BigInt(MAX_RANDOM_SEED)) {
    throw new Error(`random seed must be in range 0..${MAX_RANDOM_SEED}`);
  }
  return Number(parsed);
}

export function nativeRandomSeedCommand(seed) {
  return `RANDOM_SEED ${parseRandomSeed(seed)}`;
}

export function unsignedFrameId(value, name = "frame id") {
  let parsed;
  if (typeof value === "bigint") {
    parsed = value;
  } else if (typeof value === "number") {
    if (!Number.isSafeInteger(value) || value < 0) {
      throw new Error(`${name} must be an unsigned 64-bit integer`);
    }
    parsed = BigInt(value);
  } else if (typeof value === "string") {
    if (!/^(0|[1-9][0-9]*)$/u.test(value)) {
      throw new Error(`${name} must be an unsigned decimal 64-bit integer`);
    }
    parsed = BigInt(value);
  } else {
    throw new Error(`${name} must be an unsigned 64-bit integer`);
  }
  if (parsed < 0n || parsed > MAX_FRAME_ID) {
    throw new Error(`${name} must be in range 0..${MAX_FRAME_ID}`);
  }
  return parsed;
}

export function encodeCp1252(value, name = "text") {
  if (typeof value !== "string") {
    throw new Error(`${name} must be a string`);
  }

  const encoded = [];
  let characterIndex = 0;
  for (const character of Array.from(value)) {
    const codePoint = character.codePointAt(0);
    let byte = null;
    if (
      (codePoint >= 0x01 && codePoint <= 0x7f) ||
      (codePoint >= 0xa0 && codePoint <= 0xff)
    ) {
      byte = codePoint;
    } else {
      byte = CP1252_SPECIAL.get(codePoint) ?? null;
    }
    if (byte === null) {
      const printable = `U+${codePoint.toString(16).toUpperCase().padStart(4, "0")}`;
      throw new Error(
        `${name} character ${characterIndex} (${printable}) is not representable in CP1252`,
      );
    }
    encoded.push(byte);
    characterIndex += 1;
  }
  return encoded;
}

function pairedText(action, commonName, nativeName, wasmName, actionIndex) {
  const hasCommon = Object.hasOwn(action, commonName);
  const hasNative = Object.hasOwn(action, nativeName);
  const hasWasm = Object.hasOwn(action, wasmName);
  if (hasCommon && (hasNative || hasWasm)) {
    throw new Error(
      `action ${actionIndex} must use either ${commonName} or the native/WASM pair`,
    );
  }
  if (hasCommon) {
    if (typeof action[commonName] !== "string") {
      throw new Error(`action ${actionIndex} ${commonName} must be a string`);
    }
    return {
      native: action[commonName],
      wasm: action[commonName],
    };
  }
  if (!hasNative || !hasWasm) {
    throw new Error(
      `action ${actionIndex} requires ${commonName}, or both ${nativeName} and ${wasmName}`,
    );
  }
  if (
    typeof action[nativeName] !== "string" ||
    typeof action[wasmName] !== "string"
  ) {
    throw new Error(
      `action ${actionIndex} ${nativeName} and ${wasmName} must be strings`,
    );
  }
  return {
    native: action[nativeName],
    wasm: action[wasmName],
  };
}

function boundedActionInteger(value, maximum, name) {
  if (!Number.isSafeInteger(value) || value < 0 || value > maximum) {
    throw new Error(`${name} must be an integer in range 0..${maximum}`);
  }
  return value;
}

/*
 * Compile a versioned JSON action document into two low-level streams. Mouse
 * actions are shared. Text can intentionally differ (for example CMPNATIVE
 * versus CMPWASM), but both strings are encoded strictly as CP1252 bytes.
 * Actions are ordered by frame and retain their source order within a frame.
 */
export function parseActionSchedule(document) {
  if (!document || typeof document !== "object" || Array.isArray(document)) {
    throw new Error("actions JSON must be an object");
  }
  if (
    Object.hasOwn(document, "schema") &&
    document.schema !== "openwyd.paired-input-actions"
  ) {
    throw new Error("unsupported actions JSON schema");
  }
  if (document.schema_version !== 1) {
    throw new Error(
      `unsupported actions JSON version ${JSON.stringify(document.schema_version)}`,
    );
  }
  if (!Array.isArray(document.actions)) {
    throw new Error("actions JSON must contain an actions array");
  }

  const normalized = document.actions.map((action, sourceIndex) => {
    if (!action || typeof action !== "object" || Array.isArray(action)) {
      throw new Error(`action ${sourceIndex} must be an object`);
    }
    const frameId = unsignedFrameId(
      action.frame_id,
      `action ${sourceIndex} frame_id`,
    );
    if (typeof action.type !== "string" || action.type.length === 0) {
      throw new Error(`action ${sourceIndex} type must be a non-empty string`);
    }
    return {
      action,
      frameId,
      sourceIndex,
      type: action.type.toLowerCase(),
    };
  });
  normalized.sort((left, right) => {
    if (left.frameId < right.frameId) return -1;
    if (left.frameId > right.frameId) return 1;
    return left.sourceIndex - right.sourceIndex;
  });

  const byFrame = new Map();
  let mouseMask = 0;
  const keysDown = new Set();

  for (const entry of normalized) {
    const { action, frameId, sourceIndex, type } = entry;
    const frameKey = frameId.toString(10);
    let bucket = byFrame.get(frameKey);
    if (!bucket) {
      bucket = {
        frameId,
        actionTypes: [],
        nativeInputs: [],
        wasmInputs: [],
      };
      byFrame.set(frameKey, bucket);
    }

    if (type === "mouse_move") {
      const x = boundedActionInteger(
        action.x,
        CAPTURE_WIDTH - 1,
        `action ${sourceIndex} x`,
      );
      const y = boundedActionInteger(
        action.y,
        CAPTURE_HEIGHT - 1,
        `action ${sourceIndex} y`,
      );
      bucket.nativeInputs.push({ type, x, y });
      bucket.wasmInputs.push({
        channel: "mouse",
        message: WM_MOUSEMOVE,
        wParam: mouseMask,
        x,
        y,
      });
      bucket.actionTypes.push(type);
      continue;
    }

    if (type === "mouse_down" || type === "mouse_up") {
      const x = boundedActionInteger(
        action.x,
        CAPTURE_WIDTH - 1,
        `action ${sourceIndex} x`,
      );
      const y = boundedActionInteger(
        action.y,
        CAPTURE_HEIGHT - 1,
        `action ${sourceIndex} y`,
      );
      if (
        typeof action.button !== "string" ||
        !["LEFT", "RIGHT"].includes(action.button.toUpperCase())
      ) {
        throw new Error(`action ${sourceIndex} button must be LEFT or RIGHT`);
      }
      const button = action.button.toUpperCase();
      const buttonMask = button === "LEFT" ? MK_LBUTTON : MK_RBUTTON;
      const down = type === "mouse_down";
      const alreadyDown = (mouseMask & buttonMask) !== 0;
      if (down === alreadyDown) {
        throw new Error(
          `action ${sourceIndex} ${button} is ${
            down ? "already down" : "not down"
          }`,
        );
      }
      mouseMask = down
        ? mouseMask | buttonMask
        : mouseMask & ~buttonMask;
      const message =
        button === "LEFT"
          ? down
            ? WM_LBUTTONDOWN
            : WM_LBUTTONUP
          : down
            ? WM_RBUTTONDOWN
            : WM_RBUTTONUP;
      bucket.nativeInputs.push({ type, button, x, y });
      bucket.wasmInputs.push({
        channel: "mouse",
        message,
        wParam: mouseMask,
        x,
        y,
      });
      bucket.actionTypes.push(`${type}:${button}`);
      continue;
    }

    if (type === "key_down" || type === "key_up") {
      const key = boundedActionInteger(
        action.key,
        254,
        `action ${sourceIndex} key`,
      );
      if (key === 0) {
        throw new Error(`action ${sourceIndex} key must be in range 1..254`);
      }
      const down = type === "key_down";
      if (down === keysDown.has(key)) {
        throw new Error(
          `action ${sourceIndex} key ${key} is ${
            down ? "already down" : "not down"
          }`,
        );
      }
      if (down) keysDown.add(key);
      else keysDown.delete(key);
      bucket.nativeInputs.push({ type, key });
      bucket.wasmInputs.push({
        channel: "key",
        message: down ? WM_KEYDOWN : WM_KEYUP,
        wParam: key,
        lParam: down ? 1 : 0xc0000001,
      });
      bucket.actionTypes.push(`${type}:${key}`);
      continue;
    }

    if (type === "char" || type === "text") {
      const fields =
        type === "char"
          ? pairedText(
              action,
              "char",
              "native_char",
              "wasm_char",
              sourceIndex,
            )
          : pairedText(
              action,
              "text",
              "native_text",
              "wasm_text",
              sourceIndex,
            );
      if (
        type === "char" &&
        (Array.from(fields.native).length !== 1 ||
          Array.from(fields.wasm).length !== 1)
      ) {
        throw new Error(
          `action ${sourceIndex} char values must each contain exactly one character`,
        );
      }
      const nativeBytes = encodeCp1252(
        fields.native,
        `action ${sourceIndex} native ${type}`,
      );
      const wasmBytes = encodeCp1252(
        fields.wasm,
        `action ${sourceIndex} WASM ${type}`,
      );
      for (const byte of nativeBytes) {
        bucket.nativeInputs.push({ type: "char", byte });
      }
      for (const byte of wasmBytes) {
        bucket.wasmInputs.push({
          channel: "key",
          message: WM_CHAR,
          wParam: byte,
          lParam: 1,
        });
      }
      bucket.actionTypes.push(
        `${type}:native=${nativeBytes.length}:wasm=${wasmBytes.length}`,
      );
      continue;
    }

    throw new Error(`action ${sourceIndex} has unsupported type ${action.type}`);
  }

  return {
    schema: "openwyd.paired-input-actions",
    schemaVersion: 1,
    actionCount: normalized.length,
    byFrame,
  };
}

export function validateActionScheduleRange(schedule, firstFrame, lastFrame) {
  for (const bucket of schedule.byFrame.values()) {
    if (bucket.frameId < firstFrame || bucket.frameId > lastFrame) {
      throw new Error(
        `action frame ${bucket.frameId} is outside run range ${firstFrame}..${lastFrame}`,
      );
    }
  }
}

export function nativeInputCommand(frameId, input) {
  const normalizedFrameId = unsignedFrameId(frameId, "input frame id");
  const prefix =
    `INPUT ${INPUT_PROTOCOL_VERSION} ${normalizedFrameId.toString(10)}`;
  if (input.type === "mouse_move") {
    return `${prefix} MOUSE_MOVE ${input.x} ${input.y}`;
  }
  if (input.type === "mouse_down" || input.type === "mouse_up") {
    return `${prefix} ${input.type.toUpperCase()} ${input.button} ${input.x} ${input.y}`;
  }
  if (input.type === "key_down" || input.type === "key_up") {
    return `${prefix} ${input.type.toUpperCase()} ${input.key}`;
  }
  if (input.type === "char") {
    return `${prefix} CHAR ${input.byte}`;
  }
  throw new Error(`cannot encode native input type ${input.type}`);
}

/*
 * This is deliberately self-contained because Playwright serializes it into
 * the page. The low-level records were already CP1252-encoded and validated
 * by parseActionSchedule().
 */
export function applyWasmInputs(inputs) {
  const module = globalThis.Module;
  if (!module) throw new Error("WASM Module is unavailable");
  const results = [];
  for (const input of inputs) {
    if (input.channel === "mouse") {
      if (typeof module._wyd_mouse_event !== "function") {
        throw new Error("WASM mouse-event export is unavailable");
      }
      results.push(
        module._wyd_mouse_event(
          input.message >>> 0,
          input.wParam >>> 0,
          input.x | 0,
          input.y | 0,
          0,
        ),
      );
      continue;
    }
    if (input.channel === "key") {
      if (typeof module._wyd_key_event !== "function") {
        throw new Error("WASM key-event export is unavailable");
      }
      results.push(
        module._wyd_key_event(
          input.message >>> 0,
          input.wParam >>> 0,
          input.lParam >>> 0,
        ),
      );
      continue;
    }
    throw new Error(`unknown WASM input channel ${input.channel}`);
  }
  return results;
}

export function normalizePipePath(value) {
  if (typeof value !== "string" || value.length === 0) {
    throw new Error("native pipe name must not be empty");
  }
  if (value.includes("\r") || value.includes("\n")) {
    throw new Error("native pipe name must not contain line breaks");
  }
  return value.startsWith("\\\\.\\pipe\\") ? value : `\\\\.\\pipe\\${value}`;
}

export function parseNativeEvent(line) {
  if (typeof line !== "string") {
    throw new Error("native event must be text");
  }
  const fields = line.trim().split(/\s+/u);
  const kind = fields[0] ?? "";
  const decimal = (token, maximum, name) => {
    if (!/^(0|[1-9][0-9]*)$/u.test(token ?? "")) {
      throw new Error(`${name} is not an unsigned decimal integer`);
    }
    const parsed = BigInt(token);
    if (parsed > maximum) {
      throw new Error(`${name} is out of range`);
    }
    return parsed;
  };

  if (kind === "READY" && fields.length === 6) {
    const version = decimal(fields[1], 0xffffn, "protocol version");
    if (version !== 1n) {
      throw new Error(`unsupported native protocol version ${version}`);
    }
    return {
      kind,
      version: Number(version),
      pid: Number(decimal(fields[2], 0xffff_ffffn, "process id")),
      width: Number(decimal(fields[3], 0xffff_ffffn, "width")),
      height: Number(decimal(fields[4], 0xffff_ffffn, "height")),
      captureEnabled: Number(decimal(fields[5], 1n, "capture enabled")) === 1,
    };
  }

  if (kind === "STEP_ACCEPTED" && fields.length === 3) {
    return {
      kind,
      frameId: decimal(fields[1], MAX_FRAME_ID, "frame id"),
      timeMs: Number(decimal(fields[2], BigInt(MAX_TIME_MS), "time ms")),
    };
  }

  if (kind === "PRESENT" && fields.length === 5) {
    if (!/^0x[0-9A-Fa-f]{8}$/u.test(fields[3])) {
      throw new Error("capture HRESULT must be 0x plus eight hex digits");
    }
    return {
      kind,
      frameId: decimal(fields[1], MAX_FRAME_ID, "frame id"),
      timeMs: Number(decimal(fields[2], BigInt(MAX_TIME_MS), "time ms")),
      captureHresult: Number.parseInt(fields[3].slice(2), 16) >>> 0,
      snapshotWritten:
        Number(decimal(fields[4], 1n, "snapshot written")) === 1,
    };
  }

  if (kind === "INPUT_QUEUED" && fields.length === 3) {
    return {
      kind,
      inputSequence: decimal(
        fields[1],
        MAX_FRAME_ID,
        "input sequence",
      ),
      frameId: decimal(fields[2], MAX_FRAME_ID, "frame id"),
    };
  }

  if (kind === "RANDOM_SEEDED" && fields.length === 2) {
    return {
      kind,
      seed: Number(
        decimal(fields[1], BigInt(MAX_RANDOM_SEED), "random seed"),
      ),
    };
  }

  if (kind === "ERROR" && fields.length === 2) {
    return { kind, code: fields[1] };
  }
  if (["PONG", "CLOSING", "BYE"].includes(kind) && fields.length === 1) {
    return { kind };
  }
  throw new Error(`malformed or unknown native event: ${JSON.stringify(line)}`);
}

class NativeLineChannel {
  constructor(socket) {
    this.socket = socket;
    this.buffer = "";
    this.events = [];
    this.waiters = [];
    this.failure = null;

    socket.setEncoding("ascii");
    socket.on("data", (chunk) => {
      this.buffer += chunk;
      for (;;) {
        const newline = this.buffer.indexOf("\n");
        if (newline < 0) break;
        const line = this.buffer.slice(0, newline);
        this.buffer = this.buffer.slice(newline + 1);
        try {
          this.push(parseNativeEvent(line));
        } catch (error) {
          this.fail(error);
          return;
        }
      }
    });
    socket.on("error", (error) => this.fail(error));
    socket.on("close", () => {
      if (!this.failure) this.fail(new Error("native pipe closed"));
    });
  }

  push(event) {
    const waiter = this.waiters.shift();
    if (waiter) waiter.resolve(event);
    else this.events.push(event);
  }

  fail(error) {
    if (this.failure) return;
    this.failure = error instanceof Error ? error : new Error(String(error));
    for (const waiter of this.waiters.splice(0)) waiter.reject(this.failure);
  }

  send(line) {
    if (this.failure) throw this.failure;
    this.socket.write(`${line}\n`, "ascii");
  }

  async next(timeoutMs) {
    if (this.events.length > 0) return this.events.shift();
    if (this.failure) throw this.failure;
    return new Promise((resolve, reject) => {
      const waiter = { resolve, reject };
      this.waiters.push(waiter);
      const timer = setTimeout(() => {
        const index = this.waiters.indexOf(waiter);
        if (index >= 0) this.waiters.splice(index, 1);
        reject(new Error(`native pipe event timed out after ${timeoutMs} ms`));
      }, timeoutMs);
      waiter.resolve = (value) => {
        clearTimeout(timer);
        resolve(value);
      };
      waiter.reject = (error) => {
        clearTimeout(timer);
        reject(error);
      };
    });
  }

  async expect(kind, timeoutMs) {
    const event = await this.next(timeoutMs);
    if (event.kind === "ERROR") {
      throw new Error(`native bridge rejected command: ${event.code}`);
    }
    if (event.kind !== kind) {
      throw new Error(`expected native ${kind}, received ${event.kind}`);
    }
    return event;
  }

  close() {
    this.socket.end();
    this.socket.destroy();
  }
}

async function connectNativePipe(pipePath, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let lastError = null;
  while (Date.now() < deadline) {
    try {
      const socket = await new Promise((resolve, reject) => {
        const candidate = net.createConnection(pipePath);
        candidate.once("connect", () => resolve(candidate));
        candidate.once("error", reject);
      });
      return new NativeLineChannel(socket);
    } catch (error) {
      lastError = error;
      await delay(50);
    }
  }
  throw new Error(
    `could not connect to native pipe ${pipePath}: ${lastError?.message ?? "timeout"}`,
  );
}

function callExport(module, name, ...args) {
  const callable = module?.[name];
  return typeof callable === "function" ? callable(...args) : null;
}

/*
 * Serialized directly into the Playwright page. The deterministic RNG must be
 * armed before any client initialization can consume rand()/srand().
 */
export function prepareAndBootWasmClient({ randomSeed, startTimeMs }) {
  globalThis.stopAutoTick?.();
  const module = globalThis.Module;
  if (!module || typeof module._wyd_boot_client !== "function") {
    throw new Error("WASM client boot export is unavailable");
  }
  if (typeof module._wyd_debug_set_fake_time !== "function") {
    throw new Error("WASM fake-time export is unavailable");
  }
  if (typeof module._wyd_debug_get_time !== "function") {
    throw new Error("WASM fake-time observation export is unavailable");
  }

  let random = null;
  if (randomSeed !== null) {
    if (
      !Number.isInteger(randomSeed) ||
      randomSeed < 0 ||
      randomSeed > 0xffff_ffff
    ) {
      throw new Error("WASM random seed is outside uint32 range");
    }
    if (
      typeof module._wyd_compare_random_arm !== "function" ||
      typeof module._wyd_compare_random_is_armed !== "function" ||
      typeof module._wyd_compare_random_configured_seed !== "function"
    ) {
      throw new Error("WASM deterministic random exports are unavailable");
    }

    random = {
      armResult: module._wyd_compare_random_arm(randomSeed >>> 0),
      armed: module._wyd_compare_random_is_armed(),
      configuredSeed:
        module._wyd_compare_random_configured_seed() >>> 0,
    };
    if (
      random.armResult !== 1 ||
      random.armed !== 1 ||
      random.configuredSeed !== randomSeed
    ) {
      throw new Error("WASM random seed did not arm exactly as requested");
    }
  }

  const expectedStartTimeMs = startTimeMs >>> 0;
  module._wyd_debug_set_fake_time(expectedStartTimeMs);
  const observedStartTime = module._wyd_debug_get_time();
  if (
    !Number.isInteger(observedStartTime) ||
    (observedStartTime >>> 0) !== expectedStartTimeMs
  ) {
    throw new Error(
      `WASM fake time ${observedStartTime} does not match requested boot time ${expectedStartTimeMs}`,
    );
  }
  const bootResult = module._wyd_boot_client(0);
  const observedPostBootTime = module._wyd_debug_get_time();
  if (
    !Number.isInteger(observedPostBootTime) ||
    (observedPostBootTime >>> 0) !== expectedStartTimeMs
  ) {
    throw new Error(
      `WASM fake time ${observedPostBootTime} changed during boot; expected ${expectedStartTimeMs}`,
    );
  }
  return {
    bootResult,
    random,
  };
}

export function createBrowserErrorMonitor(page) {
  if (!page || typeof page.on !== "function") {
    throw new Error("Playwright page does not support event monitoring");
  }

  const failures = [];
  page.on("pageerror", (error) => {
    failures.push({
      kind: "pageerror",
      text:
        error instanceof Error
          ? error.stack ?? error.message
          : String(error),
    });
  });
  page.on("console", (message) => {
    if (message.type() !== "error") return;
    const location =
      typeof message.location === "function" ? message.location() : null;
    failures.push({
      kind: "console.error",
      text: message.text(),
      location,
    });
  });

  return {
    failures,
    assertClean(stage) {
      if (failures.length === 0) return;
      const details = failures
        .map((failure) => `[${failure.kind}] ${failure.text}`)
        .join("\n");
      throw new Error(
        `browser emitted ${failures.length} fatal error(s) during ${stage}:\n${details}`,
      );
    },
  };
}

/*
 * One Playwright binding is installed for the lifetime of the page. Each
 * frame arms it with a fresh capability token. The browser can release only
 * that exact frame/time pair, and only once; the callback's synchronous
 * native.send() is the common release event for both clients.
 */
export function createPairedReleaseBarrier({ native }) {
  if (
    !native ||
    typeof native.send !== "function" ||
    typeof native.expect !== "function"
  ) {
    throw new Error("paired release barrier requires a native channel");
  }

  const bindingName =
    `__openwydReleasePairedFrame_${randomUUID().replaceAll("-", "")}`;
  let pending = null;

  const callback = (_source, request) => {
    if (pending === null) {
      throw new Error("paired release binding was called without an armed frame");
    }
    if (pending.used) {
      throw new Error(
        `paired release token for frame ${pending.frameIdText} was already used`,
      );
    }
    if (!request || typeof request !== "object" || Array.isArray(request)) {
      throw new Error("paired release request must be an object");
    }
    if (request.token !== pending.token) {
      throw new Error("paired release token does not match the armed frame");
    }
    if (request.frameId !== pending.frameIdText) {
      throw new Error("paired release frame does not match the armed frame");
    }
    if (request.timeMs !== pending.timeMs) {
      throw new Error("paired release time does not match the armed frame");
    }

    pending.used = true;
    pending.nativeReleasedAtMs = Date.now();
    native.send(`STEP ${pending.frameIdText} ${pending.timeMs}`);
    return {
      frameId: pending.frameIdText,
      timeMs: pending.timeMs,
      token: pending.token,
    };
  };

  return Object.freeze({
    bindingName,
    callback,
    arm(frameId, timeMs) {
      if (pending !== null) {
        throw new Error(
          `paired release barrier is still armed for frame ${pending.frameIdText}`,
        );
      }
      const normalizedFrameId = unsignedFrameId(
        frameId,
        "paired release frame id",
      );
      const normalizedTimeMs = unsignedInteger(
        timeMs,
        MAX_TIME_MS,
        "paired release time",
      );
      const token = randomUUID();
      pending = {
        frameId: normalizedFrameId,
        frameIdText: normalizedFrameId.toString(10),
        nativeReleasedAtMs: null,
        timeMs: normalizedTimeMs,
        token,
        used: false,
      };
      return Object.freeze({
        bindingName,
        frameId: pending.frameIdText,
        timeMs: pending.timeMs,
        token,
      });
    },
    finish(token) {
      if (pending === null || pending.token !== token) {
        throw new Error("paired release completion token is not armed");
      }
      if (!pending.used || pending.nativeReleasedAtMs === null) {
        throw new Error(
          `paired release binding was not used for frame ${pending.frameIdText}`,
        );
      }
      const completed = {
        nativeReleasedAtMs: pending.nativeReleasedAtMs,
      };
      pending = null;
      return completed;
    },
    cancel(token) {
      if (pending !== null && pending.token === token) {
        pending = null;
        return true;
      }
      return false;
    },
  });
}

/*
 * This function is exported for a fake-Module unit test and is also serialized
 * directly into the Playwright page. Capturing synchronously in the same task
 * that called _wyd_tick_client observes the WebGL backing store before the
 * browser can composite/discard a preserveDrawingBuffer=false frame.
 */
export async function executeWasmTick({
  frameId,
  maxPumps = 4096,
  selector,
  timeMs,
  wasmInputs = [],
  release = null,
}) {
  const expectedWidth = 800;
  const expectedHeight = 600;
  if (!Number.isInteger(maxPumps) || maxPumps < 1 || maxPumps > 1_000_000) {
    throw new Error("maxPumps must be an integer in range 1..1000000");
  }
  const module = globalThis.Module;
  if (!module || typeof module._wyd_tick_client !== "function") {
    throw new Error("WASM client tick export is unavailable");
  }
  if (typeof module._wyd_debug_set_fake_time !== "function") {
    throw new Error("WASM fake-time export is unavailable");
  }
  if (typeof module._wyd_debug_get_time !== "function") {
    throw new Error("WASM fake-time observation export is unavailable");
  }
  if (typeof module._wyd_d3d9_present_calls !== "function") {
    throw new Error("WASM Present counter export is unavailable");
  }
  const presentStateExports = [
    "_wyd_compare_present_state_sequence",
    "_wyd_compare_present_game_state_valid",
    "_wyd_compare_present_game_state",
    "_wyd_compare_present_scene_type_valid",
    "_wyd_compare_present_scene_type",
  ];
  const missingPresentStateExport = presentStateExports.find(
    (name) => typeof module[name] !== "function",
  );
  if (missingPresentStateExport) {
    throw new Error(
      `WASM pre-Present state latch export ${missingPresentStateExport} is unavailable`,
    );
  }
  const compare3DStateExports = [
    "_wyd_compare_3d_state_sequence",
    "_wyd_compare_3d_state_valid",
    "_wyd_compare_3d_state_frame_serial",
    "_wyd_compare_3d_state_draw_serial",
    "_wyd_compare_3d_state_matrix_value",
  ];
  const missingCompare3DStateExport = compare3DStateExports.find(
    (name) => typeof module[name] !== "function",
  );
  if (missingCompare3DStateExport) {
    throw new Error(
      `WASM pre-UI 3D state latch export ${missingCompare3DStateExport} is unavailable`,
    );
  }
  const canvas = globalThis.document?.querySelector(selector);
  if (
    !canvas ||
    typeof canvas.toDataURL !== "function" ||
    canvas.width !== expectedWidth ||
    canvas.height !== expectedHeight
  ) {
    throw new Error("WASM backing canvas must be exactly 800x600");
  }

  const invoke = (name, ...args) =>
    typeof module[name] === "function" ? module[name](...args) : null;
  const readPointer = (name) => {
    const pointer = invoke(name);
    if (!pointer || typeof module.UTF8ToString !== "function") return null;
    return module.UTF8ToString(pointer);
  };
  const readUint32 = (name, ...args) => {
    const value = invoke(name, ...args);
    return value === null ? null : value >>> 0;
  };

  const expectedTimeMs = timeMs >>> 0;
  module._wyd_debug_set_fake_time(expectedTimeMs);
  const observedTimeBeforeInputs = module._wyd_debug_get_time();
  if (
    !Number.isInteger(observedTimeBeforeInputs) ||
    (observedTimeBeforeInputs >>> 0) !== expectedTimeMs
  ) {
    throw new Error(
      `WASM fake time ${observedTimeBeforeInputs} does not match requested frame time ${expectedTimeMs}`,
    );
  }
  if (!Array.isArray(wasmInputs)) {
    throw new Error("wasmInputs must be an array");
  }
  const wasmInputResults = [];
  for (const input of wasmInputs) {
    if (input?.channel === "mouse") {
      if (typeof module._wyd_mouse_event !== "function") {
        throw new Error("WASM mouse-event export is unavailable");
      }
      wasmInputResults.push(
        module._wyd_mouse_event(
          input.message >>> 0,
          input.wParam >>> 0,
          input.x | 0,
          input.y | 0,
          0,
        ),
      );
      continue;
    }
    if (input?.channel === "key") {
      if (typeof module._wyd_key_event !== "function") {
        throw new Error("WASM key-event export is unavailable");
      }
      wasmInputResults.push(
        module._wyd_key_event(
          input.message >>> 0,
          input.wParam >>> 0,
          input.lParam >>> 0,
        ),
      );
      continue;
    }
    throw new Error(`unknown WASM input channel ${input?.channel}`);
  }
  const presentBefore = module._wyd_d3d9_present_calls() >>> 0;
  const presentStateSequenceBefore =
    module._wyd_compare_present_state_sequence() >>> 0;
  const compare3DStateSequenceBefore =
    module._wyd_compare_3d_state_sequence() >>> 0;

  let releaseAcknowledgment = null;
  if (release !== null) {
    if (!release || typeof release !== "object" || Array.isArray(release)) {
      throw new Error("WASM paired release descriptor must be an object");
    }
    const frameIdText = String(frameId);
    if (
      typeof release.bindingName !== "string" ||
      release.bindingName.length === 0 ||
      typeof release.token !== "string" ||
      release.token.length === 0 ||
      release.frameId !== frameIdText ||
      release.timeMs !== expectedTimeMs
    ) {
      throw new Error(
        "WASM paired release descriptor does not match frame/time/token",
      );
    }
    const releaseBinding = globalThis[release.bindingName];
    if (typeof releaseBinding !== "function") {
      throw new Error("WASM paired release binding is unavailable");
    }
    releaseAcknowledgment = await releaseBinding({
      frameId: frameIdText,
      timeMs: expectedTimeMs,
      token: release.token,
    });
    if (
      !releaseAcknowledgment ||
      typeof releaseAcknowledgment !== "object" ||
      releaseAcknowledgment.frameId !== frameIdText ||
      releaseAcknowledgment.timeMs !== expectedTimeMs ||
      releaseAcknowledgment.token !== release.token
    ) {
      throw new Error(
        "WASM paired release acknowledgment does not match frame/time/token",
      );
    }
  }

  let presentAfter = presentBefore;
  let presentDelta = 0;
  let pumpCount = 0;
  let tickResult = null;
  while (pumpCount < maxPumps && presentDelta === 0) {
    tickResult = module._wyd_tick_client();
    pumpCount += 1;
    if (!Number.isInteger(tickResult) || tickResult < 0) {
      throw new Error(
        `WASM pump ${pumpCount} failed with result ${tickResult}`,
      );
    }
    presentAfter = module._wyd_d3d9_present_calls() >>> 0;
    presentDelta = (presentAfter - presentBefore) >>> 0;
    if (presentDelta > 1) {
      throw new Error(
        `WASM frame produced ${presentDelta} Present calls during ${pumpCount} pumps; expected exactly 1`,
      );
    }
  }
  if (presentDelta !== 1) {
    throw new Error(
      `WASM frame did not produce a Present within ${maxPumps} pumps`,
    );
  }
  const observedTimeAfterPresent = module._wyd_debug_get_time();
  if (
    !Number.isInteger(observedTimeAfterPresent) ||
    (observedTimeAfterPresent >>> 0) !== expectedTimeMs
  ) {
    throw new Error(
      `WASM fake time ${observedTimeAfterPresent} changed during frame; expected ${expectedTimeMs}`,
    );
  }

  const presentStateSequenceAfter =
    module._wyd_compare_present_state_sequence() >>> 0;
  const presentStateSequenceDelta =
    (presentStateSequenceAfter - presentStateSequenceBefore) >>> 0;
  if (presentStateSequenceDelta !== 1) {
    throw new Error(
      `WASM frame latched pre-Present state ${presentStateSequenceDelta} times; expected exactly 1`,
    );
  }
  const presentedGameState =
    module._wyd_compare_present_game_state_valid() === 1
      ? module._wyd_compare_present_game_state()
      : null;
  const presentedSceneType =
    module._wyd_compare_present_scene_type_valid() === 1
      ? module._wyd_compare_present_scene_type()
      : null;
  const compare3DStateSequenceAfter =
    module._wyd_compare_3d_state_sequence() >>> 0;
  const compare3DStateSequenceDelta =
    (compare3DStateSequenceAfter - compare3DStateSequenceBefore) >>> 0;
  const compare3DStateValidRaw = module._wyd_compare_3d_state_valid();
  if (
    compare3DStateValidRaw !== 0 &&
    compare3DStateValidRaw !== 1
  ) {
    throw new Error(
      `WASM pre-UI 3D state valid flag must be 0 or 1, received ${compare3DStateValidRaw}`,
    );
  }
  const compare3DStateValid = compare3DStateValidRaw === 1;
  if (
    (compare3DStateValid && compare3DStateSequenceDelta !== 1) ||
    (!compare3DStateValid && compare3DStateSequenceDelta !== 0)
  ) {
    throw new Error(
      `WASM frame latched pre-UI 3D state ${compare3DStateSequenceDelta} times with valid=${compare3DStateValidRaw}; expected exactly ${compare3DStateValid ? 1 : 0}`,
    );
  }

  let compare3DMatrices = {
    world: null,
    view: null,
    projection: null,
  };
  if (compare3DStateValid) {
    const values = Array.from(
      { length: 48 },
      (_, index) => module._wyd_compare_3d_state_matrix_value(index),
    );
    if (values.some((value) => !Number.isFinite(value))) {
      throw new Error("WASM pre-UI 3D matrices contain a non-finite value");
    }
    compare3DMatrices = {
      world: values.slice(0, 16),
      view: values.slice(16, 32),
      projection: values.slice(32, 48),
    };
  }

  const glErrorTotal = invoke("_wyd_d3d9_gl_error_total");
  if (!Number.isInteger(glErrorTotal)) {
    throw new Error(
      `WASM gl_error_total must be an integer, received ${glErrorTotal}`,
    );
  }
  if (glErrorTotal !== 0) {
    throw new Error(
      `WASM frame has gl_error_total=${glErrorTotal}; expected exactly 0`,
    );
  }

  const visibleHumanCapturedRaw = invoke(
    "_wyd_field_visible_human_count",
  );
  const visibleHumanTotalRaw = invoke("_wyd_field_visible_human_total");
  const visibleHumanLimitRaw = invoke("_wyd_field_visible_human_limit");
  const visibleHumanLimit =
    Number.isInteger(visibleHumanLimitRaw) &&
    visibleHumanLimitRaw >= 0 &&
    visibleHumanLimitRaw <= 1024
      ? visibleHumanLimitRaw
      : null;
  const visibleHumanCaptured =
    Number.isInteger(visibleHumanCapturedRaw) &&
    visibleHumanCapturedRaw >= 0 &&
    visibleHumanCapturedRaw <= (visibleHumanLimit ?? 1024)
      ? visibleHumanCapturedRaw
      : 0;
  const visibleHumans = [];
  for (let index = 0; index < visibleHumanCaptured; index += 1) {
    visibleHumans.push({
      id: readUint32("_wyd_field_visible_human_id", index),
      x: invoke("_wyd_field_visible_human_x", index),
      y: invoke("_wyd_field_visible_human_y", index),
      hp: invoke("_wyd_field_visible_human_hp", index),
      max_hp: invoke("_wyd_field_visible_human_max_hp", index),
      motion: invoke("_wyd_field_visible_human_motion", index),
      class_id: invoke("_wyd_field_visible_human_class_id", index),
      title_progress_visible: invoke(
        "_wyd_field_visible_human_title_progress_visible",
        index,
      ),
    });
  }

  const dataUrl = canvas.toDataURL("image/png");
  if (
    typeof dataUrl !== "string" ||
    !dataUrl.startsWith("data:image/png;base64,")
  ) {
    throw new Error("WASM canvas did not produce a PNG data URL");
  }

  return {
    dataUrl,
    inputResults: wasmInputResults,
    snapshot: {
      schema: "openwyd.debug-frame",
      schema_version: 1,
      frame_id: frameId,
      state: {
        game: presentedGameState,
        scene: presentedSceneType,
      },
      ticks: {
        compare_frame: frameId,
        wasm_pump_count: pumpCount,
        wasm_pump_limit: maxPumps,
        wasm_tick_result: tickResult,
      },
      clock: {
        controlled_time_ms: expectedTimeMs,
        wasm_time_ms: observedTimeAfterPresent >>> 0,
      },
      camera: {
        valid: invoke("_wyd_debug_camera_valid"),
        standalone: invoke("_wyd_debug_camera_standalone"),
        x: invoke("_wyd_debug_camera_x"),
        y: invoke("_wyd_debug_camera_y"),
        z: invoke("_wyd_debug_camera_z"),
        horizon_angle: invoke("_wyd_debug_camera_h"),
        vertical_angle: invoke("_wyd_debug_camera_v"),
      },
      matrices: compare3DMatrices,
      draws: [],
      render: {
        capture_point: "synchronous_after_wyd_tick_client",
        backing_width: canvas.width,
        backing_height: canvas.height,
        present_before: presentBefore,
        present_after: presentAfter,
        draw_calls: invoke("_wyd_d3d9_draw_calls"),
        primitives: invoke("_wyd_d3d9_primitives"),
        begin_scene_calls: invoke("_wyd_d3d9_begin_scene_calls"),
        end_scene_calls: invoke("_wyd_d3d9_end_scene_calls"),
        gl_error_total: glErrorTotal,
        three_d_state: {
          capture_point: "before_SetMatrixForUI",
          attempted: compare3DStateSequenceDelta !== 0,
          valid: compare3DStateValid,
          sequence: compare3DStateSequenceAfter,
          frame_serial: frameId,
          source_frame_serial: readUint32(
            "_wyd_compare_3d_state_frame_serial",
          ),
          draw_serial: compare3DStateValid
            ? readUint32("_wyd_compare_3d_state_draw_serial")
            : null,
          draw_serial_available: compare3DStateValid,
        },
      },
      network: {
        host: readPointer("_wyd_socket_last_host"),
        proxy_url: readPointer("_wyd_socket_last_proxy_url"),
        port: invoke("_wyd_socket_last_port"),
        connect_result: invoke("_wyd_socket_last_connect_result"),
        last_error: invoke("_wyd_socket_last_error"),
        bytes_sent: invoke("_wyd_socket_bytes_sent"),
        bytes_received: invoke("_wyd_socket_bytes_received"),
        last_sent_opcode: invoke("_wyd_socket_last_sent_opcode"),
        last_received_opcode: invoke("_wyd_socket_last_recv_opcode"),
      },
      extensions: {
        wasm: {
          direct_state_navigation: false,
          exact_present_delta: 1,
          inputs_before_tick: wasmInputs.length,
          input_results: wasmInputResults,
          release_barrier:
            release === null
              ? null
              : {
                  frame_id: releaseAcknowledgment.frameId,
                  released: true,
                  time_ms: releaseAcknowledgment.timeMs,
                },
          run_tick_pumps: pumpCount,
          png_encoding: "canvas.toDataURL",
          present_state_latch: {
            capture_point: "after_EndScene_before_Present",
            sequence_before: presentStateSequenceBefore,
            sequence_after: presentStateSequenceAfter,
            sequence_delta: presentStateSequenceDelta,
          },
          three_d_state_latch: {
            capture_point: "before_SetMatrixForUI",
            sequence_before: compare3DStateSequenceBefore,
            sequence_after: compare3DStateSequenceAfter,
            sequence_delta: compare3DStateSequenceDelta,
          },
          input_observation: {
            mouse: {
              x: invoke("_wyd_input_mouse_x"),
              y: invoke("_wyd_input_mouse_y"),
              left_down: invoke("_wyd_input_mouse_left_down"),
              right_down: invoke("_wyd_input_mouse_right_down"),
              middle_down: invoke("_wyd_input_mouse_middle_down"),
              event_count: invoke("_wyd_input_mouse_event_count"),
              last_message: invoke("_wyd_input_mouse_last_msg"),
              last_wparam: invoke("_wyd_input_mouse_last_wparam"),
            },
            control: {
              id: invoke("_wyd_control_last_mouse_processed_id"),
              flags: invoke("_wyd_control_last_mouse_processed_flags"),
              type: invoke("_wyd_control_last_mouse_processed_type"),
              local_x: invoke("_wyd_control_last_mouse_processed_x"),
              local_y: invoke("_wyd_control_last_mouse_processed_y"),
            },
          },
          field_observation: {
            mode: invoke("_wyd_get_field_mode"),
            debug_fixture_used: invoke("_wyd_field_debug_fixture_used"),
            initialized: invoke("_wyd_field_initialized"),
            has_ground: invoke("_wyd_field_has_ground"),
            has_my_human: invoke("_wyd_field_has_my_human"),
            critical_error: invoke("_wyd_field_critical_error"),
            map: {
              x: invoke("_wyd_field_map_x"),
              y: invoke("_wyd_field_map_y"),
            },
            player: {
              id: readUint32("_wyd_field_myhuman_id"),
              name: readPointer("_wyd_field_myhuman_name"),
              hp: invoke("_wyd_field_myhuman_hp"),
              max_hp: invoke("_wyd_field_myhuman_max_hp"),
              class_id: invoke("_wyd_field_myhuman_class_id"),
              attack_dest_id: readUint32(
                "_wyd_field_myhuman_attack_dest_id",
              ),
              title_progress_visible: invoke(
                "_wyd_field_myhuman_title_progress_visible",
              ),
              x: invoke("_wyd_field_myhuman_x"),
              y: invoke("_wyd_field_myhuman_y"),
              motion: invoke("_wyd_field_myhuman_motion"),
              sent_motion: invoke("_wyd_field_myhuman_sent_motion"),
              moving: invoke("_wyd_field_myhuman_moving"),
              progress_rate: invoke("_wyd_field_myhuman_progress_rate"),
              last_route_index: invoke(
                "_wyd_field_myhuman_last_route_index",
              ),
              max_route_index: invoke("_wyd_field_myhuman_max_route_index"),
              target_x: invoke("_wyd_field_myhuman_target_x"),
              target_y: invoke("_wyd_field_myhuman_target_y"),
              move_to_x: invoke("_wyd_field_myhuman_move_to_x"),
              move_to_y: invoke("_wyd_field_myhuman_move_to_y"),
              height: invoke("_wyd_field_myhuman_height"),
              want_height: invoke("_wyd_field_myhuman_want_height"),
              ground_height: invoke("_wyd_field_ground_height_under_player"),
              height_delta: invoke("_wyd_field_myhuman_height_delta"),
              ground_mask: invoke("_wyd_field_ground_mask_under_player"),
              ground_normal: {
                x: invoke("_wyd_field_ground_normal_under_player_x"),
                y: invoke("_wyd_field_ground_normal_under_player_y"),
                z: invoke("_wyd_field_ground_normal_under_player_z"),
              },
            },
            mouse_over_human_id: readUint32(
              "_wyd_field_mouse_over_human_id",
            ),
            visible_humans: {
              limit: visibleHumanLimit,
              total:
                Number.isInteger(visibleHumanTotalRaw) &&
                visibleHumanTotalRaw >= 0
                  ? visibleHumanTotalRaw
                  : null,
              captured: visibleHumanCaptured,
              entries: visibleHumans,
            },
            weather: {
              active: invoke("_wyd_field_weather_active"),
              rain_visible: invoke("_wyd_field_rain_visible"),
              snow_visible: invoke("_wyd_field_snow_visible"),
              snow2_visible: invoke("_wyd_field_snow2_visible"),
            },
            objects: {
              count: invoke("_wyd_field_object_count"),
              failed: invoke("_wyd_field_object_failed"),
              checksum_failed: invoke("_wyd_field_object_checksum_failed"),
              sea: invoke("_wyd_field_object_sea_count"),
              tree: invoke("_wyd_field_object_tree_count"),
              house: invoke("_wyd_field_object_house_count"),
              light: invoke("_wyd_field_object_light_count"),
              generic: invoke("_wyd_field_object_generic_count"),
              last_mask_index: invoke("_wyd_field_object_last_mask_index"),
              static_draws: invoke("_wyd_field_static_object_draws"),
            },
            visuals: {
              total_draws: invoke("_wyd_field_visual_total_draws"),
              terrain_draws: invoke("_wyd_field_visual_terrain_draws"),
              ground_draws: invoke("_wyd_field_visual_ground_draws"),
              water_draws: invoke("_wyd_field_visual_water_draws"),
              sky_draws: invoke("_wyd_field_visual_sky_draws"),
              human_draws: invoke("_wyd_field_visual_human_draws"),
              object_draws: invoke("_wyd_field_visual_object_draws"),
              effect_draws: invoke("_wyd_field_visual_effect_draws"),
              hud_draws: invoke("_wyd_field_visual_hud_draws"),
              hud_art_draws: invoke("_wyd_field_visual_hud_art_draws"),
            },
          },
          random: {
            armed: invoke("_wyd_compare_random_is_armed"),
            configured_seed: readUint32(
              "_wyd_compare_random_configured_seed",
            ),
            state: readUint32("_wyd_compare_random_state"),
            rand_calls: readUint32("_wyd_compare_random_rand_calls"),
            srand_calls: readUint32("_wyd_compare_random_srand_calls"),
            last_requested_seed: readUint32(
              "_wyd_compare_random_last_requested_seed",
            ),
          },
        },
      },
    },
  };
}

export async function releasePairedFrame({
  frameId,
  maxWasmPumps,
  native,
  page,
  selector,
  timeMs,
  timeoutMs,
  wasmInputs = [],
  releaseBarrier,
}) {
  if (
    !releaseBarrier ||
    typeof releaseBarrier.arm !== "function" ||
    typeof releaseBarrier.finish !== "function" ||
    typeof releaseBarrier.cancel !== "function"
  ) {
    throw new Error("paired frame requires an installed release barrier");
  }
  const release = releaseBarrier.arm(frameId, timeMs);
  let wasm;
  let nativeReleasedAtMs;
  try {
    wasm = await page.evaluate(executeWasmTick, {
      frameId: comparableFrameId(frameId),
      maxPumps: maxWasmPumps,
      release,
      selector,
      timeMs,
      wasmInputs,
    });
    ({ nativeReleasedAtMs } = releaseBarrier.finish(release.token));
  } catch (error) {
    releaseBarrier.cancel(release.token);
    throw error;
  }

  /*
   * STEP_ACCEPTED may already be buffered here. It is deliberately observed
   * only after the WASM tick: the binding callback sends STEP and returns
   * immediately, so neither client waits for the native acknowledgment before
   * executing the released frame.
   */
  const accepted = await native.expect("STEP_ACCEPTED", timeoutMs);
  if (accepted.frameId !== frameId || accepted.timeMs !== timeMs) {
    throw new Error("native STEP_ACCEPTED does not match requested frame/time");
  }

  const presented = await native.expect("PRESENT", timeoutMs);
  if (presented.frameId !== frameId || presented.timeMs !== timeMs) {
    throw new Error("native PRESENT does not match requested frame/time");
  }
  if (presented.captureHresult !== 0 || !presented.snapshotWritten) {
    throw new Error(
      `native frame ${frameId} capture failed: HRESULT=0x${presented.captureHresult
        .toString(16)
        .padStart(8, "0")}, snapshot=${presented.snapshotWritten}`,
    );
  }
  return { accepted, nativeReleasedAtMs, presented, wasm };
}

function parseArguments(argv) {
  const options = {
    actionsJson: null,
    browser: "chromium",
    closeNative: false,
    frameCount: 1,
    frameStart: 1n,
    headless: true,
    launchArgs: [],
    maxWasmPumps: DEFAULT_MAX_WASM_PUMPS,
    randomSeed: null,
    selector: "#canvas",
    tickMs: 16,
    timeStartMs: 0,
    timeoutMs: 60_000,
  };
  const valueNames = new Map([
    ["--actions-json", "actionsJson"],
    ["--browser", "browser"],
    ["--frame-count", "frameCount"],
    ["--frame-start", "frameStart"],
    ["--native-artifacts", "nativeArtifacts"],
    ["--max-wasm-pumps", "maxWasmPumps"],
    ["--output", "output"],
    ["--pipe", "pipe"],
    ["--random-seed", "randomSeed"],
    ["--selector", "selector"],
    ["--tick-ms", "tickMs"],
    ["--time-start-ms", "timeStartMs"],
    ["--timeout-ms", "timeoutMs"],
    ["--url", "url"],
  ]);

  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === "--headful") {
      options.headless = false;
      continue;
    }
    if (argument === "--close-native") {
      options.closeNative = true;
      continue;
    }
    if (argument === "--launch-arg") {
      index += 1;
      if (index >= argv.length) throw new Error("--launch-arg requires a value");
      options.launchArgs.push(argv[index]);
      continue;
    }
    const name = valueNames.get(argument);
    if (!name) throw new Error(`unknown argument: ${argument}`);
    index += 1;
    if (index >= argv.length) throw new Error(`${argument} requires a value`);
    options[name] = argv[index];
  }

  for (const name of [
    "frameCount",
    "maxWasmPumps",
    "tickMs",
    "timeStartMs",
    "timeoutMs",
  ]) {
    options[name] = Number.parseInt(options[name], 10);
  }
  unsignedInteger(options.frameCount, 1_000_000, "frameCount");
  if (options.frameCount === 0) throw new Error("frameCount must be greater than zero");
  unsignedInteger(options.tickMs, MAX_TIME_MS, "tickMs");
  unsignedInteger(options.maxWasmPumps, 1_000_000, "maxWasmPumps");
  if (options.maxWasmPumps === 0) {
    throw new Error("maxWasmPumps must be greater than zero");
  }
  unsignedInteger(options.timeStartMs, MAX_TIME_MS, "timeStartMs");
  unsignedInteger(options.timeoutMs, 3_600_000, "timeoutMs");
  if (options.timeoutMs === 0) throw new Error("timeoutMs must be greater than zero");
  if (options.randomSeed !== null) {
    options.randomSeed = parseRandomSeed(options.randomSeed);
  }
  options.frameStart = unsignedFrameId(options.frameStart, "frameStart");
  const finalFrame = options.frameStart + BigInt(options.frameCount - 1);
  if (finalFrame > MAX_FRAME_ID) throw new Error("frame range exceeds uint64");
  const finalTime =
    options.timeStartMs + options.tickMs * Math.max(0, options.frameCount - 1);
  if (!Number.isSafeInteger(finalTime) || finalTime > MAX_TIME_MS) {
    throw new Error("controlled time range exceeds uint32");
  }
  for (const required of ["url", "pipe", "output", "nativeArtifacts"]) {
    if (!options[required]) throw new Error(`--${required.replace(/[A-Z]/gu, (m) => `-${m.toLowerCase()}`)} is required`);
  }
  options.pipe = normalizePipePath(options.pipe);
  return options;
}

function frameStem(frameId) {
  return `frame_${frameId.toString(10).padStart(20, "0")}`;
}

function comparableFrameId(frameId) {
  return frameId <= BigInt(Number.MAX_SAFE_INTEGER)
    ? Number(frameId)
    : frameId.toString(10);
}

function comparableArtifactPath(value) {
  const resolved = path.resolve(value);
  return process.platform === "win32" ? resolved.toLowerCase() : resolved;
}

function pathsOverlap(left, right) {
  const relative = path.relative(left, right);
  return (
    relative === "" ||
    (!relative.startsWith(`..${path.sep}`) &&
      relative !== ".." &&
      !path.isAbsolute(relative))
  );
}

async function requireEmptyDirectory(directory, label) {
  let info;
  try {
    info = await lstat(directory);
  } catch (error) {
    if (error?.code !== "ENOENT") throw error;
    await mkdir(directory, { recursive: true });
    info = await lstat(directory);
  }
  if (info.isSymbolicLink() || !info.isDirectory()) {
    throw new Error(`${label} must be a real directory: ${directory}`);
  }
  const entries = await readdir(directory);
  if (entries.length !== 0) {
    const preview = entries.slice(0, 5).join(", ");
    throw new Error(
      `${label} must be new or empty; refusing to reuse ${directory} (${preview})`,
    );
  }
}

/*
 * A run owns fresh, non-overlapping artifact roots. Existing non-empty
 * directories are rejected instead of being cleaned, so a typo can never
 * silently erase or mix a previous comparison with the current one.
 */
export async function prepareArtifactDirectories(
  outputRootValue,
  nativeArtifactsValue,
) {
  const outputRoot = path.resolve(outputRootValue);
  const nativeArtifacts = path.resolve(nativeArtifactsValue);
  const wasmArtifacts = path.join(outputRoot, "wasm");
  const comparableOutput = comparableArtifactPath(outputRoot);
  const comparableNative = comparableArtifactPath(nativeArtifacts);
  const comparableWasm = comparableArtifactPath(wasmArtifacts);

  if (
    comparableOutput === comparableNative ||
    comparableWasm === comparableNative
  ) {
    throw new Error(
      "outputRoot, nativeArtifacts, and the WASM artifact directory must be distinct",
    );
  }
  if (
    pathsOverlap(comparableOutput, comparableNative) ||
    pathsOverlap(comparableNative, comparableOutput)
  ) {
    throw new Error(
      "nativeArtifacts must not overlap outputRoot; use separate fresh directories",
    );
  }

  await requireEmptyDirectory(outputRoot, "outputRoot");
  await requireEmptyDirectory(nativeArtifacts, "nativeArtifacts");
  await mkdir(wasmArtifacts);
  return { nativeArtifacts, outputRoot, wasmArtifacts };
}

export async function waitForFreshFile(filePath, timeoutMs, notBeforeMs) {
  unsignedInteger(timeoutMs, 3_600_000, "artifact timeout");
  if (timeoutMs === 0) {
    throw new Error("artifact timeout must be greater than zero");
  }
  if (!Number.isFinite(notBeforeMs) || notBeforeMs < 0) {
    throw new Error("artifact notBeforeMs must be a non-negative timestamp");
  }
  const deadline = Date.now() + timeoutMs;
  let lastError = null;
  while (Date.now() < deadline) {
    try {
      const beforeRead = await stat(filePath);
      if (!beforeRead.isFile()) {
        throw new Error("artifact path is not a file");
      }
      if (beforeRead.mtimeMs < notBeforeMs) {
        throw new Error(
          `artifact predates this frame (${beforeRead.mtimeMs} < ${notBeforeMs})`,
        );
      }
      const contents = await readFile(filePath);
      const afterRead = await stat(filePath);
      if (
        beforeRead.size !== afterRead.size ||
        beforeRead.mtimeMs !== afterRead.mtimeMs
      ) {
        throw new Error("artifact changed while it was being read");
      }
      return contents;
    } catch (error) {
      lastError = error;
      await delay(20);
    }
  }
  throw new Error(`expected artifact was not written: ${filePath}: ${lastError?.message}`);
}

function validateNativeSnapshot(contents, expectedFrameId, expectedTimeMs, filePath) {
  let snapshot;
  try {
    snapshot = JSON.parse(contents.toString("utf8"));
  } catch (error) {
    throw new Error(`native snapshot is not valid JSON (${filePath}): ${error.message}`);
  }
  const expected = unsignedFrameId(expectedFrameId);
  if (
    unsignedFrameId(snapshot?.frame_id, "native snapshot frame_id") !==
      expected ||
    unsignedFrameId(
      snapshot?.ticks?.compare_frame,
      "native snapshot ticks.compare_frame",
    ) !== expected
  ) {
    throw new Error(`native snapshot does not identify frame ${expected}`);
  }
  if (snapshot?.clock?.controlled_time_ms !== expectedTimeMs) {
    throw new Error(
      `native snapshot controlled time ${snapshot?.clock?.controlled_time_ms} does not match ${expectedTimeMs}`,
    );
  }
  return snapshot;
}

async function main() {
  const options = parseArguments(process.argv.slice(2));
  const { nativeArtifacts, outputRoot, wasmArtifacts } =
    await prepareArtifactDirectories(options.output, options.nativeArtifacts);
  const runId = randomUUID();
  const startedAt = new Date().toISOString();
  await writeFile(
    path.join(outputRoot, "paired-run-start.json"),
    `${JSON.stringify(
      {
        schema: "openwyd.paired-tick-run-start",
        schema_version: 1,
        run_id: runId,
        started_at: startedAt,
        native_artifacts: nativeArtifacts,
        wasm_artifacts: wasmArtifacts,
      },
      null,
      2,
    )}\n`,
    "utf8",
  );

  let actionSchedule = parseActionSchedule({
    schema: "openwyd.paired-input-actions",
    schema_version: 1,
    actions: [],
  });
  let actionsJson = null;
  if (options.actionsJson) {
    actionsJson = path.resolve(options.actionsJson);
    let actionDocument;
    try {
      actionDocument = JSON.parse(await readFile(actionsJson, "utf8"));
    } catch (error) {
      throw new Error(
        `could not read actions JSON ${actionsJson}: ${error.message}`,
      );
    }
    actionSchedule = parseActionSchedule(actionDocument);
  }
  const lastFrame =
    options.frameStart + BigInt(options.frameCount - 1);
  validateActionScheduleRange(
    actionSchedule,
    options.frameStart,
    lastFrame,
  );

  const native = await connectNativePipe(options.pipe, options.timeoutMs);
  let browser = null;
  let page = null;
  let browserErrors = null;
  let releaseBarrier = null;
  let lastInputSequence = 0n;
  const result = {
    schema: "openwyd.paired-tick-run",
    schema_version: 1,
    actions: {
      action_count: actionSchedule.actionCount,
      source: actionsJson,
    },
    width: CAPTURE_WIDTH,
    height: CAPTURE_HEIGHT,
    max_wasm_pumps: options.maxWasmPumps,
    random_seed: options.randomSeed,
    run_id: runId,
    started_at: startedAt,
    pipe: options.pipe,
    url: options.url,
    frames: [],
  };

  try {
    const ready = await native.expect("READY", options.timeoutMs);
    if (
      ready.width !== CAPTURE_WIDTH ||
      ready.height !== CAPTURE_HEIGHT ||
      !ready.captureEnabled
    ) {
      throw new Error(
        `native READY is ${ready.width}x${ready.height}, capture=${ready.captureEnabled}; expected 800x600 capture`,
      );
    }
    result.native = ready;

    if (options.randomSeed !== null) {
      native.send(nativeRandomSeedCommand(options.randomSeed));
      const seeded = await native.expect("RANDOM_SEEDED", options.timeoutMs);
      if (seeded.seed !== options.randomSeed) {
        throw new Error(
          `native random seed ACK ${seeded.seed} does not match ${options.randomSeed}`,
        );
      }
      result.native_random_seed = seeded.seed;
    } else {
      result.native_random_seed = null;
    }

    const requireFromCwd = createRequire(path.join(process.cwd(), "package.json"));
    const playwright = requireFromCwd("playwright");
    const browserType = playwright[options.browser];
    if (!browserType || typeof browserType.launch !== "function") {
      throw new Error(`unsupported Playwright browser: ${options.browser}`);
    }
    let launchOptions = {
      args: options.launchArgs,
      headless: options.headless,
    };
    if (options.browser === "chromium") {
      try {
        const portableBrowser = await import(
          pathToFileURL(
            path.join(process.cwd(), "tools", "playwright_portable_browser.mjs"),
          ).href
        );
        launchOptions = portableBrowser.chromiumLaunchOptions(launchOptions);
      } catch (error) {
        if (error?.code !== "ERR_MODULE_NOT_FOUND") throw error;
      }
    }
    browser = await browserType.launch(launchOptions);
    page = await browser.newPage({
      viewport: { width: CAPTURE_WIDTH, height: CAPTURE_HEIGHT },
    });
    page.setDefaultTimeout(options.timeoutMs);
    browserErrors = createBrowserErrorMonitor(page);
    releaseBarrier = createPairedReleaseBarrier({ native });
    await page.exposeBinding(
      releaseBarrier.bindingName,
      releaseBarrier.callback,
    );

    const harnessUrl = new URL(options.url);
    harnessUrl.searchParams.set("autoboot", "0");
    harnessUrl.searchParams.set("autostart", "0");
    harnessUrl.searchParams.set("logical", "800x600");
    harnessUrl.searchParams.set("layout", "capture");
    harnessUrl.searchParams.set("quiet", "1");
    await page.goto(harnessUrl.href, {
      timeout: options.timeoutMs,
      waitUntil: "domcontentloaded",
    });
    await page.waitForFunction(() => globalThis.__runtimeReady === true);
    browserErrors.assertClean("WASM harness startup");

    const wasmBoot = await page.evaluate(prepareAndBootWasmClient, {
      randomSeed: options.randomSeed,
      startTimeMs: options.timeStartMs,
    });
    browserErrors.assertClean("WASM client boot");
    if (wasmBoot.bootResult !== 1) {
      throw new Error(
        `official WASM boot path failed with result ${wasmBoot.bootResult}`,
      );
    }
    if (options.randomSeed !== null) {
      if (
        wasmBoot.random?.armResult !== 1 ||
        wasmBoot.random?.armed !== 1 ||
        wasmBoot.random?.configuredSeed !== options.randomSeed
      ) {
        throw new Error("WASM random seed did not arm exactly as requested");
      }
      result.wasm_random_seed = wasmBoot.random;
    } else {
      result.wasm_random_seed = null;
    }

    for (let index = 0; index < options.frameCount; index += 1) {
      const frameId = options.frameStart + BigInt(index);
      const timeMs = options.timeStartMs + options.tickMs * index;
      const frameIdText = frameId.toString(10);
      const scheduled = actionSchedule.byFrame.get(frameIdText) ?? {
        actionTypes: [],
        nativeInputs: [],
        wasmInputs: [],
      };
      const nativeInputSequences = [];
      for (const input of scheduled.nativeInputs) {
        native.send(nativeInputCommand(frameId, input));
        const queued = await native.expect(
          "INPUT_QUEUED",
          options.timeoutMs,
        );
        if (queued.frameId !== frameId) {
          throw new Error(
            `native INPUT_QUEUED frame ${queued.frameId} does not match ${frameId}`,
          );
        }
        if (queued.inputSequence !== lastInputSequence + 1n) {
          throw new Error(
            `native input sequence ${queued.inputSequence} is not the expected ${
              lastInputSequence + 1n
            }`,
          );
        }
        lastInputSequence = queued.inputSequence;
        nativeInputSequences.push(comparableFrameId(queued.inputSequence));
      }
      const { nativeReleasedAtMs, wasm } = await releasePairedFrame({
        frameId,
        maxWasmPumps: options.maxWasmPumps,
        native,
        page,
        selector: options.selector,
        timeMs,
        timeoutMs: options.timeoutMs,
        wasmInputs: scheduled.wasmInputs,
        releaseBarrier,
      });
      browserErrors.assertClean(`paired frame ${frameIdText}`);
      const wasmInputResults = wasm.inputResults;

      const stem = frameStem(frameId);
      const wasmPng = path.join(wasmArtifacts, `${stem}.png`);
      const wasmJson = path.join(wasmArtifacts, `${stem}.json`);
      const nativePng = path.join(nativeArtifacts, `${stem}.png`);
      const nativeJson = path.join(nativeArtifacts, `${stem}.json`);
      const encoded = wasm.dataUrl.slice(wasm.dataUrl.indexOf(",") + 1);
      await writeFile(wasmPng, Buffer.from(encoded, "base64"));
      await writeFile(wasmJson, `${JSON.stringify(wasm.snapshot, null, 2)}\n`, "utf8");
      await waitForFreshFile(
        nativePng,
        options.timeoutMs,
        nativeReleasedAtMs,
      );
      const nativeSnapshotContents = await waitForFreshFile(
        nativeJson,
        options.timeoutMs,
        nativeReleasedAtMs,
      );
      validateNativeSnapshot(
        nativeSnapshotContents,
        frameId,
        timeMs,
        nativeJson,
      );
      result.frames.push({
        actions: {
          logical: scheduled.actionTypes,
          native_input_count: scheduled.nativeInputs.length,
          native_input_sequences: nativeInputSequences,
          wasm_input_count: scheduled.wasmInputs.length,
          wasm_input_results: wasmInputResults,
        },
        frame_id: comparableFrameId(frameId),
        time_ms: timeMs,
        wasm_pump_count: wasm.snapshot.ticks.wasm_pump_count,
        native_png: nativePng,
        native_snapshot: nativeJson,
        wasm_png: wasmPng,
        wasm_snapshot: wasmJson,
      });
    }

    if (options.closeNative) {
      native.send("CLOSE");
      await native.expect("CLOSING", options.timeoutMs);
    }
    await page.evaluate(() => {
      globalThis.stopAutoTick?.();
      if (typeof globalThis.Module?._wyd_shutdown_client === "function") {
        globalThis.Module._wyd_shutdown_client();
      }
    });
    browserErrors.assertClean("WASM client shutdown");
    await writeFile(
      path.join(outputRoot, "paired-run.json"),
      `${JSON.stringify(result, null, 2)}\n`,
      "utf8",
    );
    process.stdout.write(`${JSON.stringify(result)}\n`);
  } finally {
    native.close();
    if (browser) await browser.close();
  }
}

const invokedPath = process.argv[1] ? pathToFileURL(path.resolve(process.argv[1])).href : "";
if (import.meta.url === invokedPath) {
  main().catch((error) => {
    process.stderr.write(
      `paired tick error: ${error instanceof Error ? error.stack ?? error.message : String(error)}\n`,
    );
    process.exitCode = 2;
  });
}
