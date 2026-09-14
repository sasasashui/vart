#!/bin/sh

set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 VART UART-GUEST" >&2
    exit 2
fi

log=$(mktemp /tmp/vart-runtime-error-XXXXXX)
trap 'rm -f "$log"' EXIT

status=0
"$1" --kernel /no/such/vart-kernel --memory 64M \
    </dev/null >"$log" 2>&1 || status=$?
if [ "$status" -ne 1 ] ||
   ! grep -F "vart: boot resource loading failed:" "$log" >/dev/null; then
    cat "$log" >&2
    echo "not ok - boot-file failure cleanup" >&2
    exit 1
fi

status=0
"$1" --kernel "$2" --memory 64M --test-device \
    </dev/null 1</dev/null 2>"$log" || status=$?
if [ "$status" -ne 1 ] ||
   ! grep -F "vart: console output failed:" "$log" >/dev/null; then
    cat "$log" >&2
    echo "not ok - console failure cleanup" >&2
    exit 1
fi

status=0
"$1" --kernel "$2" --memory 64M --test-device \
    <&- >"$log" 2>&1 || status=$?
if [ "$status" -ne 0 ] ||
   ! grep -F "Hello from VART UART" "$log" >/dev/null; then
    cat "$log" >&2
    echo "not ok - closed standard-input recovery" >&2
    exit 1
fi

echo "ok - clean up runtime error paths"
