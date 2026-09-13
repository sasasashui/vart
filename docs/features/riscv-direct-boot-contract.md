# RISC-V KVM direct-boot contract

## Scope

VART starts a Linux kernel directly in supervisor mode. It does not load
OpenSBI into the guest. The host must provide the complete contract below
before machine construction proceeds; discovering individual capabilities is
not enough because an advertised RISC-V register may still be disabled.

## Required host features

The common KVM requirements are user memory, one-reg access, immediate exit,
and MP state. RISC-V also requires register-list discovery, the core privilege
mode register, and the timer-frequency register.

The required ISA set is I, M, and A. The required in-kernel SBI set is TIME,
IPI, RFENCE, SRST, and HSM. These requirements cover the current SMP target and
its shutdown path. SSTC, SSAIA, DBCN, irqfd, ioeventfd, and the AIA device are
reported independently: some are optional accelerations, while AIA has its own
machine-construction checks in Stage 7.

`vart_riscv_kvm_validate_boot()` reports missing common, ISA, and SBI features
as separate bitmaps and returns `-ENOTSUP` if any required item is unavailable
or disabled. This lets a future command-line frontend print every missing item
instead of failing on the first one.

## Guest entry and runtime boundary

After validation, `vart_riscv_vcpu_init_boot()` establishes PC, S-mode, hart ID
in `a0`, FDT address in `a1`, cleared GPR/CSR state, and primary versus
secondary MP state. KVM implements standard BASE, TIME, IPI, RFENCE, HSM, and
SRST calls. VART handles system-event exits and only dispatches SBI extensions
that KVM explicitly forwards to userspace.

The focused Guest tests remain authoritative for runtime behavior. The
contract validator consolidates their prerequisites; it does not replace the
register, SMP, SBI, or exit-level tests.

## Validation

The host-only contract test constructs synthetic complete and incomplete
capability sets. It checks combined missing-feature reports, unavailable versus
disabled features, optional features, and invalid arguments. The KVM
capability integration test probes the real server and passes the resulting
state through the same validator.
