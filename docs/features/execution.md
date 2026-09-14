# MMIO exit execution

## Purpose

The execution layer connects architecture-neutral vCPU exits to the system
address space without making the vCPU subsystem depend on devices.

## Design

For a write exit, VART decodes the KVM byte array as a little-endian integer
and dispatches it through the address space. For a read exit, it dispatches the
read, then encodes the returned integer into the pending KVM MMIO buffer before
the next `KVM_RUN` resumes the original load instruction.

The vCPU records whether a read completion is pending and its width. A missing,
duplicate, or invalid-width completion is rejected. Device callback failures
are returned without completing the read.

## Machine policy

The execution module handles only MMIO. The runtime entry routes system events,
shutdown, interruption, userspace SBI, and unknown exits as machine policy,
then coordinates shutdown of all vCPUs under the VM big lock.

## Validation

Host-side tests validate write decoding and errors. The KVM roundtrip guest
performs a 32-bit MMIO load, compares the completed value, and performs a
32-bit MMIO store only on success. The integration test checks the final value
received by the device callback.
