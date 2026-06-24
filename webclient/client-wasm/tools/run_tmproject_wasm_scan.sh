#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"

python3 webclient/client-wasm/tools/extract_vcxproj_sources.py \
  --vcxproj Projects/TMProject/TMProject.vcxproj \
  --out webclient/client-wasm/build/reports/tmproject-sources.json

python3 webclient/client-wasm/tools/build_tmproject_wasm.py \
  --repo-root . \
  --vcxproj Projects/TMProject/TMProject.vcxproj \
  --report-json webclient/client-wasm/build/reports/tmproject-wasm-scan.json \
  --report-md webclient/client-wasm/build/reports/tmproject-wasm-scan.md
