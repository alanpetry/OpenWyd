import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const WEBCLIENT_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const PLAYWRIGHT_CACHE = path.join(WEBCLIENT_ROOT, ".cache", "ms-playwright");

function versionedDirs(prefix) {
  if (!fs.existsSync(PLAYWRIGHT_CACHE)) return [];
  return fs
    .readdirSync(PLAYWRIGHT_CACHE, { withFileTypes: true })
    .filter((entry) => entry.isDirectory() && entry.name.startsWith(`${prefix}-`))
    .map((entry) => entry.name)
    .sort((a, b) => {
      const av = Number.parseInt(a.slice(prefix.length + 1), 10) || 0;
      const bv = Number.parseInt(b.slice(prefix.length + 1), 10) || 0;
      return bv - av;
    });
}

function firstExisting(paths) {
  return paths.find((candidate) => candidate && fs.existsSync(candidate)) || null;
}

export function portableChromiumExecutable() {
  if (process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH) {
    return process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH;
  }

  const headlessCandidates = versionedDirs("chromium_headless_shell").flatMap((dir) => [
    path.join(PLAYWRIGHT_CACHE, dir, "chrome-mac", "headless_shell"),
    path.join(PLAYWRIGHT_CACHE, dir, "chrome-linux", "headless_shell"),
    path.join(PLAYWRIGHT_CACHE, dir, "chrome-win", "headless_shell.exe"),
  ]);
  const bundledHeadless = firstExisting(headlessCandidates);
  if (bundledHeadless) return bundledHeadless;

  const chromiumCandidates = versionedDirs("chromium").flatMap((dir) => [
    path.join(PLAYWRIGHT_CACHE, dir, "chrome-mac", "Chromium.app", "Contents", "MacOS", "Chromium"),
    path.join(PLAYWRIGHT_CACHE, dir, "chrome-linux", "chrome"),
    path.join(PLAYWRIGHT_CACHE, dir, "chrome-win", "chrome.exe"),
  ]);
  return firstExisting(chromiumCandidates);
}

export function chromiumLaunchOptions(extra = {}) {
  const executablePath = portableChromiumExecutable();
  return executablePath ? { ...extra, executablePath } : { ...extra };
}
