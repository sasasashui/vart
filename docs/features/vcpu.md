# vCPU lifecycle and execution boundary

## Purpose

The vCPU subsystem owns a RISC-V KVM vCPU file descriptor and its shared
`struct kvm_run` mapping. It provides core-register access and translates raw
KVM exits into stable VART exit records.

## Design

`VartVcpu` refers to its parent `VartVm`, records the architectural hart ID, and
owns both the vCPU descriptor and run mapping. Each vCPU also owns its host
thread and follows `CREATED -> RUNNING -> STOPPED`. Lifecycle control remains
single-threaded; `vart_vcpu_join()` returns only after the worker has stopped.

The generic one-register functions expose the KVM UAPI for architecture code.
The RISC-V helpers currently cover PC, privilege mode, and integer registers.
Register x0 reads as zero without an ioctl; attempts to write a nonzero value to
x0 are rejected.

`vart_vcpu_run()` performs one `KVM_RUN` and copies exit data out of the shared
mapping. It reports MMIO, system event, shutdown, interrupted, and unknown exits.
It remains available as a low-level synchronous operation.

`vart_vcpu_start()` launches the worker loop with an exit handler. The worker
runs `KVM_RUN` without the VM big lock so other vCPUs and control threads can
make progress. It acquires the big lock before invoking the handler and keeps
the lock across MMIO dispatch and other guest-visible state changes. A handler
returns zero to resume, a positive value for a clean stop, or a negative errno
to stop with an error. The result is collected by `vart_vcpu_join()`.

## Guest-visible behavior

PC and integer registers use the 64-bit RISC-V KVM one-reg layout from the
installed kernel UAPI. The first guest starts in supervisor mode at guest
physical address `0x80000000` and stores `0x12345678` to `0x10000000`.

## Limitations

- vCPU kick and asynchronous stop are not yet implemented. A started vCPU must
  reach an exit for which its handler requests termination.
- Lifecycle operations must be issued by one control thread.
- MP state, CSRs, timers, floating point, and vector state are not managed.
- MMIO read completion and repeated execution are deferred to the execution
  test and address-space stages.

## Validation

`tests/integration/kvm/vcpu.c` creates hart zero, maps `struct kvm_run`, writes
and reads PC, a0, and a1, verifies supervisor mode and x0 behavior, rejects
invalid register indices, and tears down the vCPU before its VM.

`tests/integration/kvm/guest-mmio.c` loads the raw image built from
`tests/guests/cpu/mmio-exit.S`, runs it once, and verifies the MMIO exit address,
direction, access size, KVM reason, and little-endian data bytes.

`tests/integration/kvm/guest-mmio-roundtrip.c` verifies a real MMIO load exit,
AddressSpace read callback, KVM read completion, resumed guest comparison, and
the following MMIO store through the write callback. It now runs the guest in a
host vCPU thread, asserts that exit dispatch holds the VM big lock, captures the
UART output, and joins the stopped worker.
