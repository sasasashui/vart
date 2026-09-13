#include "../../fixtures/sbi-hsm.h"

/* Exercise the in-kernel SBI HSM implementation across two hart starts. */

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    bnez a0, fail
    li s0, VART_SBI_HSM_SHARED_GPA

    li a0, 1
    li a6, VART_SBI_HSM_HART_STATUS
    li a7, VART_SBI_EXT_HSM
    ecall
    bnez a0, fail
    li t0, VART_SBI_HSM_STATE_STOPPED
    bne a1, t0, fail
    li a0, 99
    la a1, secondary_entry
    li a2, VART_SBI_HSM_OPAQUE_FIRST
    li a6, VART_SBI_HSM_HART_START
    li a7, VART_SBI_EXT_HSM
    ecall
    li t0, VART_SBI_ERR_INVALID_PARAM
    bne a0, t0, fail

    li a6, 99
    li a7, VART_SBI_EXT_HSM
    ecall
    li t0, VART_SBI_ERR_NOT_SUPPORTED
    bne a0, t0, fail

    li a0, 1
    la a1, secondary_entry
    li a2, VART_SBI_HSM_OPAQUE_FIRST
    li a6, VART_SBI_HSM_HART_START
    li a7, VART_SBI_EXT_HSM
    ecall
    bnez a0, fail

wait_first_start:
    lw t0, VART_SBI_HSM_STARTS_OFFSET(s0)
    li t1, 1
    bne t0, t1, wait_first_start
    fence rw, rw
    ld t0, VART_SBI_HSM_HART_ID_OFFSET(s0)
    bne t0, t1, fail
    ld t0, VART_SBI_HSM_OPAQUE_OFFSET(s0)
    li t1, VART_SBI_HSM_OPAQUE_FIRST
    bne t0, t1, fail
    ld t0, VART_SBI_HSM_ENTRY_OFFSET(s0)
    la t1, secondary_entry
    bne t0, t1, fail

    li a0, 1
    li a6, VART_SBI_HSM_HART_STATUS
    li a7, VART_SBI_EXT_HSM
    ecall
    bnez a0, fail
    li t0, VART_SBI_HSM_STATE_STARTED
    bne a1, t0, fail

    li a0, 1
    la a1, secondary_entry
    li a2, VART_SBI_HSM_OPAQUE_FIRST
    li a6, VART_SBI_HSM_HART_START
    li a7, VART_SBI_EXT_HSM
    ecall
    li t0, VART_SBI_ERR_ALREADY_AVAILABLE
    bne a0, t0, fail

    li t0, 1
    sw t0, VART_SBI_HSM_STOP_REQUEST_OFFSET(s0)
    fence rw, rw
wait_first_stop:
    li a0, 1
    li a6, VART_SBI_HSM_HART_STATUS
    li a7, VART_SBI_EXT_HSM
    ecall
    bnez a0, fail
    li t0, VART_SBI_HSM_STATE_STOPPED
    bne a1, t0, wait_first_stop

    li a0, 1
    la a1, secondary_entry
    li a2, VART_SBI_HSM_OPAQUE_SECOND
    li a6, VART_SBI_HSM_HART_START
    li a7, VART_SBI_EXT_HSM
    ecall
    bnez a0, fail

wait_second_start:
    lw t0, VART_SBI_HSM_STARTS_OFFSET(s0)
    li t1, 2
    bne t0, t1, wait_second_start
    fence rw, rw
    ld t0, VART_SBI_HSM_OPAQUE_OFFSET(s0)
    li t1, VART_SBI_HSM_OPAQUE_SECOND
    bne t0, t1, fail

    li t0, 2
    sw t0, VART_SBI_HSM_STOP_REQUEST_OFFSET(s0)
    fence rw, rw
wait_second_stop:
    li a0, 1
    li a6, VART_SBI_HSM_HART_STATUS
    li a7, VART_SBI_EXT_HSM
    ecall
    bnez a0, fail
    li t0, VART_SBI_HSM_STATE_STOPPED
    bne a1, t0, wait_second_stop

    lw t0, VART_SBI_HSM_STOP_RETURNED_OFFSET(s0)
    bnez t0, fail
    li t0, 0x00100000
    li t1, 1
    sw t1, 16(t0)
primary_idle:
    j primary_idle

    .align 2
    .globl secondary_entry
secondary_entry:
    auipc t6, 0
    li s0, VART_SBI_HSM_SHARED_GPA
    sd a0, VART_SBI_HSM_HART_ID_OFFSET(s0)
    sd a1, VART_SBI_HSM_OPAQUE_OFFSET(s0)
    sd t6, VART_SBI_HSM_ENTRY_OFFSET(s0)
    li t0, 1
    amoadd.w.aqrl t1, t0, (s0)
    addi t1, t1, 1
secondary_wait:
    lw t0, VART_SBI_HSM_STOP_REQUEST_OFFSET(s0)
    blt t0, t1, secondary_wait
    fence rw, rw
    li a6, VART_SBI_HSM_HART_STOP
    li a7, VART_SBI_EXT_HSM
    ecall
    li t0, 1
    sw t0, VART_SBI_HSM_STOP_RETURNED_OFFSET(s0)
    j fail

fail:
    li t0, 0x00100000
    li t1, 2
    sw t1, 16(t0)
    j primary_idle
    .size _start, . - _start
