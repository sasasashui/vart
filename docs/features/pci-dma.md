# PCI bus mastering and DMA

Stage 11.1 adds the first DMA path for PCI devices.

## Ownership and address translation

Each PCI device uses `VartDevice.dma_as` as the address space for bus
mastering. A device added to a PCI bus inherits that bus address space when it
does not provide one explicitly. This keeps the translation boundary attached
to the device so an IOMMU address space can be installed later without
changing device code.

The attachment is a non-owning pointer. `dma_as_from_bus` records whether the
current pointer was inherited from the PCI bus. Removing a device clears an
inherited pointer before the device can be attached to another bus; an
explicit address space remains selected across removal and re-addition. A
device must not be realized while its DMA address space is being changed.

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

The explicit requester-ID helpers reject a stale or mismatched ID with
`-EPERM`; a missing DMA address space returns `-ENODEV`, and an address range
that is not entirely covered by a RAM region is returned by the address-space
layer as `-ENOENT` (or `-EACCES` for a non-RAM region). These error classes are
kept stable for the future IOMMU fault path.

## Validation

`tests/unit/pci/dma.c` verifies command-register state transitions, DMA read and
write round trips, requester-ID rejection, unmapped-address rejection,
disabling bus mastering, and inherited versus explicit DMA address-space
lifetime across bus removal and re-attachment.
The virt machine maps its KVM RAM into the system address space so later PCI
devices can use the same path.
