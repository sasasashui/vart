# SBI HSM

VART relies on RISC-V KVM's in-kernel implementation of the standard SBI Hart
State Management extension. Direct-kernel guests do not require OpenSBI or a
userspace SBI dispatcher for hart startup, stop, or state queries.

VART creates hart zero as `KVM_MP_STATE_RUNNABLE` and secondary harts as
`KVM_MP_STATE_STOPPED`. Their host worker threads may all enter `KVM_RUN`;
KVM keeps a stopped hart blocked until another guest hart issues `hart_start`.
The start call supplies the secondary entry address and opaque value, and KVM
resets the target hart so that it enters with its hart ID in `a0` and the
opaque value in `a1`.

## Validation

The two-hart HSM guest verifies the following lifecycle:

1. hart one initially reports STOPPED and executes no guest instructions;
2. invalid hart IDs and function IDs return the standard SBI errors;
3. `hart_start` enters the requested address with the requested arguments;
4. starting an active hart returns `SBI_ERR_ALREADY_AVAILABLE`;
5. `hart_stop` does not return and the hart subsequently reports STOPPED; and
6. the same hart can be started and stopped a second time with new state.

Only hart zero's final test-device write exits to userspace. All HSM operations
and secondary-hart state transitions remain inside KVM.
