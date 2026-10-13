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

### IOMMU-0: interface and ownership audit (complete)

Document the DMA access contract, requester-ID lifetime, address-space
ownership, error propagation, and lock ordering.  Add tests for translated
access failure and for devices using different DMA address spaces.  No guest
visible IOMMU registers are added in this step.

The implementation now records whether a PCI device inherited its DMA address
space from the bus.  Removal clears that inherited association, while an
explicitly selected address space remains attached across re-attachment.  The
DMA unit test covers both lifetimes and the documented error classes.  The
resulting VART bug and its fix are recorded in
`docs/debugging/vart/vart-dma-address-space-lifetime.md`.

### IOMMU-1: software translation context (complete)

Add an IOMMU address-space object that can be attached to a PCI device and
looked up by requester ID.  Implement a locked identity context first, then a
faulting context for unmapped ranges.  Keep RAM-only bulk access semantics and
return precise errors to the device model.

VART now provides `VartIommuContext` as a proxy address space.  Its identity
mode forwards RAM DMA to a downstream address space, while fault mode rejects
translation with `-EACCES`.  Attaching a context saves and restores the
device's previous DMA association, records the requester ID, and refuses
destruction while attached.  The focused test covers both modes, requester
matching, realization restrictions, and detach lifetime.

This is a software boundary only.  It does not yet walk guest page tables,
expose an IOMMU PCI function, or claim Linux IOMMU-DMA support.

### IOMMU-2: first-stage page-table walker (complete)

Implement the minimum RISC-V IOMMU first-stage translation needed by a 64-bit
guest: Sv39x4 page-table walks, read/write permissions, superpages, accessed
and dirty-bit policy, and invalidation of a single context or address.  Add a
small host-side page-table guest and unit tests for valid, permission-fault,
unmapped, and boundary cases.  Sv48x4, G-stage translation, and PASID remain
deferred until this path is stable.

VART now implements an uncached Sv39x4 walker with the widened eleven-bit root
index, three levels, aligned superpages, canonical IOVA checks, R/W/U
permission checks, and optional software A/D-bit updates.  The translator
rejects a DMA buffer that crosses the active leaf mapping boundary so that a
future scatter-gather path cannot silently use one translation for two pages.
The invalidation API now also advances a generation boundary; actual cached
entry reclamation is implemented in IOMMU-3.

### IOMMU-3: translation cache and invalidation (complete)

VART now has a fixed 64-entry translation cache for Sv39x4 leaf mappings.  A
cache entry records the complete IOVA range, translated base, and effective
permissions, so a lookup can only reuse a mapping when the complete DMA buffer
fits inside that leaf.  The cache is intentionally disabled while software A/D
updates are enabled, because each page-table walk must retain its architectural
side effect.

Lookup, page-table walking, insertion, mode changes, and invalidation share a
context mutex.  Selective invalidation removes overlapping entries by compacting
the array; mode/root changes flush the cache.  This keeps entry lifetime valid
without exposing a pointer after removal and provides a simple baseline for a
future reader/writer or IOThread design.  The focused test verifies stale-cache
behavior before invalidation, reload after invalidation, and concurrent lookup
and invalidation.

### IOMMU-4: guest-visible RISC-V IOMMU device (complete)

VART now exposes a RISC-V IOMMU PCI function with the standard class and Red Hat
compatible vendor/device IDs used by QEMU.  BAR0 implements the standard CAP,
FCTL, DDTP, command queue, and queue CSR register layout.  The initial command
consumer supports IOTINVAL.VMA and IOFENCE commands, reads 16-byte commands
through the PCI DMA path, and reports unsupported commands through CQCSR.

The device accepts one non-owning software `VartIommuContext` for command
invalidation.  This is deliberately a small integration boundary: device
directory-table walking, fault queues, MSI-X notification, and automatic
requester-ID context allocation remain in later stages.  The device is a
normal PCI BAR device and is not special-cased in the virt machine.

### IOMMU-5: fault and interrupt delivery (complete baseline)

Translation contexts now expose a fault callback.  The PCI IOMMU records the
last fault address, access direction, size, and errno in a small diagnostic
register block, and reports pending status through a write-one-to-clear bit.
Faults use MSI-X vector 0 and command-queue errors use vector 1 through the
existing PCI MSI-X injector, so the injector can target either APLIC/IMSIC or
KVM irqfd without coupling the IOMMU code to one delivery backend.  Reset and
teardown detach the callback before destroying the device.  Tests cover fault
recording, MSI-X notification, pending clear, reset, and repeated queue use.

This is the baseline notification path; interrupt cause routing, fault queues,
and Linux-specific fault reporting remain follow-up work.  The same stage now
also supports a one-level device directory table for one software-bound PCI
requester: DDTP selects the table, the requester ID selects a 64-byte DTE, and
the DTE's device context selects identity or Sv39x4 first-stage translation.
Invalid DTEs put the bound context into fault mode.

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
