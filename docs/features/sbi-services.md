# SBI TIME, IPI, and RFENCE under KVM

## Purpose

Linux uses SBI TIME for supervisor timer programming and SBI IPI and RFENCE for
cross-hart coordination. VART relies on the in-kernel KVM replacement
extensions instead of emulating them in userspace.

## KVM behavior

Linux `arch/riscv/kvm/vcpu_sbi_replace.c` implements these extensions. TIME
programs the calling vCPU's virtual timer. IPI interprets `a0` as a hart mask
relative to the hart-base in `a1` and raises a supervisor software interrupt on
each selected vCPU. RFENCE performs host hfence operations for the selected
guest harts before returning.

Remote fence-I, sfence.vma, and sfence.vma with ASID are supported. The HFENCE
functions used for nested virtualization return `SBI_ERR_NOT_SUPPORTED`.
Standard calls do not require a VART userspace SBI handler.

## Validation

`tests/guests/sbi/services.S` installs an S-mode trap handler on two runnable
harts. Hart zero programs an immediate timer and observes a supervisor timer
interrupt. It sends an IPI with mask 1 and base 1, and hart one observes and
clears the supervisor software interrupt. Hart zero then issues each supported
RFENCE operation against hart one and verifies that a nested HFENCE operation
is rejected. The trap handler rejects a timer on hart one or an IPI on hart
zero. Invalid TIME functions and IPI masks targeting a nonexistent hart must
also return their specified errors.

Both harts are made runnable directly by the host test because SBI HSM startup
belongs to the next stage. The integration handler accepts only hart zero's
final test-device MMIO exit; any userspace SBI exit or secondary-hart exit is a
failure. Shared counters provide deterministic host-side validation.

These timer and software interrupts are per-vCPU SBI/KVM services and do not
exercise the APLIC-to-IMSIC device interrupt topology planned for Stage 7.
