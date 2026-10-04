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

The initial implementation used one critical section for lookup, page-table
walk, and insertion.  This prevents both entry use-after-free and stale
post-invalidation insertion in the common case.  A later full source audit,
recorded below, found that holding this lock across the page-table walk is not
an acceptable final design because the walk can enter the memory API and
acquire the BQL.

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

### 8. Check an upstream Linux kernel without IOMMU_DMA enablement

The same workload was tested with an unmodified Linux 6.18.3 kernel.  This
kernel includes the RISC-V IOMMU driver but predates the series which enables
`IOMMU_DMA` and RISC-V IMSIC MSI remapping.  The original and optimized QEMU
binaries both completed the four-job virtio-blk workload in eight seconds.

QEMU tracing recorded 1486 IOMMU translations, all of which were identity
writes to IMSIC pages in the `0x28000000` through `0x28003000` range.  It
recorded no block-data DMA read or write translation.  The kernel therefore
did not exercise the IOATC invalidation workload responsible for the
slowdown, so this result neither reproduces nor contradicts the optimization.

The end-to-end performance regression currently requires the not-yet-upstream
Linux RISC-V IOMMU DMA/MSI series.  The QEMU-only selective-invalidation qtest
remains independent of that kernel series and continues to demonstrate the
cache-capacity defect on upstream QEMU.

### 9. Profile with the v6 Linux IOMMU_DMA series

The v6 series was downloaded from its mailing-list message ID,
`20260925151659.419512-1-andrew.jones@oss.qualcomm.com`, and applied without
conflicts to the current `linux-iommu/next` commit `cec2dc7663e7`.  The
resulting 16-patch test branch ends at `57b68d459e32` and identifies itself as
Linux `7.3.0-rc5-g57b68d459e32`.  Its configuration has `IOMMU_DMA`, the
RISC-V IOMMU, IMSIC, and NVMe built in.

The unmodified QEMU workload completed in 121 seconds without profiling.  The
same workload under `perf record` completed in 128 seconds, while the fixed
QEMU completed in 22 seconds.  Both profiling runs used userspace cycles at
99 Hz with DWARF call graphs.  RISC-V perf emitted incomplete IP/SP unwind
warnings for some samples, but flat symbol attribution remained available and
reported no lost samples.

In the unmodified build, flat symbol attribution showed:

- `riscv_iommu_iot_inval_pscid_iova`: 61.93 percent;
- `g_hash_table_foreach`: 16.47 percent;
- combined invalidation predicate and hash scan: 78.40 percent.

The fixed build reduced `riscv_iommu_iot_inval_pscid_iova` to 19.12 percent.
Approximate sampled userspace cycles fell from 327.33 billion to 81.47
billion, a reduction of about 75 percent.

Temporary trace instrumentation recorded the cache size before and after each
invalidation.  It was removed after collecting these results and is not part
of the QEMU patch:

| Metric | Unmodified | Fixed |
| --- | ---: | ---: |
| Invalidation calls | 400,106 | 399,394 |
| Mean entries scanned per call | 10,590.91 | 918.79 |
| Maximum entries before invalidation | 11,350 | 3,589 |
| Cumulative entries scanned | 4,237,485,494 | 366,960,172 |
| Entries physically removed | 0 | 410,757 |
| Entries remaining at shutdown | 11,350 | 0 |

The number of invalidation commands is effectively unchanged, while physical
entry reclamation reduces cumulative scan work by about 11.55 times.  This
directly connects the runtime regression to tombstone accumulation in the
QEMU IOATC rather than to another part of the experimental Linux series.

### 10. Audit IOATC concurrency and entry lifetime

The hash table owns each `RISCVIOMMUEntry`.  The same allocated pointer is
used as both key and value by `g_hash_table_add()`, and the key destroy
function is `g_free`.  Consequently, `g_hash_table_remove()` and
`g_hash_table_foreach_remove()` free a matching entry immediately.  Taking a
reference to the `GHashTable` keeps the table object alive, but does not keep
an entry alive after removal.  A lookup result may therefore only be
dereferenced while exclusion against removal is held, or after its fields
have been copied to independent storage.

The mutex version meets that entry-lifetime rule: lookup, copying a hit into
the returned `IOMMUTLBEntry`, insertion, removal, whole-cache replacement,
and reset are serialized.  Whole-cache replacement is also safe under this
mutex.  The translating thread retains a reference to the old table, while
`s->iot_cache` is atomically replaced and the state object's old reference is
dropped.  The old table is destroyed only after the translating thread drops
its reference.

However, the mutex is held while `riscv_iommu_spa_fetch()` reads page-table
entries with `ldl_le_dma()` or `ldq_le_dma()`.  These accesses normally hit
RAM, but the guest controls the page-table physical addresses.  An address
which resolves to a non-lockless MMIO region enters `prepare_mmio_access()`
and acquires the BQL.  Command-queue invalidation is executed synchronously
from the IOMMU MMIO write callback, which normally already holds the BQL.
This creates the following lock inversion:

```
IOThread translation: iot_lock -> page-table MMIO -> BQL
IOMMU command/reset:   BQL -> iot_lock
```

If the page-table access re-enters this IOMMU and processes an invalidation,
the same thread can also deadlock recursively on the non-recursive
`iot_lock`.  Normal Linux page tables reside in RAM, so the existing NVMe and
multi-IOThread tests do not exercise this case.  The current coarse-lock
implementation must therefore be treated as a measured prototype, not the
final submit-ready lifetime design.

