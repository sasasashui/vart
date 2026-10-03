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

### 5. Preserve MSI translation for IOThread devices

An automated initramfs test showed that the unmodified v11.1.1 model hangs on
the first virtio-blk request when all of the following are enabled:

- KVM with APLIC and IMSIC;
- `riscv-iommu-pci` interrupt remapping; and
- a virtio-blk device assigned to an IOThread.

Removing either the IOMMU or the IOThread makes the same four-job workload
complete in eight seconds.  The failure therefore predates the IOATC locking
change and requires both components.

Tracing showed that the IOThread completed the first block request but did not
perform the expected `riscv_iommu_dma` translation for its completion MSI.
Virtio uses a guest notifier and KVM irqfd from an IOThread.  irqfd injects the
MSI without an address-space transaction, bypassing the guest-programmed IOMMU
MSI translation from addresses such as `0xffffc000` to the IMSIC file.

As a correctness-first fallback, realizing a KVM RISC-V IOMMU with interrupt
remapping enabled disables MSI-via-irqfd.  Completion notification then returns
to QEMU userspace and traverses the device DMA address space.  Trace output
after the change pairs every `virtio_blk_req_complete` with an IOMMU translation
to `0x28000000` or `0x28002000`.

### 6. Isolate and revalidate the optimization on QEMU master

The IOATC change was moved to the dedicated
`opt/riscv-iommu-ioatc-invalidation` branch based on QEMU master commit
`2a0ac9c8ac2d`.  The unrelated MSI irqfd workaround remains on a separate
branch, and permission-walk fixes that had already reached QEMU master are no
longer part of the performance patch.

The selective-invalidation qtest was applied before the implementation
change.  It failed on the unmodified master build, then passed after the
reclaiming and locking changes.  This establishes that the test detects the
original cache-capacity bug instead of merely exercising the fixed code.

The NVMe workload was also rerun using binaries built from the same source
tree and configuration:

- unmodified master: 127 seconds;
- IOATC reclaiming with the mutex: 21 seconds.

Both runs completed all four jobs without data mismatches or reported IOMMU,
DMA, or NVMe errors.  The measured speedup on current master is about 6.05
times.

### 7. Exercise concurrent IOATC access from IOThreads

A separate integration workload attaches four virtio-blk devices to four
independent IOThreads and disk backends.  Four guest jobs then copy and verify
data concurrently with deferred IOMMU invalidation enabled.  The test combines
the IOATC branch with the separate irqfd correctness fix only in its temporary
test binary; the two submitted changes remain independent.

The first run completed in seven seconds.  Ten additional runs all completed
with four successful jobs and no reported IOMMU, DMA, block, or guest errors.
This exercises simultaneous IOATC lookup, page walk, insertion, and
invalidation from real asynchronous device contexts.

## Verification

- QEMU builds without warnings.
- `iommu-riscv-test`: five subtests pass, including selective reclamation.
- The new regression test fails on the original implementation.
- The new regression test also fails on unmodified QEMU master at
  `2a0ac9c8ac2d`.
- The complete five-test RISC-V IOMMU qtest binary passed 20 consecutive
  runs after the fix, for 100 passing subtests in total.
- With the IOATC mutex, the Linux NVMe workload completes in 30 seconds with
  no data mismatch or IOMMU/DMA error.  The unlocked reclaiming prototype took
  29 seconds in the same test, so no material regression was observed.
- An initial detached-console IOThread run did not execute the guest workload.
  It was replaced by an automated initramfs test that does not require serial
  input.
- The unmodified v11.1.1 implementation times out with RISC-V IOMMU plus
  IOThread, while both isolation controls pass.
- After disabling MSI irqfd for IOMMU interrupt remapping, the IOThread test
  completes in eight seconds: all four jobs pass and all four virtio request
  queues receive interrupts, with no IOMMU/DMA error or timeout.
- On QEMU master, the same four-job NVMe workload improved from 127 seconds
  to 21 seconds with the isolated IOATC patch.
- A four-device, four-IOThread integration workload passed 11 runs, completing
  44 guest data-copy and verification jobs without an observed race or data
  mismatch.

## Remaining work

- Add IOATC lock timing statistics if finer-grained contention analysis is
  needed; the current multi-IOThread test validates concurrency but does not
  attribute time spent waiting for the mutex.
- Replace the global irqfd fallback with RISC-V IOMMU-aware MSI route updates
  so irqfd can be enabled without bypassing interrupt translation.
- Add an upstream-suitable automated KVM test for IOMMU plus IOThread; the
  current initramfs test is an external integration reproducer.
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
