# PCI bus mastering and DMA

Stage 11.1 adds the first DMA path for PCI devices.

## Ownership and address translation

Each PCI device uses `VartDevice.dma_as` as the address space for bus
mastering. A device added to a PCI bus inherits that bus address space when it
does not provide one explicitly. This keeps the translation boundary attached
to the device so an IOMMU address space can be installed later without
changing device code.

The current implementation is an identity mapping. A RAM-backed
`VartAddressRegion` exposes the host backing allocation through the address
space buffer helpers. MMIO regions are intentionally rejected by the buffer
helpers; the first DMA users only need guest RAM, and this avoids silently
turning a bulk DMA access into a sequence of device register accesses.

## Bus mastering

The PCI Command register is decoded by the generic configuration path. The
Memory Space Enable and Bus Master Enable bits are reflected in the device
state. DMA reads and writes require Bus Master Enable and return `-EPERM`
while bus mastering is disabled.

## Validation

`tests/unit/pci/dma.c` verifies command-register state transitions, DMA read and
write round trips, unmapped-address rejection, and disabling bus mastering.
The virt machine maps its KVM RAM into the system address space so later PCI
devices can use the same path.
