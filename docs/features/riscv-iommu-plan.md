# RISC-V IOMMU implementation plan

This document defines the next development line after the PCIe foundation
checkpoint.  It is intentionally staged so every increment has a small,
observable test and a stable interface for later Linux workloads.

## Design constraints

The IOMMU must sit at the device DMA boundary, not in virtio device code.
Each PCI device already owns a DMA address space and exposes an explicit
requester-ID DMA API.  Translation, permission checks, faults, and invalidation
will be implemented behind that boundary.  The initial implementation is a
userspace VART model; KVM acceleration and two-stage translation are later
optimizations/features.

## Staged work

### IOMMU-0: interface and ownership audit

Document the DMA access contract, requester-ID lifetime, address-space
ownership, error propagation, and lock ordering.  Add tests for translated
access failure and for devices using different DMA address spaces.  No guest
visible IOMMU registers are added in this step.

### IOMMU-1: software translation context

Add an IOMMU address-space object that can be attached to a PCI device and
looked up by requester ID.  Implement a locked identity context first, then a
faulting context for unmapped ranges.  Keep RAM-only bulk access semantics and
return precise errors to the device model.

### IOMMU-2: first-stage page-table walker

Implement the minimum RISC-V IOMMU first-stage translation needed by a 64-bit
guest: Sv39x4 page-table walks, read/write permissions, superpages, accessed
and dirty-bit policy, and invalidation of a single context or address.  Add a
small host-side page-table guest and unit tests for valid, permission-fault,
unmapped, and boundary cases.  Sv48x4, G-stage translation, and PASID remain
deferred until this path is stable.

### IOMMU-3: translation cache and invalidation

Add a bounded IOATC/IOTLB cache only after the uncached walker is correct.
Define entry lifetime and locking before optimizing lookups.  Implement global,
context, and address-selective invalidation with removal of stale entries.
Stress tests must exercise concurrent DMA lookup and invalidation and verify
that no stale translation or use-after-free is possible.

### IOMMU-4: guest-visible RISC-V IOMMU device

Model the minimal PCIe IOMMU configuration and command interfaces needed by a
guest driver: device context, queue setup, translation enablement, and
invalidation commands.  Report faults through a deterministic software path
first.  The device must be connected to the existing ECAM/BAR/DMA framework,
not special-cased in the virt machine.

### IOMMU-5: fault and interrupt delivery

Connect translation faults and command-queue completion to the existing APLIC
and IMSIC framework.  Complete the PCIe INTx pin-swizzling work separately so
legacy interrupts and MSI/MSI-X have unambiguous paths.  Tests must cover
masked, pending, reset, and teardown cases.

### IOMMU-6: Linux integration

Start with a tiny guest driver workload, then test a Linux kernel with the
RISC-V IOMMU driver in identity and translated modes.  Only after ordinary
DMA map/unmap is reliable should we enable IOMMU-DMA and MSI remapping
workloads.  Record the exact kernel commit/series used for every result; do
not make the implementation depend on an unreleased Linux patchset.

### IOMMU-7: optional acceleration and advanced features

Evaluate eventfd/irqfd and worker-thread acceleration for invalidation and
fault delivery.  G-stage translation, nested contexts, ATS/PASID, scalable
cache indexing, KVM integration, and VFIO are separate follow-up projects.

## Acceptance rule

Each stage needs focused unit tests, a documented failure/debugging record
when a bug is found, `make check`, and one English Linux-style commit.  A
stage may not hide an unverified fast path behind the public DMA API.  The
uncached and translated implementations must remain selectable while the
guest integration is being developed.
