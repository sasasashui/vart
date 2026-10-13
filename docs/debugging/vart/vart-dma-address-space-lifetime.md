# VART DMA address-space lifetime

## Symptom

A PCI device that inherited `VartBus.address_space` could retain the old
address-space pointer after removal from a bus. Re-attaching the same
pre-realized device to another PCI bus then continued to direct DMA to the old
space. The pointer was not dereferenced after removal because requester and
bus-master validation rejected detached devices, so ordinary tests did not
expose the stale association.

## Cause

`VartDevice.dma_as` was used for both an explicitly selected IOMMU boundary
and a bus-provided default, but the implementation did not record which case
applied. PCI bus removal therefore had no safe way to clear only the default
association.

## Fix

`dma_as_from_bus` now records inherited associations. PCI attachment sets the
flag only when it fills a null pointer from the bus. Removal clears the
pointer and flag for inherited associations, while an explicit pointer stays
attached. The DMA unit test re-attaches one device to two address spaces and
checks both paths.

This is a VART bug, not an upstream Linux or QEMU bug. It matters before page
table translation exists because an eventual IOMMU domain must never be
silently bypassed by a stale bus association.
