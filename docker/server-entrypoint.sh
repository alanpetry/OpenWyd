#!/bin/sh
set -u

if [ "${OPENWYD_HOLD_ON_EXIT:-0}" != "1" ]; then
    exec "$@"
fi

"$@"
status=$?
printf '[openwyd] server process exited with status %s; diagnostic hold enabled\n' "$status" >&2
exec sleep infinity
