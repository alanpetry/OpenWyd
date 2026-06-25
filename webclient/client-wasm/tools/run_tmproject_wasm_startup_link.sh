#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT_DIR"

python3 webclient/client-wasm/tools/check_startup_exports.py

source webclient/tools/emsdk_bootstrap.sh

python3 webclient/client-wasm/tools/link_tmproject_wasm_startup.py "$@"