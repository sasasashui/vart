# Secondary-hart MP state

VART distinguishes host thread lifecycle from the architectural execution
state reported to KVM. A vCPU worker may be `VART_VCPU_THREAD_RUNNING` while its
guest hart is `KVM_MP_STATE_STOPPED`; in that state the thread can remain inside
`KVM_RUN` without executing guest instructions.

`vart_vcpu_set_mp_state_locked()` changes MP state while the caller holds the VM
big lock. Making a running worker's hart runnable first kicks the worker out of
`KVM_RUN` and then changes MP state. Moving a hart to STOPPED does not terminate
its host thread. VM shutdown remains responsible for kicking and joining all
workers.

## Validation protocol

The secondary-hart guest and host test use a minimal command channel through the
test device:

1. The host creates hart zero RUNNABLE and hart one STOPPED.
2. It reads back STOPPED, starts hart one's worker, waits briefly, and verifies
   that the worker exists while shared RAM remains unchanged.
3. Hart zero writes `S`; its MMIO callback changes hart one to RUNNABLE and
   kicks its worker.
4. Hart one writes a shared marker and then writes `T`; its callback changes
   hart one back to STOPPED, reads the state back while outside `KVM_RUN`, and
   publishes a host acknowledgement.
5. Hart zero waits for both values before reporting PASS.
6. The host verifies hart one's STOPPED state, requests VM shutdown, and joins
   the still-existing secondary worker.

This command channel models only the lifecycle mechanics. Linux will use SBI
HSM through OpenSBI rather than the VART test device.
