#!/bin/sh

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
dmesg -n 4

echo FIO_IOATC_BOOT
mkdir -p /mnt/disk

n=0
while test ! -b /dev/nvme0n1 && test "$n" -lt 100; do
    sleep 0.1
    n=$((n + 1))
done

if ! mount /dev/nvme0n1 /mnt/disk; then
    echo FIO_IOATC_NO_DISK
    poweroff -f
fi

export LD_LIBRARY_PATH=/usr/lib/riscv64-linux-gnu:/usr/lib/riscv64-linux-gnu/ceph

echo FIO_IOATC_START $(date +%s)
/usr/bin/fio \
    --name=ioatc \
    --directory=/mnt/disk \
    '--filename_format=fio.$jobnum' \
    --size=128m \
    --rw=randrw \
    --rwmixread=30 \
    --bs=4k \
    --direct=1 \
    --ioengine=libaio \
    --iodepth=64 \
    --numjobs=4 \
    --time_based=1 \
    --runtime=60 \
    --ramp_time=5 \
    --group_reporting=1 \
    --eta=never
status=$?
echo FIO_IOATC_STATUS $status
echo FIO_IOATC_END $(date +%s)
dmesg | grep -Ei 'fault|warning|bug:|dma.*error|timeout|iommu.*error' || true

rm -f /mnt/disk/fio.*
sync
umount /mnt/disk
poweroff -f
