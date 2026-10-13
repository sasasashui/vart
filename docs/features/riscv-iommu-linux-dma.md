# Linux RISC-V IOMMU integration

VART advertises the PCI RISC-V IOMMU in the virt machine device tree only
when the machine is created with `enable_riscv_iommu`.  The PCI host node then
contains the QEMU-compatible `iommu-map` property and an in-host
`riscv,pci-iommu` child node.  Requester IDs are mapped while excluding the
IOMMU function itself.

Virtio PCI requesters are attached to separate VART IOMMU contexts.  The
contexts are initially identity contexts and are updated from the guest DDT
when Linux issues DDTP and IODIR commands.  VART currently advertises the
base 32-byte device-context format; the DDT walker therefore uses the base
format partitions (7/9/8 bits) and 32-byte leaf contexts.

The first Linux integration check uses the Linux 7.3-rc5 image built from
`linux-iommu/next` with the RISC-V IOMMU DMA and MSI-remapping series.  It
verifies PCI enumeration, IOMMU probe, and virtio device initialization.  A
full DMA workload remains a separate follow-up: the current integration test
does not yet submit a block or network request after the guest programs its
device contexts.
