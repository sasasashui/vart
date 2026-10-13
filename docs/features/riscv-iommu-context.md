# VART software IOMMU context

`VartIommuContext` is the first software translation boundary used by PCI DMA.
It owns a proxy `VartAddressSpace`; PCI device code continues to call the
existing DMA helpers without knowing whether the address space is direct or
translated.

An identity context forwards an IOVA unchanged to its downstream address
space. A fault context rejects every non-empty access with `-EACCES`. Both
modes preserve the address-space buffer rules: only a complete RAM-backed
range is accepted, while MMIO and unmapped ranges retain the existing
`-EACCES` and `-ENOENT` errors from the downstream space.

Context attachment is non-owning. The context saves the device's previous DMA
address-space pointer and inheritance flag, replaces it while attached, and
restores both on detach. Devices must be unrealized for attach/detach and the
context cannot be destroyed while attached. Requester-ID matching is checked
against the ID captured at attach time and the device's current ID; later
translation contexts will replace this single-device association with a
requester-ID table.

The implementation deliberately does not include page-table walking, IOATC,
fault queues, guest-visible registers, or KVM acceleration. Those belong to
the following IOMMU stages.
