# SBI BASE under KVM

## Purpose

VART relies on the host kernel's RISC-V KVM SBI implementation instead of
loading OpenSBI or emulating standard SBI calls in userspace. SBI BASE is the
first guest-level check of that contract.

## KVM and userspace boundary

Linux `arch/riscv/kvm/vcpu_sbi_base.c` handles standard BASE functions in the
kernel. Its extension probe forwards only experimental or vendor extension
ranges to userspace. QEMU's RISC-V KVM exit handler implements legacy console
and optional DBCN forwarding, but does not reimplement BASE.

VART therefore adds no BASE dispatcher. A `KVM_EXIT_RISCV_SBI` during the test
is treated as a failure. Userspace SBI exit decoding is deferred until VART
needs the forwarded console and DBCN paths.

## Guest validation

The tiny guest calls every standard BASE function:

- get specification version;
- get implementation ID and version;
- probe TIME and an unknown extension;
- get `mvendorid`, `marchid`, and `mimpid`;
- call an unknown BASE function and require `SBI_ERR_NOT_SUPPORTED`.

It requires implementation ID 3, assigned to KVM, TIME availability, and a
zero result for the unknown extension. Hardware IDs are recorded but may
legitimately be zero. Results are written to shared RAM and checked again by
the host integration test before cleanup.

## Validation

`tests/integration/kvm/sbi-base.c` starts the guest through VART's direct-boot
interface and accepts only the final test-device MMIO exit. This proves all
preceding BASE ecalls completed in the kernel. It prints the reported SBI
specification and KVM implementation versions for diagnostics.
