# KVM, VM, and guest memory lifecycle

## Purpose

This feature establishes the smallest useful KVM foundation: opening KVM,
checking required capabilities, creating a VM, allocating anonymous guest RAM,
and registering that RAM as a KVM memory slot.

## Design

`VartKvm` owns `/dev/kvm` and caches capabilities needed by later subsystems.
`VartVm` owns a KVM VM file descriptor and refers to its `VartKvm`. A
`VartMemoryRegion` owns one anonymous host mapping and describes one KVM memory
slot.

Public functions return zero on success or a negative `errno` value on failure.
Probe functions may return a positive capability value. Destructors accept
partially initialized objects so failure paths can unwind in reverse order.

Guest RAM is page-aligned, anonymous, private, and initially allocated with
`MAP_NORESERVE`. Registering the region does not change mapping ownership.
Callers must unregister a region before destroying a VM or its host mapping.

This KVM memory slot is only the acceleration-layer mapping. A future VART
system address space will own guest-visible address resolution, while device DMA
will use a per-device DMA address space and optional IOMMU translation.

## Guest-visible behavior

The integration test maps 16 MiB at guest physical address `0x80000000`. No
vCPU runs in this feature, so the mapping is not yet observable by guest code.

## Limitations

- Only anonymous writable RAM is supported.
- Dirty logging, read-only memory, memory aliases, and multiple-slot overlap
  validation are not implemented.
- Memory lifetime ordering is currently a caller responsibility.

## Validation

`tests/integration/kvm/vm-memory.c` creates a real KVM VM, writes the first and
last words of a 16 MiB host mapping, registers memory slot zero, unregisters it,
and tears all objects down. `make check` also probes required KVM capabilities
and tests whether the RISC-V AIA device type is available.

`tests/unit/memory/region.c` covers zero and unaligned region parameters, a
valid one-page allocation, initial zero contents, metadata, and destruction
without requiring KVM.
