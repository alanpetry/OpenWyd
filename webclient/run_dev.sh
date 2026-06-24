#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VENV_PATH="${WYD_WEB_VENV:-$ROOT_DIR/webclient/.venv}"
HOST="${WYD_WEB_HOST:-0.0.0.0}"
PORT="${WYD_WEB_PORT:-8765}"

cd "$ROOT_DIR"

python3 webclient/tools/generate_asset_manifest.py

if [[ ! -d "$VENV_PATH" ]]; then
  python3 -m venv "$VENV_PATH"
fi

# shellcheck source=/dev/null
source "$VENV_PATH/bin/activate"
echo "Instalando dependências Python (primeira execução pode demorar)..."
python -m pip install -r webclient/server/requirements.txt

export WYD_WEB_HOST="$HOST"
export WYD_WEB_PORT="$PORT"

echo "WYD webclient em http://$HOST:$PORT"
echo "Para parar: Ctrl+C"

exec python webclient/server/app.py
