#include "../../fixtures/smp-shared.h"

/* Coordinate two harts entirely through shared RAM and RV64 atomics. */

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    li t0, VART_SMP_HARTS
    bgeu a0, t0, fail

    li t0, VART_SMP_SHARED_GPA
    li t1, 1
    amoadd.w.aqrl zero, t1, (t0)

wait_arrived:
    lw t2, VART_SMP_ARRIVED_OFFSET(t0)
    li t3, VART_SMP_HARTS
    bne t2, t3, wait_arrived
    fence rw, rw

    addi t2, t0, VART_SMP_COUNTER_OFFSET
    li t3, VART_SMP_ITERATIONS
count:
    amoadd.d.aqrl zero, t1, (t2)
    addi t3, t3, -1
    bnez t3, count

    addi t2, t0, VART_SMP_MARKERS_OFFSET
    slli t3, a0, 3
    add t2, t2, t3
    addi t3, a0, 1
    sd t3, 0(t2)
    fence rw, rw

    addi t2, t0, VART_SMP_FINISHED_OFFSET
    amoadd.w.aqrl zero, t1, (t2)
    bnez a0, secondary_idle

wait_finished:
    lw t2, VART_SMP_FINISHED_OFFSET(t0)
    li t3, VART_SMP_HARTS
    bne t2, t3, wait_finished
    fence rw, rw

    ld t2, VART_SMP_COUNTER_OFFSET(t0)
    li t3, VART_SMP_ITERATIONS * VART_SMP_HARTS
    bne t2, t3, fail
    ld t2, VART_SMP_MARKERS_OFFSET(t0)
    bne t2, t1, fail
    ld t2, VART_SMP_MARKERS_OFFSET + 8(t0)
    li t3, 2
    bne t2, t3, fail

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
