# Concurrent vCPU MMIO

Multiple vCPUs execute guest code concurrently while outside VART. When KVM
returns an MMIO exit, the vCPU worker acquires the VM big lock before address
space dispatch. Device callbacks therefore remain single-threaded even when
different harts access the same register at the same time.

The big lock currently protects the whole exit handler rather than individual
devices. This deliberately favors a simple ownership rule. A later optimization
may add device locks, but it must retain the documented lock order and preserve
serialization for device models that require it.

## Validation

`tests/guests/smp/concurrent-mmio.S` synchronizes two harts with a shared-RAM
startup barrier. Each hart then writes its identity byte to the same test-device
register 1,000 times. The primary waits for an atomic completion barrier before
reporting PASS.

The output callback asserts that the VM big lock is held, uses an atomic
re-entry guard, and calls `sched_yield()` to enlarge the host scheduling window.
The host requires exactly 1,000 writes from each hart, no invalid values, and no
overlapping callback execution. The test then shuts down and joins the secondary
hart.
