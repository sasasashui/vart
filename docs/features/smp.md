# Shared-RAM SMP execution

## Scope

VART can run multiple KVM vCPUs concurrently, with one host pthread per hart.
All vCPUs in a VM use the same registered guest RAM, while each vCPU owns its
register state and `kvm_run` mapping. The VM big lock is not held during
`KVM_RUN`, so guest code executes in parallel on available host CPUs.

RISC-V KVM can create secondary harts in a stopped MP state. This test sets both
harts to `KVM_MP_STATE_RUNNABLE` and reads the state back before starting their
threads. This is test setup rather than the final secondary-hart boot policy.

The machine controller initializes each hart's `a0` register with its hart ID.
This is a temporary tiny-guest boot convention; the later OpenSBI boot contract
will define the firmware-visible hart ID and device-tree arguments.

## Atomic test protocol

The SMP guest uses a shared structure at guest physical address `0x80100000`:

```text
0x00  u32 arrived
0x04  u32 finished
0x08  u64 counter
0x10  u64 marker[2]
```

Both harts increment `arrived` with `amoadd.w.aqrl` and wait at a startup
barrier. Each then performs 10,000 `amoadd.d.aqrl` operations on `counter`,
writes its unique marker, and atomically increments `finished`. Hart zero waits
for both completions, validates the shared result, and reports PASS or FAIL
through the test device. Hart one remains in guest code until VM shutdown kicks
it out of KVM.

The acquire/release AMOs and explicit fences make the intended ordering visible
in the guest rather than relying on host scheduling or volatile accesses.

## Validation

`tests/integration/kvm/smp-shared-atomic.c` creates two vCPUs, assigns hart IDs,
starts both worker threads, waits for hart zero's MMIO completion, and verifies
the shared structure from the host. It then requests VM shutdown and joins the
secondary vCPU. A ten-second alarm bounds deadlocks and lost wakeups.
