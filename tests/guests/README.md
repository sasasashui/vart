# Tiny guest tests

This directory contains minimal RISC-V guest source programs used before and
alongside full Linux boot tests. Group guests by the subsystem they validate,
such as CPU state, MMIO, interrupts, SMP, or DMA.

Guest sources are committed; toolchain output is written under `build/guests/`.

The `cpu/spin.S` guest deliberately executes an infinite loop. Host-side kick
tests use it to prove that a vCPU blocked in `KVM_RUN` can be interrupted and
stopped without relying on a guest MMIO exit.

The `cpu/direct-boot.S` guest validates the KVM S-mode entry PC, hart ID, FDT
address, and reset GPR state established by VART.

The `smp/shared-atomic.S` guest uses RISC-V acquire/release AMOs and barriers to
coordinate two harts through shared RAM. Its layout is shared with the host test
through `tests/fixtures/smp-shared.h`.

The `smp/concurrent-mmio.S` guest starts both harts at a shared-RAM barrier and
then drives the same MMIO output register from both vCPUs. It validates host-side
serialization of device callbacks under the VM big lock.

The `smp/hart-state.S` guest asks the host to transition a stopped secondary
hart to RUNNABLE and back to STOPPED. It validates lifecycle mechanics without
pretending that the test-device command channel is an SBI HSM interface.
