/* Validate the Linux boot ABI and the FDT header passed in a1. */

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    bnez a0, fail
    beqz a1, fail

    lwu t0, 0(a1)
    /* FDT fields are big-endian, so an RV64 little-endian load sees this. */
    li t1, 0xedfe0dd0
    bne t0, t1, fail
    lwu t0, 4(a1)
    beqz t0, fail

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
