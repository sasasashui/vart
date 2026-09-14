#!/bin/sh

set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 VART IMAGE INITRAMFS" >&2
    exit 2
fi

log=$(mktemp /tmp/vart-linux-interactive-XXXXXX)
trap 'rm -f "$log"' EXIT

status=0
printf '%s\n' "printf 'VART_%s_OK\\n' INTERACTIVE; poweroff -f" |
    timeout 10 "$1" --kernel "$2" --initrd "$3" \
        --memory 512M --cpus 1 \
        --append "earlycon=uart8250,mmio,0x10000000 console=ttyS0" \
        >"$log" 2>&1 || status=$?

if [ "$status" -ne 0 ] ||
   ! grep -F "VART_INTERACTIVE_OK" "$log" >/dev/null ||
   ! grep -F "reboot: Power down" "$log" >/dev/null; then
    cat "$log" >&2
    echo "not ok - Linux interactive console exited with status $status" >&2
    exit 1
fi

echo "ok - interact with Linux through the production console"
