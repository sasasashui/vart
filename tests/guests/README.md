# Tiny guest tests

This directory contains minimal RISC-V guest source programs used before and
alongside full Linux boot tests. Group guests by the subsystem they validate,
such as CPU state, MMIO, interrupts, SMP, or DMA.

Guest sources are committed; toolchain output is written under `build/guests/`.

