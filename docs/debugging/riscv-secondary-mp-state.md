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
