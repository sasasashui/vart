#!/bin/sh

set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 VART IMAGE INITRAMFS" >&2
    exit 2
fi

log=$(mktemp /tmp/vart-linux-early-boot-XXXXXX)
trap 'rm -f "$log"' EXIT

status=0
timeout 5 "$1" --kernel "$2" --initrd "$3" --memory 512M --cpus 1 \
    --append "earlycon=uart8250,mmio,0x10000000 console=ttyS0 loglevel=7" \
    >"$log" 2>&1 || status=$?

if [ "$status" -ne 124 ]; then
    cat "$log" >&2
    echo "not ok - Linux early boot exited with status $status" >&2
    exit 1
fi

for marker in \
    "Booting Linux on hartid 0" \
    "Linux version 6.18.3" \
    "Machine model: VART RISC-V virtual machine" \
    "SBI TIME extension detected" \
    "sched_clock: 64 bits at 24MHz" \
    "smp: Brought up 1 node, 1 CPU" \
    "Memory: "
do
    if ! grep -F "$marker" "$log" >/dev/null; then
        cat "$log" >&2
        echo "not ok - missing Linux marker: $marker" >&2
        exit 1
    fi
done

if grep -E "Kernel panic|Oops:|BUG:" "$log" >/dev/null; then
    cat "$log" >&2
    echo "not ok - Linux reported a fatal error" >&2
    exit 1
fi

echo "ok - boot Linux 6.18.3 through CPU, memory, and timer setup"
