#include "../../fixtures/sbi-userspace.h"

/* Validate default and registered userspace SBI extension handling. */

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    li a0, 1
    li a1, 2
    li a2, 3
    li a3, 4
    li a4, 5
    li a5, 6
    li a6, VART_SBI_USER_FUNCTION
    li a7, VART_SBI_USER_EXPERIMENTAL_EXT
    ecall
    li t0, VART_SBI_USER_ERR_NOT_SUPPORTED
    bne a0, t0, fail
    bnez a1, fail

    li a0, 6
    li a1, 5
    li a2, 4
    li a3, 3
    li a4, 2
    li a5, 1
    li a6, VART_SBI_USER_FUNCTION
    li a7, VART_SBI_USER_VENDOR_EXT
    ecall
    bnez a0, fail
    li t0, VART_SBI_USER_VALUE
    bne a1, t0, fail

    li t0, 0x00100000
    li t1, 1
    sw t1, 16(t0)
idle:
    j idle

fail:
    li t0, 0x00100000
    li t1, 2
    sw t1, 16(t0)
    j idle
    .size _start, . - _start
