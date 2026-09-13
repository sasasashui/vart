#include "../../fixtures/smp-hart-state.h"

/* Ask the host to start and stop a secondary hart through MP state. */

    .section .text.entry, "ax", @progbits
    .globl _start
    .type _start, @function
_start:
    beqz a0, primary
    li t0, 1
    bne a0, t0, fail

    li t0, VART_HART_STATE_SHARED_GPA
    li t1, 1
    sw t1, VART_HART_STATE_MARKER_OFFSET(t0)
    fence rw, rw
    li t0, 0x00100000
    li t1, VART_HART_STATE_STOP_COMMAND
    sb t1, 0(t0)
secondary_should_stop:
    j secondary_should_stop

primary:
    li t0, VART_HART_STATE_SHARED_GPA
wait_primary_gate:
    lw t1, VART_HART_STATE_PRIMARY_GATE_OFFSET(t0)
    beqz t1, wait_primary_gate
    fence rw, rw
    lw t1, VART_HART_STATE_MARKER_OFFSET(t0)
    bnez t1, fail

    li t2, 0x00100000
    li t3, VART_HART_STATE_START_COMMAND
    sb t3, 0(t2)

wait_marker:
    lw t1, VART_HART_STATE_MARKER_OFFSET(t0)
    li t3, 1
    bne t1, t3, wait_marker
    fence rw, rw
wait_stopped:
    lw t1, VART_HART_STATE_STOPPED_ACK_OFFSET(t0)
    bne t1, t3, wait_stopped
    fence rw, rw

    li t1, 1
    sw t1, 16(t2)
primary_idle:
    j primary_idle

fail:
    li t0, 0x00100000
    li t1, 2
    sw t1, 16(t0)
    j primary_idle
    .size _start, . - _start
