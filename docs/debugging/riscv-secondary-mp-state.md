# RISC-V secondary vCPU does not run

## Symptom

The first shared-RAM SMP guest timed out at its startup barrier. Both vCPU host
threads had entered their run loops, but only hart zero incremented the shared
arrival counter.

## Cause

RISC-V KVM may create secondary vCPUs in a stopped MP state. Creating a vCPU,
setting its PC and registers, and calling `KVM_RUN` does not by itself guarantee
that the hart is runnable. The blocked `KVM_RUN` produced no diagnostic exit, so
the failure initially looked like an atomic-memory ordering bug.

QEMU probes `KVM_CAP_MP_STATE` and synchronizes CPU state with
`KVM_SET_MP_STATE`. VART had not yet implemented that part of the KVM vCPU
contract.

## Resolution

VART now probes `KVM_CAP_MP_STATE` and wraps `KVM_GET_MP_STATE` and
`KVM_SET_MP_STATE`. SMP tests explicitly set both harts to
`KVM_MP_STATE_RUNNABLE` and read the state back before starting their threads.

This does not define the final machine boot policy. The secondary-hart stage
will deliberately create secondaries stopped and validate a controlled wakeup
path. When an SMP guest stalls before its first shared-memory write, inspect MP
state before investigating cache coherence or AMO ordering.

Do not issue `KVM_GET_MP_STATE` from a control thread while that vCPU is blocked
inside `KVM_RUN`; the ioctl can wait for the active run to return and make the
diagnostic itself look like a lifecycle deadlock. Read back initial state before
starting the worker, or inspect the state from the vCPU's own exit context while
it is outside `KVM_RUN`.

The same constraint affects `KVM_SET_MP_STATE(RUNNABLE)`. Calling it for a
stopped secondary while the caller holds the VM big lock can block waiting for
the secondary's active `KVM_RUN`; the secondary then cannot acquire the big lock
to finish its exit. VART therefore sends the vCPU kick before issuing the state
ioctl. The signal makes `KVM_RUN` return without needing the big lock, allowing
the ioctl to complete; normal exit handling resumes after the caller releases
the lock.
