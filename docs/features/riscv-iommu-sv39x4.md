# VART Sv39x4 translation

The software IOMMU context supports the RISC-V IOMMU Sv39x4 first-stage page
table format.  The root table uses eleven index bits and is therefore aligned
to 16 KiB; the next two levels use nine bits each.  PTEs are 64-bit little
endian entries in the device's downstream RAM address space.

Leaf entries require `V`, `U`, and a suitable `R` or `W` permission.  The
reserved `W=1,R=0` and `W+X` combinations, invalid PTEs, reserved bits, NAPOT
entries, and misaligned superpage PPNs are rejected.  Three translation sizes
are supported: 4 KiB, 2 MiB, and 1 GiB.  IOVAs must be canonical for the
41-bit Sv39x4 range, and a DMA buffer may not cross the selected leaf mapping
boundary.

The context can either require pre-set `A`/`D` bits or update them in software
when `update_ad` is enabled.  Page-table reads and A/D writes use the existing
RAM-only address-space helpers, so malformed or non-RAM tables retain the
normal VART error propagation.

The walker is deliberately uncached.  `vart_iommu_context_invalidate()` is a
generation/invalidation boundary for the later IOATC implementation; IOMMU-3
will add cached entries and prove their lifetime under invalidation.
