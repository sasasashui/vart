#include "../../fixtures/riscv-direct-boot.h"

/* Validate the register state received through the KVM direct-boot path. */

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    auipc t6, 0
    li t0, VART_DIRECT_BOOT_ENTRY
    bne t6, t0, fail
    bnez a0, fail
    li t0, VART_DIRECT_BOOT_FDT_ADDR
    bne a1, t0, fail
    bnez ra, fail
    bnez sp, fail
    bnez gp, fail
    bnez tp, fail
    bnez a2, fail
    bnez a7, fail

    /* This instruction traps instead of reaching PASS if KVM starts U-mode. */
    csrr t1, sstatus
    li t0, VART_DIRECT_BOOT_DEVICE
    li t1, 1
    sw t1, 16(t0)
idle:
    j idle

fail:
    li t0, VART_DIRECT_BOOT_DEVICE
    li t1, 2
    sw t1, 16(t0)
    j idle
    .size _start, . - _start
