#!/usr/bin/env node

/*
 * Exact WebGL canvas capture for the OpenWyd comparison controller.
 *
 * This intentionally does not use Playwright's screenshot API.  A screenshot
 * observes the CSS/composited page; this helper asks the 800x600 backing canvas
 * itself for PNG bytes through toBlob() (with toDataURL() only as a fallback).
 */

import { writeFile } from "node:fs/promises";
import { createRequire } from "node:module";
import path from "node:path";
import { pathToFileURL } from "node:url";

const FRAME_SCHEMA = "openwyd.debug-frame";
const FRAME_SCHEMA_VERSION = 1;

function fail(message) {
  process.stderr.write(`capture error: ${message}\n`);
  process.exitCode = 2;
}

function parseArguments(argv) {
  const options = {
    browser: "chromium",
    frameId: "0",
    headless: true,
    height: 600,
    selector: "canvas",
    settleFrames: 1,
    timeoutMs: 30000,
    width: 800,
    launchArgs: [],
  };

  const names = new Map([
    ["--browser", "browser"],
    ["--frame-id", "frameId"],
    ["--height", "height"],
    ["--metadata-expression", "metadataExpression"],
    ["--metadata-output", "metadataOutput"],
    ["--output", "output"],
    ["--selector", "selector"],
    ["--settle-frames", "settleFrames"],
    ["--timeout-ms", "timeoutMs"],
    ["--url", "url"],
    ["--wait-expression", "waitExpression"],
    ["--width", "width"],
  ]);

  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === "--headful") {
      options.headless = false;
      continue;
    }
    if (argument === "--launch-arg") {
      index += 1;
      if (index >= argv.length) {
        throw new Error("--launch-arg requires a value");
      }
      options.launchArgs.push(argv[index]);
      continue;
    }
    const name = names.get(argument);
    if (!name) {
      throw new Error(`unknown argument: ${argument}`);
    }
    index += 1;
    if (index >= argv.length) {
      throw new Error(`${argument} requires a value`);
    }
    options[name] = argv[index];
  }

  for (const name of ["height", "settleFrames", "timeoutMs", "width"]) {
    options[name] = Number.parseInt(options[name], 10);
    if (!Number.isInteger(options[name]) || options[name] < 0) {
      throw new Error(`${name} must be a non-negative integer`);
    }
  }
  if (!options.url || !options.output || !options.metadataOutput) {
    throw new Error("--url, --output and --metadata-output are required");
  }
  if (options.width !== 800 || options.height !== 600) {
    throw new Error("OpenWyd comparison captures require an 800x600 backing canvas");
  }
  if (options.timeoutMs <= 0) {
    throw new Error("timeoutMs must be greater than zero");
  }
  return options;
}

function objectOrEmpty(value) {
  return value && typeof value === "object" && !Array.isArray(value) ? value : {};
}

function normalizeFrameRecord(sourceValue, options, captureInfo) {
  const source = objectOrEmpty(sourceValue);
  const sourceExtensions = objectOrEmpty(source.extensions);
  const knownFields = new Set([
    "schema",
    "schema_version",
    "frame_id",
    "state",
    "ticks",
    "clock",
    "camera",
    "matrices",
    "draws",
    "render",
    "network",
    "extensions",
  ]);
  const producerExtra = {};
  for (const [key, value] of Object.entries(source)) {
    if (!knownFields.has(key)) {
      producerExtra[key] = value;
    }
  }

  const draws = Array.isArray(source.draws)
    ? source.draws.filter((entry) => entry && typeof entry === "object" && !Array.isArray(entry))
    : [];

  return {
    schema: FRAME_SCHEMA,
    schema_version: FRAME_SCHEMA_VERSION,
    frame_id: source.frame_id ?? options.frameId,
    state: source.state ?? null,
    ticks: objectOrEmpty(source.ticks),
    clock: objectOrEmpty(source.clock),
    camera: objectOrEmpty(source.camera),
    matrices: objectOrEmpty(source.matrices),
    draws,
    render: {
      ...objectOrEmpty(source.render),
      backing_height: captureInfo.backingHeight,
      backing_width: captureInfo.backingWidth,
    },
    network: objectOrEmpty(source.network),
    extensions: {
      ...sourceExtensions,
      canvas_capture: {
        css_height: captureInfo.cssHeight,
        css_width: captureInfo.cssWidth,
        encoding: captureInfo.encoding,
        selector: options.selector,
      },
      ...(Object.keys(producerExtra).length > 0
        ? { producer_extra: producerExtra }
        : {}),
    },
  };
}

