#include "../../fixtures/sbi-srst.h"

#ifndef VART_SBI_SRST_TYPE
#error "VART_SBI_SRST_TYPE must select the reset request"
#endif

#ifndef VART_SBI_SRST_REASON
#error "VART_SBI_SRST_REASON must select the reset reason"
#endif

/* Request a system event from KVM; a supported SRST call never returns. */

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    li a0, VART_SBI_SRST_TYPE
    li a1, VART_SBI_SRST_REASON
    li a6, VART_SBI_SRST_RESET
    li a7, VART_SBI_EXT_SRST
    ecall

    li t0, 0x00100000
    li t1, 2
    sw t1, 16(t0)
idle:
    j idle
    .size _start, . - _start
