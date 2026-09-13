#include "../../fixtures/sbi-services.h"

/* Validate KVM's in-kernel TIME, IPI, and RFENCE implementations. */

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    li t0, VART_SBI_SERVICES_HARTS
    bgeu a0, t0, fail
    mv s1, a0
    li s0, VART_SBI_SERVICES_SHARED_GPA

    li sp, VART_SBI_SERVICES_STACK_GPA
    slli t0, a0, 12
    add sp, sp, t0
    li t0, VART_SBI_SERVICES_STACK_SIZE
    add sp, sp, t0

    la t0, trap_entry
    csrw stvec, t0
    li t0, 0x22
    csrs sie, t0
    li t0, 0x2
    csrs sstatus, t0

    li t0, 1
    amoadd.w.aqrl zero, t0, (s0)
wait_ready:
    lw t1, VART_SBI_SERVICES_READY_OFFSET(s0)
    li t2, VART_SBI_SERVICES_HARTS
    bne t1, t2, wait_ready
    fence rw, rw
    bnez a0, secondary_wait

    rdtime a0
    addi a0, a0, 1
    li a6, 0
    li a7, VART_SBI_EXT_TIME
    ecall
    bnez a0, fail
wait_timer:
    lw t0, VART_SBI_SERVICES_TIMER_OFFSET(s0)
    beqz t0, wait_timer

    li a6, 1
    li a7, VART_SBI_EXT_TIME
    ecall
    li t0, VART_SBI_ERR_NOT_SUPPORTED
    bne a0, t0, fail

    li a0, 2
    li a1, 1
    li a6, 0
    li a7, VART_SBI_EXT_IPI
    ecall
    li t0, VART_SBI_ERR_INVALID_PARAM
    bne a0, t0, fail

    li a0, 1
    li a1, 1
    li a6, 0
    li a7, VART_SBI_EXT_IPI
    ecall
    bnez a0, fail
wait_ipi:
    lw t0, VART_SBI_SERVICES_IPI_OFFSET(s0)
    beqz t0, wait_ipi

    li a0, 1
    li a1, 1
    li a6, 0
    li a7, VART_SBI_EXT_RFENCE
    ecall
    bnez a0, fail

    li a0, 1
    li a1, 1
    li a2, 0
    li a3, 0
    li a6, 1
    li a7, VART_SBI_EXT_RFENCE
    ecall
    bnez a0, fail

    li a0, 1
    li a1, 1
    li a2, 0
    li a3, 0
    li a4, 0
    li a6, 2
    li a7, VART_SBI_EXT_RFENCE
    ecall
    bnez a0, fail

    li a0, 1
    li a1, 1
    li a6, 3
    li a7, VART_SBI_EXT_RFENCE
    ecall
    li t0, VART_SBI_ERR_NOT_SUPPORTED
    bne a0, t0, fail

    li t0, 1
    sw t0, VART_SBI_SERVICES_RFENCE_OFFSET(s0)
    fence rw, rw
    li t0, 0x00100000
    li t1, 1
    sw t1, 16(t0)
idle:
    j idle

secondary_wait:
    lw t0, VART_SBI_SERVICES_IPI_OFFSET(s0)
    beqz t0, secondary_wait
    j idle

    .align 2
trap_entry:
    addi sp, sp, -112
    sd ra, 0(sp)
    sd t0, 8(sp)
    sd t1, 16(sp)
    sd t2, 24(sp)
    sd t3, 32(sp)
    sd a0, 40(sp)
    sd a1, 48(sp)
    sd a2, 56(sp)
    sd a3, 64(sp)
    sd a4, 72(sp)
    sd a5, 80(sp)
    sd a6, 88(sp)
    sd a7, 96(sp)

    csrr t0, scause
    bgez t0, trap_fail
    slli t0, t0, 1
    srli t0, t0, 1
    li t1, 5
    beq t0, t1, timer_interrupt
    li t1, 1
    beq t0, t1, software_interrupt
    j trap_fail

timer_interrupt:
    bnez s1, trap_fail
    li t0, 0x20
    csrc sie, t0
    li t0, 1
    sw t0, VART_SBI_SERVICES_TIMER_OFFSET(s0)
    j trap_return

software_interrupt:
    li t0, 1
    bne s1, t0, trap_fail
    li t0, 0x2
    csrc sip, t0
    li t0, 1
    sw t0, VART_SBI_SERVICES_IPI_OFFSET(s0)

trap_return:
    ld ra, 0(sp)
    ld t0, 8(sp)
    ld t1, 16(sp)
    ld t2, 24(sp)
    ld t3, 32(sp)
    ld a0, 40(sp)
    ld a1, 48(sp)
    ld a2, 56(sp)
    ld a3, 64(sp)
    ld a4, 72(sp)
    ld a5, 80(sp)
    ld a6, 88(sp)
    ld a7, 96(sp)
    addi sp, sp, 112
    sret

trap_fail:
    li t0, 0x00100000
    li t1, 2
    sw t1, 16(t0)
    j idle

fail:
    li t0, 0x00100000
    li t1, 2
    sw t1, 16(t0)
    j idle
    .size _start, . - _start
