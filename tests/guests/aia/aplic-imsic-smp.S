#include "../../fixtures/aia-aplic-smp.h"

/* Route two APLIC sources to different hart IMSIC files. */

    .equ CSR_SISELECT, 0x150
    .equ CSR_SIREG, 0x151
    .equ CSR_STOPEI, 0x15c
    .equ APLIC_BASE, 0x0d000000

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    li t0, VART_AIA_APLIC_HARTS
    bgeu a0, t0, fail
    csrw sscratch, a0
    la t0, interrupt
    csrw stvec, t0

    li t0, 0x70
    csrw CSR_SISELECT, t0
    li t1, 1
    csrw CSR_SIREG, t1
    li t0, 0x72
    csrw CSR_SISELECT, t0
    csrw CSR_SIREG, zero
    li t0, 0xc0
    csrw CSR_SISELECT, t0
    li t1, 6
    csrw CSR_SIREG, t1

    bnez a0, enable_cpu_interrupts
    li t0, APLIC_BASE
    li t1, 4
    sw t1, 4(t0)
    sw t1, 8(t0)
    li t1, (1 << 18) | 1
    li t2, 0x3004
    add t2, t0, t2
    sw t1, 0(t2)
    li t1, 2
    sw t1, 4(t2)
    li t2, 0x1edc
    add t2, t0, t2
    li t1, VART_AIA_APLIC_SOURCE_HART1
    sw t1, 0(t2)
    li t1, VART_AIA_APLIC_SOURCE_HART0
    sw t1, 0(t2)
    li t1, 0x104
    sw t1, 0(t0)

enable_cpu_interrupts:
    li t0, 1 << 9
    csrs sie, t0
    li t0, 1 << 1
    csrs sstatus, t0
    li t0, VART_AIA_APLIC_SHARED_GPA
    li t1, 1
    sll t1, t1, a0
    amoor.w.aqrl zero, t1, (t0)

wait:
    lw t1, VART_AIA_APLIC_RECEIVED_OFFSET(t0)
    lw t2, VART_AIA_APLIC_RECEIVED_OFFSET + 4(t0)
    bnez a0, wait_interrupt
    li t3, 2
    bne t1, t3, wait_interrupt
    li t3, 1
    bne t2, t3, wait_interrupt
    lw t1, VART_AIA_APLIC_ERROR_OFFSET(t0)
    bnez t1, fail
    li t0, 0x00100000
    li t1, 1
    sw t1, 16(t0)
idle:
    j idle

wait_interrupt:
    wfi
    li t0, VART_AIA_APLIC_SHARED_GPA
    csrr a0, sscratch
    j wait

    .align 2
interrupt:
    csrr t0, scause
    li t1, 1
    slli t1, t1, 63
    ori t1, t1, 9
    bne t0, t1, interrupt_fail
    csrr t0, CSR_STOPEI
    srli t3, t0, 16
    li t1, 1
    bltu t3, t1, interrupt_fail
    li t1, 2
    bgtu t3, t1, interrupt_fail
    csrw CSR_STOPEI, t0
    csrr t1, sscratch
    li t2, VART_AIA_APLIC_SHARED_GPA + VART_AIA_APLIC_RECEIVED_OFFSET
    slli t1, t1, 2
    add t2, t2, t1
    amoswap.w.aqrl zero, t3, (t2)
    sret

interrupt_fail:
    li t0, VART_AIA_APLIC_SHARED_GPA + VART_AIA_APLIC_ERROR_OFFSET
    li t1, 1
    amoswap.w.aqrl zero, t1, (t0)
    sret

fail:
    li t0, 0x00100000
    li t1, 2
    sw t1, 16(t0)
    j idle
    .size _start, . - _start
