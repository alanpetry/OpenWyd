#!/usr/bin/env bash

if command -v em++ >/dev/null 2>&1; then
  return 0
fi

EMSDK_ROOT="${EMSDK:-$HOME/.cache/emsdk}"
EMSDK_ENV="$EMSDK_ROOT/emsdk_env.sh"

if [ -f "$EMSDK_ENV" ]; then
  # shellcheck disable=SC1090
  EMSDK_QUIET=1 source "$EMSDK_ENV" >/dev/null
fi

if ! command -v em++ >/dev/null 2>&1; then
  echo "[emsdk] em++ não encontrado. Instale/ative o emsdk local em $EMSDK_ROOT." >&2
  return 1
fi
