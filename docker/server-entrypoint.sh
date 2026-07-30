#!/bin/sh
set -u

if [ "${OPENWYD_HOLD_ON_EXIT:-0}" != "1" ]; then
    exec "$@"
fi

diagnostic_dir=/opt/openwyd/Server/Common/diagnostics
diagnostic_name=$(basename "$1")
mkdir -p "$diagnostic_dir"
diagnostic_log="$diagnostic_dir/$diagnostic_name.log"

"$@" >"$diagnostic_log" 2>&1
status=$?
cat "$diagnostic_log" >&2
printf '[openwyd] server process exited with status %s; diagnostic hold enabled\n' "$status" >&2
exec sleep infinity
