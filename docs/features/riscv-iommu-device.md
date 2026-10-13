# VART guest-visible RISC-V IOMMU device

`VartRiscvIommu` is a small PCIe function that follows the register offsets
defined by the RISC-V IOMMU specification and QEMU's `riscv-iommu-pci` model.
It uses vendor ID `0x1b36`, device ID `0x0014`, class `0x0806`, and a 4 KiB
64-bit BAR.

The implemented registers are capability, feature control, DDTP, command queue
base/head/tail, fault queue base/head/tail, queue CSRs, and the fault/page-
request queue placeholders.  The command queue contains 16-byte little-endian
commands in guest RAM.  The initial command path handles `IOTINVAL.VMA`
(page-selective or all-IOTLB) and `IOFENCE.C`; unsupported commands advance the
head and set `CQCSR.CMD_ILL`.

Translation faults are delivered through a non-owning context callback.  BAR0
offsets `0x200` and `0x208` expose the last fault address and compact fault
metadata, while `0x210` is a write-one-to-clear pending status register.  When
enabled, the fault queue writes 32-byte records containing the cause, requester
ID, fault address, and access metadata, and reports queue overflow or memory
fault through `FQCSR`.  MSI-X vector 0 reports translation faults and vector 1
reports command-queue errors; the normal VART MSI-X injector selects APLIC/IMSIC
or irqfd delivery.

The device's software context pointer is non-owning and must be configured
before PCI realization.  IOTINVAL commands call the existing context
invalidation API, so cache lifetime and locking remain in the IOMMU translation
context rather than being duplicated in the PCI register device.  The focused
test in `tests/unit/iommu/device.c` verifies ECAM identity, BAR access,
capability reporting, queue DMA, invalidation, head advancement, and reset.

For requester management, DDTP modes `1LVL`, `2LVL`, and `3LVL` are walked for
each software-bound PCI requester.  In `1LVL`, the requester ID selects a
64-byte device context directly.  In `2LVL` and `3LVL`, the upper requester-ID
fields select one or two valid directory DTEs, followed by the leaf device
context.  Each binding owns a separate `VartIommuContext`, so a single IOMMU
can translate multiple PCI devices and rejects duplicate requester IDs.
Process-directory tables and automatic requester-ID context allocation remain
outside this small model.

This stage does not claim full Linux driver compatibility.  Interrupt cause
queues and Linux-specific fault reporting remain deferred to IOMMU-6.
