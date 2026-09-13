# RISC-V KVM HSM status error return

## Symptom

An SBI HSM test expected `hart_get_status` for an absent hart to return
`SBI_ERR_INVALID_PARAM`, but the server kernel returned success with value
zero. Valid hart state queries worked normally.

## Cause

In Linux 6.18.3, `kvm_sbi_hsm_vcpu_get_status()` returns the correct negative
error. The surrounding HSM handler only fills the SBI return structure when
that result is nonnegative, then returns without copying the negative value to
`err_val`. The zero-initialized SBI result therefore reaches the guest as
success.

This is a host-kernel behavior, not a userspace register synchronization or
Guest ABI error.

## Test policy

VART validates the invalid-target error through `hart_start`, whose KVM handler
does propagate `SBI_ERR_INVALID_PARAM`. The HSM lifecycle test does not encode
the erroneous `hart_get_status` behavior as a required VART contract. Revisit
this case when the server kernel is updated.

## Stable reproducer

The `repro/riscv-kvm-hsm-status-error` branch intentionally changes the HSM
guest back to querying absent hart 99. Build and run only the focused test:

```sh
make build/tests/kvm-sbi-hsm build/guests/sbi/hsm.bin
build/tests/kvm-sbi-hsm build/guests/sbi/hsm.bin
```

The reproducer expects `SBI_ERR_INVALID_PARAM` in `a0`. On the affected kernel,
KVM instead returns `a0 = 0` and `a1 = 0`, so the guest reports FAIL through
the test device and the host test exits with `Input/output error`. This failure
is intentional on the reproducer branch. The normal `main` branch remains
green and checks the invalid-target error through `hart_start`.
