/* Emit one deterministic store that KVM returns as an MMIO exit. */

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    li t0, 0x10000000
    li t1, 0x12345678
    sw t1, 0(t0)

    /* Reaching this loop means the host incorrectly completed the MMIO exit. */
1:
    j 1b
    .size _start, . - _start
