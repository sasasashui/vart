# RISC-V KVM direct boot

## Purpose

RISC-V KVM does not enter guest OpenSBI. VART initializes each KVM vCPU to the
architectural state expected by a kernel entered directly in S-mode. This
matches QEMU `virt` KVM behavior in `hw/riscv/virt.c` and
`target/riscv/kvm/kvm-cpu.c`.

## Contract

`vart_riscv_vcpu_init_boot()` clears the CORE and general CSR register groups,
sets PC to the image entry, passes the KVM hart ID in `a0` and the FDT guest
physical address in `a1`, selects S-mode, and synchronizes both groups to KVM.
Hart zero becomes RUNNABLE. Secondary harts become STOPPED and remain available
for later SBI HSM startup.

The entry must be nonzero and at least two-byte aligned. The FDT address must be
nonzero and eight-byte aligned. Linux-specific image alignment and placement
constraints belong to the loader rather than this architectural interface.

Initialization holds the VM big lock and rejects a running vCPU. If an ioctl
fails, dirty bits remain set for the group that did not complete so the caller
does not mistake partially synchronized state for a clean snapshot.

## Validation

`tests/integration/kvm/riscv-direct-boot.c` validates KVM-visible register and
MP state for primary and secondary harts. The `cpu/direct-boot.S` tiny guest
saves its first PC, checks `a0`, `a1`, and reset GPRs, and reads `sstatus` to
prove S-mode execution before reporting PASS through the test device.
