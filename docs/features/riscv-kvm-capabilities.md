# RISC-V KVM capabilities

## Purpose

The RISC-V boot path must distinguish generic KVM capabilities from features
advertised through architecture-specific vCPU registers. VART probes both
before relying on a direct S-mode Linux boot contract.

## Interface

`vart_kvm_open()` records generic capabilities reported by
`KVM_CHECK_EXTENSION`, including `KVM_CAP_RISCV_MP_STATE_RESET`.
`vart_riscv_kvm_probe_vcpu()` queries `KVM_GET_REG_LIST` on a newly created
vCPU and records the availability and current value of selected ISA and SBI
extension registers.

An extension register can be available but disabled. The two states remain
separate because some extensions, such as DBCN, can be exposed by KVM for the
VMM to enable before the vCPU first runs.

## Boot capability set

The initial probe reports:

- the core privilege-mode and timer-frequency registers;
- I, M, A, SSTC, and SSAIA ISA extension state;
- legacy SBI 0.1, TIME, IPI, RFENCE, SRST, HSM, and DBCN state;
- the RISC-V AIA device and reset MP-state capability.

I, M, A, TIME, IPI, RFENCE, SRST, and HSM are required by the integration test
on the development server. SSTC, SSAIA, DBCN, AIA, and reset MP-state remain
reported capabilities; later stages decide which are mandatory for a selected
machine configuration.

## Upstream contract

The register discovery follows QEMU's `target/riscv/kvm/kvm-cpu.c` use of
`KVM_GET_REG_LIST`. The installed kernel UAPI in `asm/kvm.h` defines the
architecture-specific register IDs. Linux KVM is authoritative for whether an
advertised extension is enabled.

QEMU's `hw/riscv/virt.c` rejects M-mode firmware with KVM and calls
`riscv_setup_direct_kernel()`. VART therefore treats direct S-mode entry as the
only KVM boot path; OpenSBI is not loaded into the guest.

## Validation

`tests/integration/kvm/riscv-capabilities.c` creates a real VM and vCPU, reads
its register list, verifies the core boot registers, and requires the server's
minimum ISA and SBI set. `vart --probe` prints optional and required feature
states for diagnosis.
