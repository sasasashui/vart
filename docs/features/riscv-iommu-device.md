# VART guest-visible RISC-V IOMMU device

`VartRiscvIommu` is a small PCIe function that follows the register offsets
defined by the RISC-V IOMMU specification and QEMU's `riscv-iommu-pci` model.
It uses vendor ID `0x1b36`, device ID `0x0014`, class `0x0806`, and a 4 KiB
64-bit BAR.

The implemented registers are capability, feature control, DDTP, command queue
base/head/tail, queue CSRs, and the fault/page-request queue placeholders.  The
command queue contains 16-byte little-endian commands in guest RAM.  The
initial command path handles `IOTINVAL.VMA` (page-selective or all-IOTLB) and
`IOFENCE.C`; unsupported commands advance the head and set `CQCSR.CMD_ILL`.

The device's software context pointer is non-owning and must be configured
before PCI realization.  IOTINVAL commands call the existing context
invalidation API, so cache lifetime and locking remain in the IOMMU translation
context rather than being duplicated in the PCI register device.  The focused
test in `tests/unit/iommu/device.c` verifies ECAM identity, BAR access,
capability reporting, queue DMA, invalidation, head advancement, and reset.

This stage does not claim full Linux driver compatibility.  Device-directory
table walking, automatic requester-ID context management, fault recording, and
interrupt delivery are intentionally deferred to IOMMU-5 and IOMMU-6.
