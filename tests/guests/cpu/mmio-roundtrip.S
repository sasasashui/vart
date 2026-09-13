/* Exercise the complete VART test-device interface through KVM MMIO exits. */

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    li t0, 0x00100000
    li t1, 0x11223344
    sd t1, 8(t0)
    ld t2, 8(t0)
    bne t1, t2, 1f
    li t1, 'O'
    sb t1, 0(t0)
    li t1, 'K'
    sb t1, 0(t0)
    li t1, 1
    sw t1, 16(t0)
2:
    j 2b
1:
    li t1, 2
    sw t1, 16(t0)
    j 2b
    .size _start, . - _start
