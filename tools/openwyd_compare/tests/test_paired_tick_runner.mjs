import assert from "node:assert/strict";
import {
  mkdir,
  mkdtemp,
  readFile,
  readdir,
  rm,
  utimes,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import test from "node:test";

import {
  applyWasmInputs,
  createBrowserErrorMonitor,
  createPairedReleaseBarrier,
  encodeCp1252,
  executeWasmTick,
  nativeInputCommand,
  nativeRandomSeedCommand,
  normalizePipePath,
  parseActionSchedule,
  parseNativeEvent,
  parseRandomSeed,
  prepareAndBootWasmClient,
  prepareArtifactDirectories,
  releasePairedFrame,
  unsignedFrameId,
  validateActionScheduleRange,
  waitForFreshFile,
} from "../paired_tick_runner.mjs";

function fakePresentStateExports(readLatch) {
  return {
    _wyd_compare_present_state_sequence() {
      return readLatch().sequence;
    },
    _wyd_compare_present_game_state_valid() {
      return readLatch().gameValid;
    },
    _wyd_compare_present_game_state() {
      return readLatch().game;
    },
    _wyd_compare_present_scene_type_valid() {
      return readLatch().sceneValid;
    },
    _wyd_compare_present_scene_type() {
      return readLatch().scene;
    },
    _wyd_d3d9_gl_error_total() {
      return 0;
    },
  };
}

function fakeClockExports(initialTime = 0) {
  let fakeTime = initialTime >>> 0;
  return {
    _wyd_debug_set_fake_time(value) {
      fakeTime = value >>> 0;
    },
    _wyd_debug_get_time() {
      return fakeTime;
    },
  };
}

test("normalizes only local named-pipe paths", () => {
  assert.equal(
    normalizePipePath("OpenWyd.Compare.1"),
    "\\\\.\\pipe\\OpenWyd.Compare.1",
  );
  assert.equal(
    normalizePipePath("\\\\.\\pipe\\OpenWyd.Compare.2"),
    "\\\\.\\pipe\\OpenWyd.Compare.2",
  );
  assert.throws(() => normalizePipePath("bad\nname"), /line breaks/u);
});

test("artifact roots are fresh, empty, distinct, and never cleaned implicitly", async (t) => {
  const scratch = await mkdtemp(path.join(tmpdir(), "openwyd-runner-artifacts-"));
  t.after(async () => {
    await rm(scratch, { force: true, recursive: true });
  });

  const outputRoot = path.join(scratch, "paired");
  const nativeArtifacts = path.join(scratch, "native");
  const prepared = await prepareArtifactDirectories(
    outputRoot,
    nativeArtifacts,
  );
  assert.deepEqual(prepared, {
    nativeArtifacts: path.resolve(nativeArtifacts),
    outputRoot: path.resolve(outputRoot),
    wasmArtifacts: path.resolve(outputRoot, "wasm"),
  });
  assert.deepEqual(await readdir(prepared.nativeArtifacts), []);
  assert.deepEqual(await readdir(prepared.wasmArtifacts), []);

  await assert.rejects(
    prepareArtifactDirectories(outputRoot, path.join(scratch, "native-2")),
    /must be new or empty/u,
  );

  const occupiedNative = path.join(scratch, "occupied-native");
  const untouched = path.join(occupiedNative, "do-not-delete.txt");
  await mkdir(occupiedNative);
  await writeFile(untouched, "preserve me", "utf8");
  await assert.rejects(
    prepareArtifactDirectories(
      path.join(scratch, "paired-2"),
      occupiedNative,
    ),
    /must be new or empty/u,
  );
  assert.equal(await readFile(untouched, "utf8"), "preserve me");

  await assert.rejects(
    prepareArtifactDirectories(
      path.join(scratch, "overlap"),
      path.join(scratch, "overlap", "native"),
    ),
    /must not overlap/u,
  );
  await assert.rejects(
    prepareArtifactDirectories(
      path.join(scratch, "same"),
      path.join(scratch, "same"),
    ),
    /must be distinct/u,
  );
});

test("artifact wait refuses a file older than this frame", async (t) => {
  const scratch = await mkdtemp(path.join(tmpdir(), "openwyd-runner-fresh-"));
  t.after(async () => {
    await rm(scratch, { force: true, recursive: true });
  });
  const artifact = path.join(scratch, "frame.png");
  await writeFile(artifact, "old frame");
  await utimes(artifact, new Date(1000), new Date(1000));

  await assert.rejects(
    waitForFreshFile(artifact, 60, Date.now()),
    /artifact predates this frame/u,
  );

  const notBeforeMs = Date.now() - 1000;
  await writeFile(artifact, "current frame");
  assert.equal(
    (await waitForFreshFile(artifact, 1000, notBeforeMs)).toString("utf8"),
    "current frame",
  );
});

test("parses bounded native READY and PRESENT events", () => {
  assert.deepEqual(parseNativeEvent("READY 1 42 800 600 1"), {
    kind: "READY",
    version: 1,
    pid: 42,
    width: 800,
    height: 600,
    captureEnabled: true,
  });
  assert.deepEqual(parseNativeEvent("PRESENT 7 32 0x00000000 1"), {
    kind: "PRESENT",
    frameId: 7n,
    timeMs: 32,
    captureHresult: 0,
    snapshotWritten: true,
  });
  assert.deepEqual(parseNativeEvent("INPUT_QUEUED 19 7"), {
    kind: "INPUT_QUEUED",
    inputSequence: 19n,
    frameId: 7n,
  });
  assert.deepEqual(parseNativeEvent("RANDOM_SEEDED 4294967295"), {
    kind: "RANDOM_SEEDED",
    seed: 0xffff_ffff,
  });
  assert.throws(
    () => parseNativeEvent("PRESENT 7 32 0x0 1"),
    /eight hex digits/u,
  );
  assert.throws(
    () => parseNativeEvent("READY 2 42 800 600 1"),
    /unsupported/u,
  );
});

test("random seed CLI and native command preserve the full uint32 range", () => {
  assert.equal(parseRandomSeed("0"), 0);
  assert.equal(parseRandomSeed("4294967295"), 0xffff_ffff);
  assert.equal(nativeRandomSeedCommand(123456789), "RANDOM_SEED 123456789");
  assert.throws(() => parseRandomSeed("-1"), /unsigned decimal/u);
  assert.throws(() => parseRandomSeed("4294967296"), /range/u);
  assert.throws(() => parseRandomSeed("0x10"), /unsigned decimal/u);
});

test("frame IDs accept only strict decimal uint64 values", () => {
  assert.equal(unsignedFrameId(0), 0n);
  assert.equal(unsignedFrameId(42n), 42n);
  assert.equal(unsignedFrameId("18446744073709551615"), (1n << 64n) - 1n);
  for (const invalid of [
    true,
    false,
    "",
    " ",
    " 1",
    "1 ",
    "0x10",
    "01",
    -1,
    1.5,
    Number.MAX_SAFE_INTEGER + 1,
  ]) {
    assert.throws(
      () => unsignedFrameId(invalid),
      /unsigned(?: decimal)? 64-bit integer/u,
      `unexpectedly accepted ${JSON.stringify(invalid)}`,
    );
  }
  assert.throws(
    () => unsignedFrameId("18446744073709551616"),
    /range/u,
  );
});

test("WASM deterministic RNG is armed and verified before client boot", () => {
  const priorModule = globalThis.Module;
  const priorStopAutoTick = globalThis.stopAutoTick;
  const calls = [];
  let configuredSeed = 0;
  let armed = false;
  let fakeTime = 0;
  globalThis.stopAutoTick = () => calls.push("stop-auto-tick");
  globalThis.Module = {
    _wyd_compare_random_arm(seed) {
      calls.push("random-arm");
      configuredSeed = seed >>> 0;
      armed = true;
      return 1;
    },
    _wyd_compare_random_is_armed() {
      calls.push("random-verify-armed");
      return armed ? 1 : 0;
    },
    _wyd_compare_random_configured_seed() {
      calls.push("random-verify-seed");
      return configuredSeed;
    },
    _wyd_debug_set_fake_time(timeMs) {
      calls.push(`fake-time-${timeMs}`);
      fakeTime = timeMs >>> 0;
    },
    _wyd_debug_get_time() {
      calls.push("fake-time-read");
      return fakeTime;
    },
    _wyd_boot_client() {
      calls.push("boot");
      assert.equal(armed, true);
      assert.equal(configuredSeed, 0xfedc_ba98);
      return 1;
    },
  };
  try {
    assert.deepEqual(
      prepareAndBootWasmClient({
        randomSeed: 0xfedc_ba98,
        startTimeMs: 1234,
      }),
      {
        bootResult: 1,
        random: {
          armResult: 1,
          armed: 1,
          configuredSeed: 0xfedc_ba98,
        },
      },
    );
    assert.deepEqual(calls, [
      "stop-auto-tick",
      "random-arm",
      "random-verify-armed",
      "random-verify-seed",
      "fake-time-1234",
      "fake-time-read",
      "boot",
      "fake-time-read",
    ]);
  } finally {
    globalThis.Module = priorModule;
    globalThis.stopAutoTick = priorStopAutoTick;
  }
});

test("WASM boot rejects a fake clock that does not take the requested value", () => {
  const priorModule = globalThis.Module;
  let bootCalls = 0;
  globalThis.Module = {
    _wyd_debug_set_fake_time() {},
    _wyd_debug_get_time() {
      return 0;
    },
    _wyd_boot_client() {
      bootCalls += 1;
      return 1;
    },
  };
  try {
    assert.throws(
      () =>
        prepareAndBootWasmClient({
          randomSeed: null,
          startTimeMs: 1234,
        }),
      /does not match requested boot time/u,
    );
    assert.equal(bootCalls, 0);
  } finally {
    globalThis.Module = priorModule;
  }
});

test("browser page errors and console.error are collected as fatal", () => {
  const handlers = new Map();
  const monitor = createBrowserErrorMonitor({
    on(name, handler) {
      handlers.set(name, handler);
    },
  });

  handlers.get("console")({
    type: () => "warning",
    text: () => "not fatal",
    location: () => ({}),
  });
  monitor.assertClean("warning-only stage");

  handlers.get("console")({
    type: () => "error",
    text: () => "WebGL exploded",
    location: () => ({ url: "http://127.0.0.1/client.js", lineNumber: 9 }),
  });
  handlers.get("pageerror")(new Error("uncaught client failure"));
  assert.equal(monitor.failures.length, 2);
  assert.throws(
    () => monitor.assertClean("frame 7"),
    /console\.error[\s\S]*WebGL exploded[\s\S]*pageerror[\s\S]*uncaught client failure/u,
  );
});

test("strict CP1252 encoding covers official text and rejects substitutions", () => {
  assert.deepEqual(
    encodeCp1252("OpenWyd €–ação"),
    [79, 112, 101, 110, 87, 121, 100, 32, 0x80, 0x96, 97, 231, 227, 111],
  );
  assert.throws(() => encodeCp1252("\0"), /not representable/u);
  assert.throws(() => encodeCp1252("🎮"), /U\+1F3AE/u);
});

test("action schedule preserves frame semantics and allows distinct accounts", () => {
  const schedule = parseActionSchedule({
    schema: "openwyd.paired-input-actions",
    schema_version: 1,
    actions: [
      {
        frame_id: "3",
        type: "text",
        native_text: "CMPNATIVE",
        wasm_text: "CMPWASM",
      },
      { frame_id: 1, type: "mouse_move", x: 400, y: 300 },
      {
        frame_id: 2,
        type: "mouse_down",
        button: "left",
        x: 510,
        y: 440,
      },
      {
        frame_id: 2,
        type: "mouse_up",
        button: "LEFT",
        x: 510,
        y: 440,
      },
      { frame_id: 4, type: "char", char: "€" },
      {
        frame_id: 5,
        type: "mouse_down",
        button: "RIGHT",
        x: 100,
        y: 200,
      },
      {
        frame_id: 5,
        type: "mouse_up",
        button: "RIGHT",
        x: 100,
        y: 200,
      },
      { frame_id: 6, type: "key_down", key: 13 },
      { frame_id: 6, type: "key_up", key: 13 },
    ],
  });

  assert.equal(schedule.actionCount, 9);
  assert.deepEqual(
    [...schedule.byFrame.keys()],
    ["1", "2", "3", "4", "5", "6"],
  );
  assert.deepEqual(schedule.byFrame.get("2").wasmInputs, [
    {
      channel: "mouse",
      message: 0x0201,
      wParam: 1,
      x: 510,
      y: 440,
    },
    {
      channel: "mouse",
      message: 0x0202,
      wParam: 0,
      x: 510,
      y: 440,
    },
  ]);
  assert.equal(schedule.byFrame.get("3").nativeInputs.length, 9);
  assert.equal(schedule.byFrame.get("3").wasmInputs.length, 7);
  assert.equal(
    nativeInputCommand(3n, schedule.byFrame.get("3").nativeInputs[0]),
    "INPUT 1 3 CHAR 67",
  );
  assert.equal(schedule.byFrame.get("4").nativeInputs[0].byte, 0x80);
  assert.deepEqual(schedule.byFrame.get("5").wasmInputs, [
    {
      channel: "mouse",
      message: 0x0204,
      wParam: 2,
      x: 100,
      y: 200,
    },
    {
      channel: "mouse",
      message: 0x0205,
      wParam: 0,
      x: 100,
      y: 200,
    },
  ]);
  assert.deepEqual(schedule.byFrame.get("6").wasmInputs, [
    {
      channel: "key",
      message: 0x0100,
      wParam: 13,
      lParam: 1,
    },
    {
      channel: "key",
      message: 0x0101,
      wParam: 13,
      lParam: 0xc0000001,
    },
  ]);
  validateActionScheduleRange(schedule, 1n, 6n);
  assert.throws(
    () => validateActionScheduleRange(schedule, 1n, 5n),
    /outside run range/u,
  );
});

test("action parser catches imprecise frames, invalid input, and state errors", () => {
  const document = (actions) => ({
    schema_version: 1,
    actions,
  });
  assert.throws(
    () =>
      parseActionSchedule(
        document([
          {
            frame_id: Number.MAX_SAFE_INTEGER + 1,
            type: "mouse_move",
            x: 0,
            y: 0,
          },
        ]),
      ),
    /unsigned 64-bit/u,
  );
  assert.throws(
    () =>
      parseActionSchedule(
        document([
          {
            frame_id: 1,
            type: "mouse_up",
            button: "LEFT",
            x: 0,
            y: 0,
          },
        ]),
      ),
    /not down/u,
  );
  assert.throws(
    () =>
      parseActionSchedule(
        document([
          {
            frame_id: 1,
            type: "text",
            text: "same",
            native_text: "different",
          },
        ]),
      ),
    /either text/u,
  );
  assert.throws(
    () =>
      parseActionSchedule(
        document([{ frame_id: 1, type: "char", char: "ab" }]),
      ),
    /exactly one/u,
  );
});

test("WASM input application calls only the original input exports", () => {
  const priorModule = globalThis.Module;
  const calls = [];
  globalThis.Module = {
    _wyd_mouse_event(...args) {
      calls.push(["mouse", ...args]);
      return 11;
    },
    _wyd_key_event(...args) {
      calls.push(["key", ...args]);
      return 12;
    },
  };
  try {
    assert.deepEqual(
      applyWasmInputs([
        {
          channel: "mouse",
          message: 0x0201,
          wParam: 1,
          x: 22,
          y: 33,
        },
        {
          channel: "key",
          message: 0x0102,
          wParam: 0xe7,
          lParam: 1,
        },
      ]),
      [11, 12],
    );
    assert.deepEqual(calls, [
      ["mouse", 0x0201, 1, 22, 33, 0],
      ["key", 0x0102, 0xe7, 1],
    ]);
  } finally {
    globalThis.Module = priorModule;
  }
});

test("paired release prepares WASM before STEP and ticks after the same release", async () => {
  const priorModule = globalThis.Module;
  const priorDocument = globalThis.document;
  const events = [];
  const wasmInputs = [
    {
      channel: "key",
      message: 0x0100,
      wParam: 13,
      lParam: 1,
    },
  ];
  let fakeTime = 0;
  let presents = 0;
  const presentState = {
    sequence: 0,
    gameValid: 1,
    game: 1,
    sceneValid: 1,
    scene: 30001,
  };
  globalThis.Module = {
    ...fakePresentStateExports(() => presentState),
    _wyd_debug_set_fake_time(value) {
      fakeTime = value;
      events.push(`wasm:clock:${value}`);
    },
    _wyd_debug_get_time() {
      return fakeTime;
    },
    _wyd_d3d9_present_calls() {
      return presents;
    },
    _wyd_key_event(message, wParam, lParam) {
      events.push(`wasm:input:${fakeTime}:${message}:${wParam}:${lParam}`);
      return 73;
    },
    _wyd_tick_client() {
      events.push(`wasm:tick:${fakeTime}`);
      presentState.sequence += 1;
      presents += 1;
      return 1;
    },
  };
  globalThis.document = {
    querySelector() {
      return {
        width: 800,
        height: 600,
        toDataURL() {
          return "data:image/png;base64,AA==";
        },
      };
    },
  };

  const native = {
    send(command) {
      events.push(`send:${command}`);
    },
    async expect(kind) {
      if (kind === "STEP_ACCEPTED") {
        events.push("wait:STEP_ACCEPTED");
        assert.ok(
          events.includes("wasm:tick:272"),
          "WASM tick must start before STEP_ACCEPTED is awaited",
        );
        return {
          kind: "STEP_ACCEPTED",
          frameId: 17n,
          timeMs: 272,
        };
      }
      assert.equal(kind, "PRESENT");
      events.push("wait:PRESENT");
      return {
        kind: "PRESENT",
        frameId: 17n,
        timeMs: 272,
        captureHresult: 0,
        snapshotWritten: true,
      };
    },
  };
  const exposed = new Map();
  const page = {
    async exposeBinding(name, callback) {
      exposed.set(name, callback);
      globalThis[name] = (request) => callback({ page }, request);
    },
    async evaluate(fn, args) {
      assert.equal(fn, executeWasmTick);
      assert.equal(args.frameId, 17);
      assert.equal(args.maxPumps, 9);
      assert.equal(args.selector, "#canvas");
      assert.equal(args.timeMs, 272);
      assert.deepEqual(args.wasmInputs, wasmInputs);
      assert.equal(args.release.frameId, "17");
      assert.equal(args.release.timeMs, 272);
      assert.equal(typeof args.release.token, "string");
      assert.equal(args.release.bindingName, releaseBarrier.bindingName);
      events.push("wasm:evaluate");
      return fn(args);
    },
  };
  const releaseBarrier = createPairedReleaseBarrier({ native });
  await page.exposeBinding(
    releaseBarrier.bindingName,
    releaseBarrier.callback,
  );

  try {
    const released = await releasePairedFrame({
      frameId: 17n,
      maxWasmPumps: 9,
      native,
      page,
      releaseBarrier,
      selector: "#canvas",
      timeMs: 272,
      timeoutMs: 1000,
      wasmInputs,
    });
    assert.equal(released.wasm.snapshot.ticks.wasm_pump_count, 1);
    assert.deepEqual(
      released.wasm.snapshot.extensions.wasm.release_barrier,
      {
        frame_id: "17",
        released: true,
        time_ms: 272,
      },
    );
    assert.deepEqual(events, [
      "wasm:evaluate",
      "wasm:clock:272",
      `wasm:input:272:${0x0100}:13:1`,
      "send:STEP 17 272",
      "wasm:tick:272",
      "wait:STEP_ACCEPTED",
      "wait:PRESENT",
    ]);
  } finally {
    delete globalThis[releaseBarrier.bindingName];
    globalThis.Module = priorModule;
    globalThis.document = priorDocument;
  }
});

test("paired release validates frame, time, token, and one-time use", () => {
  const sent = [];
  const native = {
    send(command) {
      sent.push(command);
    },
    expect() {
      throw new Error("event wait is not part of binding validation");
    },
  };
  const barrier = createPairedReleaseBarrier({ native });
  const armed = barrier.arm(23n, 460);

  assert.throws(
    () => barrier.callback({}, { ...armed, frameId: "24" }),
    /frame does not match/u,
  );
  assert.throws(
    () => barrier.callback({}, { ...armed, timeMs: 461 }),
    /time does not match/u,
  );
  assert.throws(
    () => barrier.callback({}, { ...armed, token: "wrong-token" }),
    /token does not match/u,
  );
  assert.deepEqual(sent, []);

  assert.deepEqual(barrier.callback({}, armed), {
    frameId: "23",
    timeMs: 460,
    token: armed.token,
  });
  assert.deepEqual(sent, ["STEP 23 460"]);
  assert.throws(
    () => barrier.callback({}, armed),
    /already used/u,
  );
  assert.throws(
    () => barrier.finish("wrong-token"),
    /completion token is not armed/u,
  );
  const completed = barrier.finish(armed.token);
  assert.equal(typeof completed.nativeReleasedAtMs, "number");
});

test("WASM preparation failure cancels release without STEP or native wait", async () => {
  const priorModule = globalThis.Module;
  const priorDocument = globalThis.document;
  globalThis.Module = {
    ...fakePresentStateExports(() => ({
      sequence: 0,
      gameValid: 0,
      game: 0,
      sceneValid: 0,
      scene: 0,
    })),
    _wyd_debug_set_fake_time() {},
    _wyd_debug_get_time() {
      return 0;
    },
    _wyd_d3d9_present_calls() {
      return 0;
    },
    _wyd_tick_client() {
      throw new Error("tick must not run after preparation failure");
    },
  };
  globalThis.document = {
    querySelector() {
      return {
        width: 800,
        height: 600,
        toDataURL() {
          throw new Error("capture must not run");
        },
      };
    },
  };

  const events = [];
  const native = {
    send(command) {
      events.push(`send:${command}`);
    },
    expect(kind) {
      events.push(`wait:${kind}`);
      throw new Error("native wait must not start");
    },
  };
  const page = {
    async exposeBinding(name, callback) {
      globalThis[name] = (request) => callback({ page }, request);
    },
    evaluate(fn, args) {
      return fn(args);
    },
  };
  const releaseBarrier = createPairedReleaseBarrier({ native });
  await page.exposeBinding(
    releaseBarrier.bindingName,
    releaseBarrier.callback,
  );

  try {
    await assert.rejects(
      releasePairedFrame({
        frameId: 5n,
        maxWasmPumps: 2,
        native,
        page,
        releaseBarrier,
        selector: "#canvas",
        timeMs: 55,
        timeoutMs: 1000,
      }),
      /does not match requested frame time/u,
    );
    assert.deepEqual(events, []);

    const next = releaseBarrier.arm(6n, 66);
    assert.equal(releaseBarrier.cancel(next.token), true);
  } finally {
    delete globalThis[releaseBarrier.bindingName];
    globalThis.Module = priorModule;
    globalThis.document = priorDocument;
  }
});

test("WASM frame sets clock N before dispatching its inputs and pumping", async () => {
  const priorModule = globalThis.Module;
  const priorDocument = globalThis.document;
  let fakeTime = 111;
  let presents = 0;
  const observations = [];
  const presentState = {
    sequence: 0,
    gameValid: 1,
    game: 1,
    sceneValid: 1,
    scene: 30001,
  };
  globalThis.Module = {
    ...fakePresentStateExports(() => presentState),
    _wyd_debug_set_fake_time(value) {
      fakeTime = value;
      observations.push(["clock", value]);
    },
    _wyd_debug_get_time() {
      return fakeTime;
    },
    _wyd_d3d9_present_calls() {
      return presents;
    },
    _wyd_key_event(message, wParam, lParam) {
      observations.push(["input", fakeTime, message, wParam, lParam]);
      return 73;
    },
    _wyd_tick_client() {
      observations.push(["pump", fakeTime]);
      presentState.sequence += 1;
      presents += 1;
      return 1;
    },
  };
  globalThis.document = {
    querySelector() {
      return {
        width: 800,
        height: 600,
        toDataURL() {
          return "data:image/png;base64,AA==";
        },
      };
    },
  };
  try {
    const result = await executeWasmTick({
      frameId: 9,
      maxPumps: 2,
      selector: "#canvas",
      timeMs: 9000,
      wasmInputs: [
        {
          channel: "key",
          message: 0x0100,
          wParam: 13,
          lParam: 1,
        },
      ],
    });
    assert.deepEqual(observations, [
      ["clock", 9000],
      ["input", 9000, 0x0100, 13, 1],
      ["pump", 9000],
    ]);
    assert.deepEqual(result.inputResults, [73]);
    assert.equal(result.snapshot.extensions.wasm.inputs_before_tick, 1);
    assert.deepEqual(result.snapshot.extensions.wasm.input_results, [73]);
  } finally {
    globalThis.Module = priorModule;
    globalThis.document = priorDocument;
  }
});

test("WASM frame rejects a fake clock that changes before Present", async () => {
  const priorModule = globalThis.Module;
  const priorDocument = globalThis.document;
  let fakeTime = 0;
  let presents = 0;
  const presentState = {
    sequence: 0,
    gameValid: 1,
    game: 1,
    sceneValid: 1,
    scene: 30001,
  };
  globalThis.Module = {
    ...fakePresentStateExports(() => presentState),
    _wyd_debug_set_fake_time(value) {
      fakeTime = value >>> 0;
    },
    _wyd_debug_get_time() {
      return fakeTime;
    },
    _wyd_d3d9_present_calls() {
      return presents;
    },
    _wyd_tick_client() {
      fakeTime += 1;
      presentState.sequence += 1;
      presents += 1;
      return 1;
    },
  };
  globalThis.document = {
    querySelector() {
      return {
        width: 800,
        height: 600,
        toDataURL() {
          throw new Error("capture must not run after clock divergence");
        },
      };
    },
  };
  try {
    await assert.rejects(
      executeWasmTick({
        frameId: 1,
        maxPumps: 2,
        selector: "#canvas",
        timeMs: 100,
      }),
      /changed during frame/u,
    );
  } finally {
    globalThis.Module = priorModule;
    globalThis.document = priorDocument;
  }
});

test("WASM pumps RunTick until exactly one Present and emits common snapshot", async () => {
  const priorModule = globalThis.Module;
  const priorDocument = globalThis.document;
  let fakeTime = null;
  let presents = 9;
  let pumps = 0;
  const presentState = {
    sequence: 21,
    gameValid: 1,
    game: 7,
    sceneValid: 1,
    scene: 30004,
  };
  globalThis.Module = {
    ...fakePresentStateExports(() => presentState),
    _wyd_debug_set_fake_time(value) {
      fakeTime = value;
    },
    _wyd_debug_get_time() {
      return fakeTime;
    },
    _wyd_d3d9_present_calls() {
      return presents;
    },
    _wyd_tick_client() {
      pumps += 1;
      if (pumps === 3) {
        presentState.sequence += 1;
        presents += 1;
      }
      return 0;
    },
    _wyd_d3d9_gl_error_total() {
      return 0;
    },
  };
  globalThis.document = {
    querySelector(selector) {
      assert.equal(selector, "#canvas");
      return {
        width: 800,
        height: 600,
        toDataURL(type) {
          assert.equal(type, "image/png");
          return "data:image/png;base64,iVBORw0KGgo=";
        },
      };
    },
  };
  try {
    const result = await executeWasmTick({
      frameId: 11,
      maxPumps: 8,
      selector: "#canvas",
      timeMs: 48,
    });
    assert.equal(fakeTime, 48);
    assert.equal(result.snapshot.frame_id, 11);
    assert.equal(result.snapshot.state.game, 7);
    assert.equal(result.snapshot.state.scene, 30004);
    assert.equal(result.snapshot.render.present_before, 9);
    assert.equal(result.snapshot.render.present_after, 10);
    assert.equal(result.snapshot.render.gl_error_total, 0);
    assert.equal(result.snapshot.ticks.wasm_pump_count, 3);
    assert.equal(result.snapshot.ticks.wasm_pump_limit, 8);
    assert.equal(result.snapshot.extensions.wasm.run_tick_pumps, 3);
    assert.equal(result.snapshot.extensions.wasm.direct_state_navigation, false);
    assert.deepEqual(result.snapshot.extensions.wasm.present_state_latch, {
      capture_point: "after_EndScene_before_Present",
      sequence_before: 21,
      sequence_after: 22,
      sequence_delta: 1,
    });
  } finally {
    globalThis.Module = priorModule;
    globalThis.document = priorDocument;
  }
});

test("WASM frame rejects non-integer and nonzero gl_error_total", async (t) => {
  for (const [name, glErrorTotal, pattern] of [
    ["non-integer", "0", /must be an integer/u],
    ["nonzero", 1, /expected exactly 0/u],
  ]) {
    await t.test(name, async () => {
      const priorModule = globalThis.Module;
      const priorDocument = globalThis.document;
      let presents = 0;
      const presentState = {
        sequence: 0,
        gameValid: 1,
        game: 7,
        sceneValid: 1,
        scene: 30004,
      };
      globalThis.Module = {
        ...fakePresentStateExports(() => presentState),
        ...fakeClockExports(),
        _wyd_d3d9_present_calls() {
          return presents;
        },
        _wyd_d3d9_gl_error_total() {
          return glErrorTotal;
        },
        _wyd_tick_client() {
          presentState.sequence += 1;
          presents += 1;
          return 1;
        },
      };
      globalThis.document = {
        querySelector() {
          return {
            width: 800,
            height: 600,
            toDataURL() {
              throw new Error("PNG capture must not run after GL failure");
            },
          };
        },
      };
      try {
        await assert.rejects(
          executeWasmTick({
            frameId: 1,
            maxPumps: 2,
            selector: "#canvas",
            timeMs: 0,
          }),
          pattern,
        );
      } finally {
        globalThis.Module = priorModule;
        globalThis.document = priorDocument;
      }
    });
  }
});

test("a WASM tick with no Present is rejected", async () => {
  const priorModule = globalThis.Module;
  const priorDocument = globalThis.document;
  const presentState = {
    sequence: 4,
    gameValid: 0,
    game: 0,
    sceneValid: 0,
    scene: 0,
  };
  globalThis.Module = {
    ...fakePresentStateExports(() => presentState),
    ...fakeClockExports(),
    pumps: 0,
    _wyd_d3d9_present_calls() {
      return 3;
    },
    _wyd_tick_client() {
      this.pumps += 1;
      return 0;
    },
  };
  globalThis.document = {
    querySelector() {
      return {
        width: 800,
        height: 600,
        toDataURL() {
          return "data:image/png;base64,AA==";
        },
      };
    },
  };
  try {
    await assert.rejects(
      executeWasmTick({
        frameId: 1,
        maxPumps: 3,
        selector: "#canvas",
        timeMs: 0,
      }),
      /within 3 pumps/u,
    );
    assert.equal(globalThis.Module.pumps, 3);
  } finally {
    globalThis.Module = priorModule;
    globalThis.document = priorDocument;
  }
});

test("WASM snapshot records a state transition inside the tick before Present", async () => {
  const priorModule = globalThis.Module;
  const priorDocument = globalThis.document;
  let presents = 0;
  let gameState = 1;
  let sceneType = 30001;
  const presentState = {
    sequence: 0,
    gameValid: 0,
    game: 0,
    sceneValid: 0,
    scene: 0,
  };
  globalThis.Module = {
    ...fakePresentStateExports(() => presentState),
    ...fakeClockExports(),
    _wyd_d3d9_present_calls() {
      return presents;
    },
    _wyd_get_game_state() {
      return gameState;
    },
    _wyd_get_scene_type() {
      return sceneType;
    },
    _wyd_tick_client() {
      gameState = 7;
      sceneType = 30004;
      presentState.gameValid = 1;
      presentState.game = gameState;
      presentState.sceneValid = 1;
      presentState.scene = sceneType;
      presentState.sequence += 1;
      presents += 1;
      return 1;
    },
  };
  globalThis.document = {
    querySelector() {
      return {
        width: 800,
        height: 600,
        toDataURL() {
          return "data:image/png;base64,AA==";
        },
      };
    },
  };
  try {
    const result = await executeWasmTick({
      frameId: 1,
      maxPumps: 2,
      selector: "#canvas",
      timeMs: 0,
    });
    assert.deepEqual(result.snapshot.state, {
      game: 7,
      scene: 30004,
    });
    assert.equal(gameState, 7);
    assert.equal(sceneType, 30004);
  } finally {
    globalThis.Module = priorModule;
    globalThis.document = priorDocument;
  }
});

test("WASM snapshot keeps the state latched at Present when the tick mutates it afterward", async () => {
  const priorModule = globalThis.Module;
  const priorDocument = globalThis.document;
  let presents = 0;
  let gameState = -1;
  let sceneType = null;
  const presentState = {
    sequence: 8,
    gameValid: 0,
    game: 0,
    sceneValid: 0,
    scene: 0,
  };
  globalThis.Module = {
    ...fakePresentStateExports(() => presentState),
    ...fakeClockExports(),
    _wyd_d3d9_present_calls() {
      return presents;
    },
    _wyd_tick_client() {
      presentState.gameValid = 1;
      presentState.game = gameState;
      presentState.sceneValid = sceneType === null ? 0 : 1;
      presentState.scene = sceneType ?? 0;
      presentState.sequence += 1;
      presents += 1;

      gameState = 7;
      sceneType = 30004;
      return 1;
    },
  };
  globalThis.document = {
    querySelector() {
      return {
        width: 800,
        height: 600,
        toDataURL() {
          return "data:image/png;base64,AA==";
        },
      };
    },
  };
  try {
    const result = await executeWasmTick({
      frameId: 1,
      maxPumps: 2,
      selector: "#canvas",
      timeMs: 0,
    });
    assert.deepEqual(result.snapshot.state, {
      game: -1,
      scene: null,
    });
    assert.equal(gameState, 7);
    assert.equal(sceneType, 30004);
  } finally {
    globalThis.Module = priorModule;
    globalThis.document = priorDocument;
  }
});

test("a WASM pump that produces multiple Presents is rejected immediately", async () => {
  const priorModule = globalThis.Module;
  const priorDocument = globalThis.document;
  let presents = 4;
  const presentState = {
    sequence: 12,
    gameValid: 1,
    game: 7,
    sceneValid: 1,
    scene: 30004,
  };
  globalThis.Module = {
    ...fakePresentStateExports(() => presentState),
    ...fakeClockExports(),
    _wyd_d3d9_present_calls() {
      return presents;
    },
    _wyd_tick_client() {
      presentState.sequence += 2;
      presents += 2;
      return 0;
    },
  };
  globalThis.document = {
    querySelector() {
      return {
        width: 800,
        height: 600,
        toDataURL() {
          throw new Error("capture must not run");
        },
      };
    },
  };
  try {
    await assert.rejects(
      executeWasmTick({
        frameId: 1,
        maxPumps: 5,
        selector: "#canvas",
        timeMs: 0,
      }),
      /produced 2 Present calls during 1 pumps/u,
    );
  } finally {
    globalThis.Module = priorModule;
    globalThis.document = priorDocument;
  }
});
