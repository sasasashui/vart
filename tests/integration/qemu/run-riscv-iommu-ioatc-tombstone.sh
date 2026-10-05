#!/bin/sh

set -eu

if test "$#" -ne 1; then
    echo "usage: QEMU=... KERNEL=... ROOTFS=... DISK=... $0 OUTPUT_DIR" >&2
    exit 2
fi

: "${QEMU:?set QEMU to the instrumented qemu-system-riscv64}"
: "${KERNEL:?set KERNEL to the guest Image}"
: "${ROOTFS:?set ROOTFS to the initramfs staging directory}"
: "${DISK:?set DISK to the NVMe filesystem image}"

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output_dir=$(mkdir -p "$1" && CDPATH= cd -- "$1" && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/ioatc-tombstone.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

qemu=$(realpath "$QEMU")
kernel=$(realpath "$KERNEL")
rootfs=$(realpath "$ROOTFS")
disk=$(realpath "$DISK")
initrd=$work_dir/rootfs.cpio.gz
trace=$output_dir/ioatc-tombstone.trace
console=$output_dir/ioatc-tombstone-console.log
csv=$output_dir/ioatc-tombstone.csv

cp -a "$rootfs/." "$work_dir/rootfs"
cp "$script_dir/riscv-iommu-ioatc-tombstone-init.sh" "$work_dir/rootfs/init"
chmod 0755 "$work_dir/rootfs/init"
(
    cd "$work_dir/rootfs"
    find . -print0 | cpio --null -o --format=newc 2>/dev/null | gzip -9 > "$initrd"
)

timeout "${TIMEOUT:-240}" "$qemu" \
    -machine virt,aia=aplic-imsic \
    -accel kvm \
    -cpu host \
    -m 1G \
    -smp 4 \
    -nographic \
    -kernel "$kernel" \
    -initrd "$initrd" \
    -append "console=ttyS0 rdinit=/init nokaslr iommu.strict=0" \
    -device riscv-iommu-pci,x-ioatc-stat-interval="${STAT_INTERVAL:-128}" \
    -drive file="$disk",if=none,format=raw,id=blk0 \
    -device nvme,drive=blk0,serial=ioatc-test \
    -trace enable=riscv_iommu_ioatc_stats,file="$trace" \
    > "$console" 2>&1

"$script_dir/analyze-riscv-iommu-ioatc-trace.awk" "$trace" > "$csv"
tail -20 "$console"
