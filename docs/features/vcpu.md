# vCPU lifecycle and execution boundary

## Purpose

The vCPU subsystem owns a RISC-V KVM vCPU file descriptor and its shared
`struct kvm_run` mapping. It provides core-register access and translates raw
KVM exits into stable VART exit records.

## Design

`VartVcpu` refers to its parent `VartVm`, records the architectural hart ID, and
owns both the vCPU descriptor and run mapping. Creation and destruction are
single-threaded in this stage. A future vCPU thread will remain outside this
low-level object.

The generic one-register functions expose the KVM UAPI for architecture code.
The RISC-V helpers currently cover PC, privilege mode, and integer registers.
Register x0 reads as zero without an ioctl; attempts to write a nonzero value to
x0 are rejected.

`vart_vcpu_run()` performs one `KVM_RUN` and copies exit data out of the shared
mapping. It reports MMIO, system event, shutdown, interrupted, and unknown exits.
It does not dispatch devices or loop internally, leaving policy to the future
execution layer.

## Guest-visible behavior

PC and integer registers use the 64-bit RISC-V KVM one-reg layout from the
installed kernel UAPI. The first guest starts in supervisor mode at guest
physical address `0x80000000` and stores `0x12345678` to `0x10000000`.

## Limitations

- vCPUs are not yet placed in host threads.
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
the following MMIO store through the write callback.
