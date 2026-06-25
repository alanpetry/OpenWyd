#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT_DIR"

WIDTH="${SELECT_SERVER_SNAPSHOT_WIDTH:-1280}"
HEIGHT="${SELECT_SERVER_SNAPSHOT_HEIGHT:-720}"
FRAMES="${SELECT_SERVER_SNAPSHOT_FRAMES:-180}"
FRAME_MS="${SELECT_SERVER_SNAPSHOT_FRAME_MS:-16.6667}"
OUT_DIR="${SELECT_SERVER_SNAPSHOT_OUT_DIR:-webclient/client-wasm/build/reports/select-server-snapshot}"

bash webclient/client-wasm/tools/run_tmproject_wasm_objects.sh
bash webclient/client-wasm/tools/run_tmproject_wasm_startup_link.sh

node webclient/client-wasm/tools/capture_select_server_snapshot.js \
  --width "$WIDTH" \
  --height "$HEIGHT" \
  --frames "$FRAMES" \
  --frame-ms "$FRAME_MS" \
  --out-dir "$OUT_DIR" \
  "$@"
