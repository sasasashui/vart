# Tiny guest tests

This directory contains minimal RISC-V guest source programs used before and
alongside full Linux boot tests. Group guests by the subsystem they validate,
such as CPU state, MMIO, interrupts, SMP, or DMA.

Guest sources are committed; toolchain output is written under `build/guests/`.

The `cpu/spin.S` guest deliberately executes an infinite loop. Host-side kick
tests use it to prove that a vCPU blocked in `KVM_RUN` can be interrupted and
stopped without relying on a guest MMIO exit.