One invalidation command also has an uncovered atomicity window.
`IOTINVAL.GVMA` invalidates `VG` and `VN` entries with two calls to the helper,
and each call independently acquires and releases `iot_lock`.  A translation
can insert a `VG` entry after the first call and before the second call.  That
entry survives completion of the command even though its translation began
concurrently with the invalidation.  Locking must cover the complete command,
not each tag-specific table walk.

The revised implementation uses a short-held IOATC mutex plus a monotonically
increasing generation:

1. Lock, look up an entry, copy a hit's fields, or record the current
   generation on a miss, then unlock.
2. Perform the page-table walk without `iot_lock`.
3. Lock again and insert only if the generation has not changed.  If it has
   changed, discard the cache entry; retrying the translation can be used if
   strict completion ordering is required.
4. Hold the lock once for the complete invalidation command, remove all
   matching tags, increment the generation, then unlock.
5. Reset removes all entries and increments the generation under the same
   lock.

This keeps raw entry pointers inside short critical sections, prevents a walk
started before invalidation from repopulating the cache, makes the two-tag
GVMA operation atomic, and avoids acquiring the BQL while holding
`iot_lock`.  The implementation retries the complete translation after a
generation change rather than returning an already-computed result.

The audit found two adjacent issues which should not be hidden in this
performance patch:

- With `ioatc-limit=0`, the caller allocated an entry before
  `riscv_iommu_iot_update()` returned early, leaking one entry for every
  cacheable non-identity translation.  Temporary instrumentation and a pure
  S-stage qtest dynamically reached this path and printed the allocated entry
  passed to the early return.  The final code checks the limit before
  allocation and adds disabled-cache behavior coverage.
- `ctx_cache` uses the same table-reference/raw-entry pattern without an
  equivalent lock.  Context lookup, invalidation, and reset require their own
  concurrency audit.  The IOATC mutex alone does not make the entire RISC-V
  IOMMU model thread-safe.

Lifecycle review found no migration obligation for the IOATC because the PCI
RISC-V IOMMU declares itself unmigratable.  Finalization relies on normal QOM
device teardown having quiesced all users; the PCI device is non-hotpluggable
and detaches its bus IOMMU mapping during exit.  No IOATC entry pointer is
stored outside a translation call.

### 11. Split the upstream patch series

The combined development commit remains available as a stable reference, but
an independent branch was created from QEMU master to make review and bisection
clearer.  The series contains three commits:

1. `1059e68485` fixes the independent `ioatc-limit=0` allocation leak and
   adds disabled-cache behavior coverage.
2. `5f0dd612b5` adds the selective-reclamation qtest before changing the
   implementation.
3. `5e7c8259e0` reclaims invalidated entries and adds the short mutex,
   generation retry, and atomic two-tag GVMA invalidation.

Each generated patch passed `scripts/checkpatch.pl` with zero errors and zero
warnings.  At the second commit, the selective-reclamation qtest aborts as
expected because reinserting the invalidated entry evicts the unrelated live
entry.  Applying the third commit makes the same test pass.  The complete
`qemu:qtest-riscv64` Meson suite then completed with 16 tests passing, one
configuration-dependent skip, and no failures.

A deterministic miss-versus-invalidation test needs additional test
infrastructure.  The existing `iommu-testdev` performs DMA synchronously from
its MMIO read callback, so a single qtest connection cannot pause a page-table
walk and concurrently submit an invalidation.  A suitable test needs an
asynchronous DMA worker plus an explicit lockless synchronization point in
the page-table access path.  A probabilistic stress loop is not recorded as
equivalent deterministic coverage.

## Verification

- QEMU builds without warnings.
- `iommu-riscv-test`: five subtests pass, including selective reclamation.
- The new regression test fails on the original implementation.
- The new regression test also fails on unmodified QEMU master at
  `2a0ac9c8ac2d`.
- The revised six-test RISC-V IOMMU qtest binary passed 20 consecutive runs,
  including selective reclamation and disabled-cache behavior.
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
- Unmodified Linux 6.18.3 completed both A/B runs in eight seconds, but trace
  analysis confirmed that it issued only identity IMSIC writes and did not put
  block-data DMA through the IOMMU.
- Linux `7.3.0-rc5` plus the v6 16-patch IOMMU_DMA/MSI series reproduced the
  slowdown, with 78.40 percent of baseline samples in the selective
  invalidation predicate and GLib hash-table scan.
- The short-lock and generation version completed the same four-job NVMe
  workload in 31 seconds with all data checks passing and no IOMMU, DMA, or
  NVMe error.
- A combined test build containing the separate irqfd fix attached four
  virtio-blk devices to four independent IOThreads.  Four concurrent guest
  workloads passed in seven seconds; every device request queue received
  interrupts and no fault, DMA error, or timeout was reported.

## Remaining work

- Add IOATC lock timing statistics if finer-grained contention analysis is
  needed; the current multi-IOThread test validates concurrency but does not
  attribute time spent waiting for the mutex.
- Add a deterministic concurrent miss/invalidation test and an adversarial
  page-table-MMIO test before considering the lifetime work complete.
- Consider splitting the independent `ioatc-limit=0` leak fix into the first
  patch of an upstream series.
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

Physical reclamation remains the performance fix.  The revised generation
design preserves entry lifetime and stale-insertion guarantees without
holding an IOATC lock across AddressSpace accesses.
