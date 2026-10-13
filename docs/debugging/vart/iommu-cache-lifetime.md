# VART IOMMU cache lifetime notes

IOMMU-3 adds a small software translation cache after the Sv39x4 walker.  The
cache stores values, not pointers to page-table memory, and has a fixed
capacity of 64 entries.  This keeps the first implementation deterministic and
avoids an unbounded allocation path in device DMA.

The important lifetime rule is that lookup and invalidation use the same
`cache_lock`.  Selective invalidation compacts the entry array immediately;
there are no invalid tombstones and no reader may retain an entry address after
unlocking.  Mode changes and a new root page table flush the complete cache.

The unit test in `tests/unit/iommu/context.c` changes a leaf mapping, confirms
that the old mapping remains visible until invalidation, then confirms that the
new mapping is loaded.  It also runs 2,000 DMA lookups concurrently with 2,000
selective invalidations.  This is a VART concurrency test, not a report of an
upstream QEMU or Linux bug.

When `update_ad` is enabled, the cache is deliberately bypassed.  A cached hit
would otherwise skip the accessed/dirty page-table writes.  A later optimized
implementation may cache A/D state explicitly, but it must preserve those
architectural side effects.
