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
    li t3, 0x10000000
    la t4, message
3:
    lbu t5, 0(t4)
    beqz t5, 4f
5:
    lbu t6, 5(t3)
    andi t6, t6, 0x20
    beqz t6, 5b
    sb t5, 0(t3)
    addi t4, t4, 1
    j 3b
4:
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
message:
    .asciz "Hello from VART UART\n"
    .size _start, . - _start
