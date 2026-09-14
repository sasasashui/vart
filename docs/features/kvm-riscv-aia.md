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
`KVM_DEV_RISCV_AIA_CTRL_INIT`. The IMSIC initializer added in Stage 7.2 sets
the interrupt identity count, zero guest-index bits, every vCPU interrupt-file
address, and the required hart-index bits before issuing that command. The KVM
ABI freezes configuration at initialization, so repeated initialization is
rejected.

Userspace injects an MSI with `KVM_SIGNAL_MSI`, addressing the selected vCPU's
interrupt file and placing the interrupt identity in the data field. The API
checks the target vCPU and identity against the initialized topology.

## Reference and validation

The lifecycle follows `kvm_riscv_aia_create()` in QEMU's
`target/riscv/kvm/kvm-cpu.c`; the Linux UAPI remains authoritative. The KVM
integration tests validate rejected arguments, device creation, mode
round-tripping, required configuration attributes, and cleanup on the RISC-V
host. A bare-metal S-mode guest enables one IMSIC identity through `siselect`
and `sireg`, receives a userspace MSI as a supervisor external interrupt, and
claims it through `stopei`.

The SMP test configures adjacent interrupt files for two runnable vCPUs and
checks the one-bit hart index selected by KVM. Both harts enable the same
identity. Userspace first targets hart 1 and verifies that hart 0 remains
untouched, then targets hart 0; shared atomic counters record the recipient
independently of the common interrupt identity.
