# Machine context

VART currently models one virtual machine per process.  The process-wide
`vart_system` object owns system-level address spaces and is initialized and
destroyed by `VartVirtMachine`.  This keeps machine-wide state in one place
without passing the same address-space pointer through every high-level call.

Subsystem APIs still receive an explicit `VartAddressSpace *` when they operate
on an address space.  This is intentional: PCIe, device DMA, and a future
IOMMU will introduce additional address spaces that must not be confused with
the system address space.

When PCIe is added, its root bus and other process-wide machine objects can be
added to `VartSystem` while device-local state remains owned by each device.
