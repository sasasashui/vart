# Linux does not enumerate a standalone virtio-net device in PCI slot 1

## Symptom

An unprivileged Linux smoke test booted successfully with the VART virtio-net
backend, but Linux did not create a PCI or network device when virtio-net was
the only PCI endpoint. The guest still discovered the PCI host bridge, while
`/sys/bus/pci/devices` and `/sys/class/net/eth0` were empty.

## Reproduction

Start VART with an inherited socketpair descriptor as the virtio-net backend,
without a virtio-blk device, and boot either the Linux 6.18.3 or 7.3-rc2 image.
The backend does not require TAP setup or root privileges. In the guest, check:

```text
ls /sys/bus/pci/devices
ls /sys/bus/virtio/devices
ls /sys/class/net
```

With the original layout, virtio-net was assigned to `00:01.0` and none of
these commands showed the network device.

## Investigation

Temporary tracing in `pci_ecam_read()` showed that Linux performed an ECAM
read for bus 0, slot 0, then stopped scanning when that slot returned an empty
vendor ID. It did not issue a read for slot 1. Placing the same virtio-net
device at slot 0 caused Linux to enumerate it and bind `virtio_net`.

The VART ECAM dispatch and virtio-net PCI configuration were correct; the
failure was the device placement relative to the Linux generic PCI host scan.

## Fix

The virtual machine now assigns PCI slots as follows:

* virtio-blk uses slot 0 when present;
* virtio-net uses slot 0 when it is the only virtio device;
* virtio-net uses slot 1 when virtio-blk occupies slot 0.

This keeps the first enumerated slot populated while preserving a stable
layout when both devices are enabled.

## Regression coverage

The KVM virtio-net machine test checks that a standalone network device is
attached at slot 0. Linux 6.18.3 and 7.3-rc2 smoke boots then report:

```text
virtio0
virtio_net
52:54:00:12:34:57
```

The test uses an inherited socketpair backend and shuts the guest down through
the initramfs shell. It does not modify host networking and does not require
`sudo`.
