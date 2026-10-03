# QEMU RISC-V IOMMU IOATC invalidation performance

## Problem

QEMU keeps invalidated RISC-V IOMMU IOATC entries in its hash table with
`IOMMU_NONE` permissions.  The entries still count towards `ioatc-limit`, and
every later invalidation scans them again.  A normal Linux guest using lazy
IOMMU invalidation can therefore make the cost grow with all translations
seen since the last whole-cache replacement.

The issue was reproduced with the default IOATC limit, `iommu.strict=0`, four
vCPUs, and four concurrent storage jobs.  This is a normal DMA map/unmap
workload rather than a reduced-cache stress configuration.

## Investigation record

### 1. Establish the performance problem

Four jobs each copied and verified an 8 MiB buffer 50 times through an NVMe
device behind `riscv-iommu-pci`.  The workload performs about 6.4 GiB of
verified writes.

- Default IOATC: the unmodified implementation did not finish in 180 seconds.
- `ioatc-limit=1` or disabled IOATC: the workload completed in tens of seconds.
- No IOMMU fault, DMA error, NVMe timeout, or data mismatch was reported.

This isolated accumulated IOATC state as the source of the slowdown.

### 2. Reclaim selectively invalidated entries

The invalidation predicates were converted from `GHFunc` callbacks that set
`entry->perm = IOMMU_NONE` to `GHRFunc` predicates used by
`g_hash_table_foreach_remove()`.  Matching entries are now reclaimed instead
of becoming permanent tombstones.

With the default IOATC limit, the same workload completed in 29 seconds.  This
is a measured improvement of at least 6.2 times over the incomplete 180-second
baseline.

### 3. Add focused regression coverage

The qtest uses `ioatc-limit=2`, fills the cache with translations A and B,
selectively invalidates A, and inserts A again.  It then changes B's page-table
entry without invalidating B and checks that B remains cached.

The old implementation fails because A's tombstone makes the table appear
full and reinserting A evicts the still-valid B entry.  The reclaiming
implementation passes.  All five RISC-V IOMMU qtest cases pass with the fix.

### 4. Protect entry lifetime and invalidation ordering

Deleting an entry immediately is unsafe if an IOThread has obtained a raw
pointer from `g_hash_table_lookup()`.  A reference to the hash table protects
the table object, not an individual entry removed from that table.

An IOATC mutex now serializes lookup, page-table walk, insertion, invalidation,
and reset.  Holding it across a cache miss is intentional: protecting only
the individual hash-table operations would allow this ordering:

1. A translation misses and reads a page-table entry.
2. The guest invalidates that translation.
3. The first thread inserts the translation read before invalidation.

The complete critical section prevents both entry use-after-free and stale
post-invalidation insertion.  It also documents the locking requirement on
the cache helpers.

## Verification

- QEMU builds without warnings.
- `iommu-riscv-test`: five subtests pass, including selective reclamation.
- The new regression test fails on the original implementation.
- With the IOATC mutex, the Linux NVMe workload completes in 30 seconds with
  no data mismatch or IOMMU/DMA error.  The unlocked reclaiming prototype took
  29 seconds in the same test, so no material regression was observed.
- An initial detached-console IOThread run did not execute the guest workload.
  QEMU thread backtraces showed normal polling and KVM waits rather than a lock
  cycle, but this is not sufficient concurrency validation and is not recorded
  as a passing test.

## Remaining work

- Complete a storage workload with the block device assigned to a dedicated
  IOThread and retain its result as concurrency regression evidence.
- Measure contention introduced by the single IOATC mutex.
- Consider partitioning the cache by translation tag and soft-context IDs so
  exact invalidations can use direct hash removal instead of a full scan.
- Consider generation counters or per-context tables for broad invalidations.
- Add trace points or statistics for cache size, hits, misses, invalidations,
  and reclaimed entries before attempting more complex cache policies.
- Audit the device-context cache separately; it is outside the scope of this
  IOATC fix.

The mutex-based version favors correctness and reviewability.  Lock sharding
or lockless lookup should only replace it after equivalent lifetime and
invalidation-order guarantees have dedicated tests.
