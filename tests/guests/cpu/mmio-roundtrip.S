/* Verify that the host completes an MMIO read before execution resumes. */

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    li t0, 0x10000000
    lw t1, 0(t0)
    li t2, 0x12345678
    bne t1, t2, 1f
    sw t1, 8(t0)
2:
    j 2b
1:
    li t1, 0xdead
    sw t1, 8(t0)
    j 2b
    .size _start, . - _start
