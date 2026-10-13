# Linux IOMMU integration findings

## Initial probe failures

The first Linux integration run used the 7.3-rc5 guest containing the RISC-V
IOMMU DMA and MSI-remapping series.  PCI enumeration succeeded, but the guest
stopped during `iommu_pci` probe because VART returned `-EINVAL` for register
accesses that the Linux driver performs during normal initialization:

* `ICVEC` at offset `0x2f8` was not implemented.
* The queue-size limit was eight address bits, while Linux probes and enables
  the default 8192-entry command and fault queues (13 address bits).
* Linux clears interrupt-pending state through `IPSR` and writes the page
  request queue control register even when page requests are not enabled.

These were VART compatibility gaps, not Linux or upstream QEMU bugs.  The
minimal fixes added the missing register paths, raised the queue limit to 13,
and accepted the no-op page-request control write without claiming page
request processing support.

## Reproduction

From the VART tree, with `linux-build-iommu-v6` and `rootfs.cpio.gz` present:

```sh
make check-linux-iommu-v6-pcie-switch-net
```

The successful run reports `VART_IOMMU_PCI_OK` from the guest shell and ends
with the normal VART Linux integration result.  The test also checks the
existing PCIe switch, virtio-blk, and virtio-net topology in the same boot.
