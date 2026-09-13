#include "../../fixtures/smp-mmio.h"

/* Drive the same MMIO device concurrently from two KVM vCPUs. */

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    li t0, VART_SMP_MMIO_HARTS
    bgeu a0, t0, fail

    li t0, VART_SMP_MMIO_SHARED_GPA
    li t1, 1
    amoadd.w.aqrl zero, t1, (t0)

wait_arrived:
    lw t2, VART_SMP_MMIO_ARRIVED_OFFSET(t0)
    li t3, VART_SMP_MMIO_HARTS
    bne t2, t3, wait_arrived
    fence rw, rw

    li t2, 0x00100000
    addi t3, a0, 'A'
    li t4, VART_SMP_MMIO_ITERATIONS
write_mmio:
    sb t3, 0(t2)
    addi t4, t4, -1
    bnez t4, write_mmio

    addi t2, t0, VART_SMP_MMIO_FINISHED_OFFSET
    amoadd.w.aqrl zero, t1, (t2)
    bnez a0, secondary_idle

wait_finished:
    lw t2, VART_SMP_MMIO_FINISHED_OFFSET(t0)
    li t3, VART_SMP_MMIO_HARTS
    bne t2, t3, wait_finished
    fence rw, rw

    li t0, 0x00100000
    li t1, 1
    sw t1, 16(t0)
primary_idle:
    j primary_idle

fail:
    li t0, 0x00100000
    li t1, 2
    sw t1, 16(t0)
    j primary_idle

secondary_idle:
    j secondary_idle
    .size _start, . - _start
