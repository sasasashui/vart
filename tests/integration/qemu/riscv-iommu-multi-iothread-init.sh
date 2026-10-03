#!/bin/sh

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
dmesg -n 4

echo MULTI_IOTHREAD_BOOT
status_file=/tmp/multi-iothread-status
: > "$status_file"

index=0
for dev in vda vdb vdc vdd; do
    index=$((index + 1))
    n=0
    while test ! -b /dev/$dev && test "$n" -lt 100; do
        sleep 0.1
        n=$((n + 1))
    done

    mkdir -p /mnt/disk$index
    if ! mount /dev/$dev /mnt/disk$index; then
        echo DISK_${index}_MOUNT_FAILED >> "$status_file"
    fi
done

echo MULTI_IOTHREAD_START $(date +%s)
for j in 1 2 3 4; do
    (
        if ! dd if=/dev/urandom of=/tmp/src$j bs=1M count=8 2>/dev/null; then
            echo JOB_${j}_SOURCE_FAILED >> "$status_file"
            exit
        fi

        n=0
        while test "$n" -lt 50; do
            if ! cp /tmp/src$j /mnt/disk$j/file$j ||
               ! sync ||
               ! cmp -s /tmp/src$j /mnt/disk$j/file$j; then
                echo JOB_${j}_FAILED_$n >> "$status_file"
                exit
            fi
            echo 3 > /proc/sys/vm/drop_caches
            n=$((n + 1))
        done

        rm -f /mnt/disk$j/file$j /tmp/src$j
        echo JOB_${j}_OK >> "$status_file"
    ) &
done
wait

cat "$status_file"
echo MULTI_IOTHREAD_END $(date +%s)
cat /proc/interrupts | grep -E 'virtio|vda|vdb|vdc|vdd'
dmesg | grep -Ei 'fault|warning|bug:|dma.*error|timeout|iommu.*error' || true

for j in 1 2 3 4; do
    umount /mnt/disk$j
done
poweroff -f
