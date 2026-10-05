#!/bin/sh

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
dmesg -n 4

echo IOATC_TOMBSTONE_BOOT
mkdir -p /mnt/disk

n=0
while test ! -b /dev/nvme0n1 && test "$n" -lt 100; do
    sleep 0.1
    n=$((n + 1))
done

if ! mount /dev/nvme0n1 /mnt/disk; then
    echo IOATC_TOMBSTONE_NO_DISK
    poweroff -f
fi

echo IOATC_TOMBSTONE_START $(date +%s)
status_file=/tmp/ioatc-tombstone-status
: > "$status_file"

# Keep several DMA streams active so deferred unmaps generate selective
# invalidations while new translations continue to populate the IOATC.
for j in 1 2 3 4; do
    (
        if ! dd if=/dev/urandom of=/tmp/src$j bs=1M count=8 2>/dev/null; then
            echo JOB_${j}_SOURCE_FAILED >> "$status_file"
            exit
        fi

        n=0
        while test "$n" -lt 50; do
            if ! cp /tmp/src$j /mnt/disk/file$j ||
               ! sync ||
               ! cmp -s /tmp/src$j /mnt/disk/file$j; then
                echo JOB_${j}_FAILED_$n >> "$status_file"
                exit
            fi
            echo 3 > /proc/sys/vm/drop_caches
            n=$((n + 1))
        done

        rm -f /mnt/disk/file$j /tmp/src$j
        echo JOB_${j}_OK >> "$status_file"
    ) &
done
wait

cat "$status_file"
echo IOATC_TOMBSTONE_END $(date +%s)
dmesg | grep -Ei 'fault|warning|bug:|dma.*error|timeout|iommu.*error' || true
umount /mnt/disk
poweroff -f
