# virtio-blk DMA stress failure

## Classification

This was a VART device-model bug. It was not a Linux or QEMU upstream bug.

## Symptom

Booting the Linux IOMMU test kernel with a 64 MiB ext4 virtio-blk disk and
writing a file with `dd` produced `I/O error` after roughly 13 MiB. The guest
reported failed writes even though the disk still had free space.

## Reproducer

The following workload exercises guest-to-host and host-to-guest DMA through
the virtio-blk request queue:

```sh
truncate -s 64M /tmp/vart-virtio-dma.img
mkfs.ext4 -F -q /tmp/vart-virtio-dma.img

printf '%s\n' \
  'mkdir -p /mnt' \
  'mount -t ext4 /dev/vda /mnt' \
  'dd if=/dev/zero of=/mnt/dma-test bs=4096 count=4096 conv=fsync' \
  'dd if=/mnt/dma-test of=/dev/null bs=4096' \
  'echo VART_VIRTIO_DMA_OK' \
  'umount /mnt' \
  'poweroff -f' |
timeout 90 ./build/vart \
  --kernel ../linux-build-iommu-v6/arch/riscv/boot/Image \
  --initrd ../rootfs.cpio.gz --memory 512M --cpus 1 \
  --virtio-blk /tmp/vart-virtio-dma.img \
  --append 'earlycon=uart8250,mmio,0x10000000 console=ttyS0'
```

## Root cause

VART exposed `size_max = 1 MiB` and `seg_max = 128` in the virtio-blk
configuration space, but did not advertise `VIRTIO_BLK_F_SIZE_MAX` or
`VIRTIO_BLK_F_SEG_MAX`. Linux therefore did not apply the advertised segment
limit and submitted a 2 MiB data descriptor. VART's internal 1 MiB validation
rejected the descriptor and completed it with `VIRTIO_BLK_S_IOERR`.

## Fix and validation

VART now advertises both feature bits consistently with the configuration
fields. Linux then limits each data descriptor to 1 MiB, and the same workload
completed successfully. A three-iteration write/read workload also completed
without guest I/O errors.
