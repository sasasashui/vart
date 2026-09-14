/* Receive one byte through UART interrupt source 10. */

    .equ CSR_SISELECT, 0x150
    .equ CSR_SIREG, 0x151
    .equ CSR_STOPEI, 0x15c
    .equ APLIC_BASE, 0x0d000000
    .equ UART_BASE, 0x10000000
    .equ TEST_BASE, 0x00100000

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
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
    li t1, 1 << 10
    csrw CSR_SIREG, t1

    li t0, APLIC_BASE
    li t1, 6
    li t2, 10 * 4
    add t2, t0, t2
    sw t1, 0(t2)
    li t1, 10
    li t2, 0x1edc
    add t2, t0, t2
    sw t1, 0(t2)
    li t1, 9
    li t2, 0x3004
    add t2, t0, t2
    slli t1, t1, 2
    add t2, t2, t1
    li t1, 10
    sw t1, 0(t2)
    li t1, 0x104
    sw t1, 0(t0)

    li t0, 1 << 9
    csrs sie, t0
    li t0, 1 << 1
    csrs sstatus, t0
    li t0, UART_BASE
    li t1, 1
    sb t1, 1(t0)
wait:
    wfi
    j wait

    .align 2
interrupt:
    csrr t0, scause
    li t1, 1
    slli t1, t1, 63
    ori t1, t1, 9
    bne t0, t1, fail
    csrr t0, CSR_STOPEI
    srli t1, t0, 16
    li t2, 10
    bne t1, t2, fail
    li t2, UART_BASE
    lbu t3, 0(t2)
    li t4, 'V'
    bne t3, t4, fail
    lbu t3, 5(t2)
    andi t3, t3, 1
    bnez t3, fail
    csrw CSR_STOPEI, t0
    li t0, TEST_BASE
    li t1, 1
    sw t1, 16(t0)
idle:
    j idle

fail:
    li t0, TEST_BASE
    li t1, 2
    sw t1, 16(t0)
    j idle
    .size _start, . - _start