async function main() {
  const options = parseArguments(process.argv.slice(2));

  // The repository keeps Playwright under webclient/node_modules. Resolving
  // from cwd lets the controller select that source-managed Node workspace
  // without copying or globally installing the package.
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
      if (error?.code !== "ERR_MODULE_NOT_FOUND") {
        throw error;
      }
    }
  }
  const browser = await browserType.launch(launchOptions);
  try {
    const page = await browser.newPage({
      viewport: { width: options.width, height: options.height },
    });
    page.setDefaultTimeout(options.timeoutMs);
    await page.goto(options.url, {
      timeout: options.timeoutMs,
      waitUntil: "domcontentloaded",
    });
    if (options.waitExpression) {
      await page.waitForFunction(options.waitExpression, null, {
        timeout: options.timeoutMs,
      });
    }
    await page.waitForSelector(options.selector, {
      state: "attached",
      timeout: options.timeoutMs,
    });
    for (let index = 0; index < options.settleFrames; index += 1) {
      await page.evaluate(
        () => new Promise((resolve) => requestAnimationFrame(() => resolve())),
      );
    }

    const capture = await page.evaluate(
      async ({ expectedHeight, expectedWidth, metadataExpression, selector }) => {
        const canvas = document.querySelector(selector);
        if (!(canvas instanceof HTMLCanvasElement)) {
          throw new Error(`selector does not resolve to an HTMLCanvasElement: ${selector}`);
        }
        if (canvas.width !== expectedWidth || canvas.height !== expectedHeight) {
          throw new Error(
            `backing canvas is ${canvas.width}x${canvas.height}; ` +
              `expected ${expectedWidth}x${expectedHeight}`,
          );
        }

        // Request serialization inside the next animation-frame callback. This
        // observes the backing store after the application's already-queued
        // draw callback and before the browser composites/clears a WebGL
        // drawing buffer created with preserveDrawingBuffer=false.
        const serialized = await new Promise((resolve, reject) => {
          requestAnimationFrame(() => {
            let sourceMetadata = {};
            try {
              sourceMetadata = metadataExpression
                ? (0, eval)(metadataExpression)
                : {};
            } catch (error) {
              reject(error);
              return;
            }
            canvas.toBlob((blob) => {
              if (!blob) {
                resolve({ dataUrl: null, encoding: "toBlob", sourceMetadata });
                return;
              }
              const reader = new FileReader();
              reader.onerror = () => reject(reader.error ?? new Error("FileReader failed"));
              reader.onload = () =>
                resolve({
                  dataUrl: reader.result,
                  encoding: "toBlob",
                  sourceMetadata,
                });
              reader.readAsDataURL(blob);
            }, "image/png");
          });
        });
        let { dataUrl, encoding, sourceMetadata } = serialized;
        if (!dataUrl) {
          encoding = "toDataURL";
          dataUrl = canvas.toDataURL("image/png");
        }
        if (typeof dataUrl !== "string" || !dataUrl.startsWith("data:image/png;base64,")) {
          throw new Error("canvas did not produce a base64 PNG data URL");
        }
        const bounds = canvas.getBoundingClientRect();
        return {
          backingHeight: canvas.height,
          backingWidth: canvas.width,
          cssHeight: bounds.height,
          cssWidth: bounds.width,
          dataUrl,
          encoding,
          sourceMetadata,
        };
      },
      {
        expectedHeight: options.height,
        expectedWidth: options.width,
        metadataExpression: options.metadataExpression ?? "",
        selector: options.selector,
      },
    );

    const pngBase64 = capture.dataUrl.slice(capture.dataUrl.indexOf(",") + 1);
    await writeFile(options.output, Buffer.from(pngBase64, "base64"));

    const frameRecord = normalizeFrameRecord(
      capture.sourceMetadata,
      options,
      capture,
    );
    await writeFile(
      options.metadataOutput,
      `${JSON.stringify(frameRecord, null, 2)}\n`,
      "utf8",
    );

    process.stdout.write(
      `${JSON.stringify({
        backing_height: capture.backingHeight,
        backing_width: capture.backingWidth,
        encoding: capture.encoding,
        frame_id: frameRecord.frame_id,
        metadata_output: path.resolve(options.metadataOutput),
        output: path.resolve(options.output),
        selector: options.selector,
      })}\n`,
    );
  } finally {
    await browser.close();
  }
}

main().catch((error) => {
  fail(error instanceof Error ? error.stack ?? error.message : String(error));
});
