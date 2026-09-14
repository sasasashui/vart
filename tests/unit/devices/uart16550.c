#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/devices/uart16550.h"
#include "vart/machine/virt.h"

static void capture(void *opaque, unsigned char value)
{
    *(unsigned char *)opaque = value;
}

typedef struct IrqState {
    bool level;
    unsigned int changes;
} IrqState;

static int set_irq(void *opaque, uint32_t line, bool level)
{
    IrqState *state = opaque;

    if (line != VART_VIRT_UART_IRQ) {
        return -EINVAL;
    }
    state->level = level;
    state->changes++;
    return 0;
}

int main(void)
{
    VartAddressSpace as;
    VartUart16550 uart;
    IrqState irq_state = { 0 };
    VartIrq irq;
    unsigned char output = 0;
    uint64_t value;

    vart_address_space_init(&as);
    vart_irq_init(&irq, set_irq, &irq_state, VART_VIRT_UART_IRQ);
    if (vart_uart16550_init(&uart, VART_VIRT_UART_BASE,
                            capture, &output) < 0 ||
        vart_address_space_add(&as, &uart.region) < 0 ||
        vart_address_space_read(&as, VART_VIRT_UART_BASE + 5,
                                1, &value) < 0 || value != 0x60 ||
        vart_address_space_write(&as, VART_VIRT_UART_BASE, 1, 'A') < 0 ||
        output != 'A' ||
        vart_address_space_write(&as, VART_VIRT_UART_BASE + 3, 1, 0x83) < 0 ||
        vart_address_space_write(&as, VART_VIRT_UART_BASE, 1, 0x34) < 0 ||
        vart_address_space_write(&as, VART_VIRT_UART_BASE + 1,
                                 1, 0x12) < 0 ||
        vart_address_space_read(&as, VART_VIRT_UART_BASE,
                                1, &value) < 0 || value != 0x34 ||
        vart_address_space_read(&as, VART_VIRT_UART_BASE + 1,
                                1, &value) < 0 || value != 0x12) {
        return EXIT_FAILURE;
    }
    if (vart_address_space_write(&as, VART_VIRT_UART_BASE + 3,
                                 1, 0x03) < 0 ||
        vart_uart16550_connect_irq(NULL, &irq) != -EINVAL ||
        vart_uart16550_connect_irq(&uart, NULL) != -EINVAL ||
        vart_uart16550_connect_irq(&uart, &irq) < 0 ||
        vart_uart16550_receive(&uart, 'R') < 0 ||
        !(uart.lsr & VART_UART16550_LSR_DR) || irq_state.level ||
        vart_uart16550_receive(&uart, 'X') != -EAGAIN ||
        vart_address_space_write(&as, VART_VIRT_UART_BASE + 1, 1, 1) < 0 ||
        !irq_state.level || uart.iir != 4 || irq_state.changes != 1 ||
        vart_address_space_read(&as, VART_VIRT_UART_BASE, 1, &value) < 0 ||
        value != 'R' || irq_state.level || uart.iir != 1 ||
        (uart.lsr & VART_UART16550_LSR_DR) || irq_state.changes != 2) {
        return EXIT_FAILURE;
    }
    vart_address_space_write(&as, VART_VIRT_UART_BASE + 3, 1, 0x03);
    vart_address_space_write(&as, VART_VIRT_UART_BASE + 1, 1, 0xff);
    vart_address_space_write(&as, VART_VIRT_UART_BASE + 2, 1, 0xff);
    vart_address_space_write(&as, VART_VIRT_UART_BASE + 4, 1, 0xff);
    vart_address_space_write(&as, VART_VIRT_UART_BASE + 7, 1, 0x5a);
    if (uart.ier != 0x0f || uart.fcr != 0xc9 || uart.mcr != 0x1f ||
        uart.scr != 0x5a ||
        vart_address_space_read(&as, VART_VIRT_UART_BASE,
                                2, &value) != -EINVAL) {
        return EXIT_FAILURE;
    }
    vart_uart16550_reset(&uart);
    if (uart.iir != 1 || uart.lsr != 0x60 || uart.divisor != 0 ||
        uart.output_opaque != &output) {
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&as);
    puts("ok - emulate 16550 UART registers and receive interrupts");
    return EXIT_SUCCESS;
}
