#include "../../fixtures/sbi-base.h"

/* Exercise the SBI BASE implementation provided by the host kernel KVM. */

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    li s0, VART_SBI_BASE_RESULTS_GPA

    li a6, 0
    call sbi_base_call
    bnez a0, fail
    sd a1, VART_SBI_BASE_SPEC_OFFSET(s0)
    srli t0, a1, 24
    bnez t0, spec_ok
    li t0, 2
    bltu a1, t0, fail
spec_ok:
    li a6, 1
    call sbi_base_call
    bnez a0, fail
    sd a1, VART_SBI_BASE_IMPL_ID_OFFSET(s0)
    li t0, VART_SBI_IMPL_ID_KVM
    bne a1, t0, fail

    li a6, 2
    call sbi_base_call
    bnez a0, fail
    sd a1, VART_SBI_BASE_IMPL_VERSION_OFFSET(s0)

    li a0, VART_SBI_EXT_TIME
    li a6, 3
    call sbi_base_call
    bnez a0, fail
    sd a1, VART_SBI_BASE_TIME_PROBE_OFFSET(s0)
    li t0, 1
    bne a1, t0, fail

    li a0, VART_SBI_EXT_UNKNOWN
    li a6, 3
    call sbi_base_call
    bnez a0, fail
    sd a1, VART_SBI_BASE_UNKNOWN_PROBE_OFFSET(s0)
    bnez a1, fail

    li a6, 4
    call sbi_base_call
    bnez a0, fail
    sd a1, VART_SBI_BASE_MVENDORID_OFFSET(s0)

    li a6, 5
    call sbi_base_call
    bnez a0, fail
    sd a1, VART_SBI_BASE_MARCHID_OFFSET(s0)

    li a6, 6
    call sbi_base_call
    bnez a0, fail
    sd a1, VART_SBI_BASE_MIMPID_OFFSET(s0)

    li a6, 7
    call sbi_base_call
    li t0, VART_SBI_ERR_NOT_SUPPORTED
    bne a0, t0, fail

    li t0, 0x00100000
    li t1, 1
    sw t1, 16(t0)
idle:
    j idle

sbi_base_call:
    li a7, VART_SBI_EXT_BASE
    ecall
    ret

fail:
    li t0, 0x00100000
    li t1, 2
    sw t1, 16(t0)
    j idle
    .size _start, . - _start
