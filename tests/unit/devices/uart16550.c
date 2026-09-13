#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/devices/uart16550.h"

static void capture(void *opaque, unsigned char value)
{
    *(unsigned char *)opaque = value;
}

int main(void)
{
    VartAddressSpace as;
    VartUart16550 uart;
    unsigned char output = 0;
    uint64_t value;

    vart_address_space_init(&as);
    if (vart_uart16550_init(&uart, 0x1000, capture, &output) < 0 ||
        vart_address_space_add(&as, &uart.region) < 0 ||
        vart_address_space_read(&as, 0x1005, 1, &value) < 0 || value != 0x60 ||
        vart_address_space_write(&as, 0x1000, 1, 'A') < 0 || output != 'A' ||
        vart_address_space_write(&as, 0x1003, 1, 0x83) < 0 ||
        vart_address_space_write(&as, 0x1000, 1, 0x34) < 0 ||
        vart_address_space_write(&as, 0x1001, 1, 0x12) < 0 ||
        vart_address_space_read(&as, 0x1000, 1, &value) < 0 || value != 0x34 ||
        vart_address_space_read(&as, 0x1001, 1, &value) < 0 || value != 0x12) {
        return EXIT_FAILURE;
    }
    vart_address_space_write(&as, 0x1003, 1, 0x03);
    vart_address_space_write(&as, 0x1001, 1, 0xff);
    vart_address_space_write(&as, 0x1002, 1, 0xff);
    vart_address_space_write(&as, 0x1004, 1, 0xff);
    vart_address_space_write(&as, 0x1007, 1, 0x5a);
    if (uart.ier != 0x0f || uart.fcr != 0xc9 || uart.mcr != 0x1f ||
        uart.scr != 0x5a ||
        vart_address_space_read(&as, 0x1000, 2, &value) != -EINVAL) {
        return EXIT_FAILURE;
    }
    vart_uart16550_reset(&uart);
    if (uart.iir != 1 || uart.lsr != 0x60 || uart.divisor != 0 ||
        uart.output_opaque != &output) {
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&as);
    puts("ok - emulate polling 16550 UART registers");
    return EXIT_SUCCESS;
}
