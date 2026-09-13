# RISC-V register state

## Purpose

Each vCPU owns a userspace copy of its architectural state. This provides one
place for boot initialization now and a foundation for register dumps,
debuggers, snapshots, and migration later.

## Register groups

The first implementation separates state into CORE, CSR, and TIMER groups.
CORE contains PC, x0 through x31, and the KVM privilege mode. CSR contains the
general supervisor CSRs exposed by the current kernel UAPI. TIMER contains
frequency, time, compare, and state.

Floating-point, vector, AIA CSR, and optional extension state will be separate
groups when VART needs them. Keeping large optional state out of the common
path avoids unnecessary ioctls.

## Synchronization

`vart_riscv_vcpu_get_registers()` copies selected groups from KVM and marks
them valid and clean. `vart_riscv_cpu_state_mark_dirty()` accepts only valid
groups. `vart_riscv_vcpu_put_registers()` writes the selected dirty groups and
clears each dirty bit only after the complete group succeeds.

The older direct PC, GPR, and privilege-mode setters invalidate cached CORE
state after a successful ioctl. Callers must synchronize again before editing
that group; this prevents a later grouped write from restoring stale values.

The public synchronization functions take the VM big lock and reject a vCPU
whose worker state is RUNNING. A control thread must not issue register ioctls
against a vCPU that may be inside `KVM_RUN`. A later debugger pause protocol
will kick the worker and establish an explicit stopped-at-exit state before
using these interfaces.

The timer frequency is read-only. Following QEMU's RISC-V KVM handling, VART
also skips writing the timer state when it is OFF because the current KVM ABI
rejects that write. Time and compare remain writable for future state restore.

## Validation

`tests/integration/kvm/riscv-registers.c` reads every initial group from a real
vCPU, modifies CORE and CSR state, performs a grouped write and readback, and
checks valid/dirty tracking and invalid inputs. It also exercises an unchanged
TIMER writeback without attempting to overwrite its frequency.

Direct-boot initialization consumes this state through the interface documented
in `docs/features/riscv-direct-boot.md`.
