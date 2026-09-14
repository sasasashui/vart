# KVM RISC-V AIA

## Purpose

VART uses the in-kernel RISC-V AIA device for both APLIC and IMSIC emulation.
Userspace owns device lifecycle, topology configuration, and device interrupt
wiring, but does not reproduce interrupt-controller register semantics.

## Lifecycle and mode

The generic KVM device wrapper owns the file descriptor returned by
`KVM_CREATE_DEVICE` and provides checked access to device attributes. The AIA
wrapper creates `KVM_DEV_TYPE_RISCV_AIA`, selects one of the kernel ABI's
emulated, hardware-accelerated, or automatic modes, and reads back the active
mode. Closing the descriptor releases the device before its VM is destroyed.

Stage 7.1 deliberately stops before setting topology addresses or issuing
`KVM_DEV_RISCV_AIA_CTRL_INIT`. Later increments must finish all configuration
before initialization because the KVM ABI freezes the configuration at that
point.

## Reference and validation

The lifecycle follows `kvm_riscv_aia_create()` in QEMU's
`target/riscv/kvm/kvm-cpu.c`; the Linux UAPI remains authoritative. The KVM
integration test validates rejected arguments, device creation, mode
round-tripping, required configuration attributes, and cleanup on the RISC-V
host.
