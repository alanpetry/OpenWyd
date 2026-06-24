#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WEBCLIENT_DIR="$ROOT_DIR/webclient"
SRC_FILE="$WEBCLIENT_DIR/wasm/src/wyd_core.cpp"
OUT_DIR="$WEBCLIENT_DIR/app/wasm"
OUT_JS="$OUT_DIR/wyd_core.js"

source "$WEBCLIENT_DIR/tools/emsdk_bootstrap.sh"

mkdir -p "$OUT_DIR"

em++ \
  "$SRC_FILE" \
  -O3 \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web \
  -sEXPORTED_FUNCTIONS='["_malloc","_free","_wyd_parse_msh_header","_wyd_parse_trn_overview"]' \
  -sEXPORTED_RUNTIME_METHODS='["HEAPU8"]' \
  -o "$OUT_JS"

echo "[build_wasm] Gerado:"
echo "  - $OUT_JS"
echo "  - ${OUT_JS%.js}.wasm"
