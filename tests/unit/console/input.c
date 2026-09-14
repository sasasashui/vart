#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/address-space.h"
#include "vart/console.h"
#include "vart/machine/virt.h"

typedef struct IrqSink {
    bool level;
    int error;
} IrqSink;

static int set_irq(void *opaque, uint32_t line, bool level)
{
    IrqSink *sink = opaque;

    if (line != VART_VIRT_UART_IRQ) {
        return -EINVAL;
    }
    if (sink->error < 0) {
        return sink->error;
    }
    sink->level = level;
    return 0;
}

static int consume(VartAddressSpace *as, VartConsole *console,
                   unsigned char expected)
{
    uint64_t value;
    int ret;

    ret = vart_address_space_read(as, VART_VIRT_UART_BASE, 1, &value);
    if (ret < 0 || value != expected) {
        return -EIO;
    }
    return vart_console_drain_input(console);
}

int main(void)
{
    unsigned char initial[VART_CONSOLE_INPUT_CAPACITY];
    unsigned char wrapped[128];
    VartAddressSpace address_space;
    VartUart16550 uart;
    VartConsole console;
    IrqSink sink = { 0 };
    VartIrq irq;
    size_t i;
    int ret;

    for (i = 0; i < sizeof(initial); i++) {
        initial[i] = (unsigned char)i;
    }
    for (i = 0; i < sizeof(wrapped); i++) {
        wrapped[i] = (unsigned char)(0xa0 + i);
    }
    vart_address_space_init(&address_space);
    if (vart_uart16550_init(&uart, VART_VIRT_UART_BASE, NULL, NULL) < 0 ||
        vart_irq_init(&irq, set_irq, &sink, VART_VIRT_UART_IRQ) < 0 ||
        vart_uart16550_connect_irq(&uart, &irq) < 0 ||
        vart_address_space_add(&address_space, &uart.region) < 0 ||
        vart_address_space_write(&address_space, VART_VIRT_UART_BASE + 1,
                                 1, 1) < 0 ||
        vart_console_init(NULL, &uart) != -EINVAL ||
        vart_console_init(&console, NULL) != -EINVAL ||
        vart_console_init(&console, &uart) < 0 ||
        vart_console_queue_input(NULL, initial, 1) != -EINVAL ||
        vart_console_queue_input(&console, NULL, 1) != -EINVAL ||
        vart_console_queue_input(&console, NULL, 0) != 0 ||
        vart_console_pending_input(&console) != 0 ||
        vart_console_input_space(&console) != VART_CONSOLE_INPUT_CAPACITY ||
        vart_console_queue_input(&console, initial, sizeof(initial)) !=
            (ssize_t)sizeof(initial) ||
        vart_console_queue_input(&console, initial, 1) != -EAGAIN ||
        vart_console_drain_input(&console) != 1 || !sink.level ||
        vart_console_pending_input(&console) != sizeof(initial) - 1 ||
        vart_console_drain_input(&console) != 0) {
        return EXIT_FAILURE;
    }
    for (i = 0; i + 1 < sizeof(wrapped); i++) {
        ret = consume(&address_space, &console, initial[i]);
        if (ret != 1) {
            return EXIT_FAILURE;
        }
    }
    if (vart_console_queue_input(&console, wrapped, sizeof(wrapped)) !=
            (ssize_t)sizeof(wrapped) ||
        vart_console_input_space(&console) != 0) {
        return EXIT_FAILURE;
    }
    for (i = sizeof(wrapped) - 1; i < sizeof(initial); i++) {
        ret = consume(&address_space, &console, initial[i]);
        if (ret != 1) {
            return EXIT_FAILURE;
        }
    }
    for (i = 0; i < sizeof(wrapped); i++) {
        ret = consume(&address_space, &console, wrapped[i]);
        if (ret != (i + 1 == sizeof(wrapped) ? 0 : 1)) {
            return EXIT_FAILURE;
        }
    }
    if (vart_console_pending_input(&console) != 0 ||
        vart_console_input_space(&console) != VART_CONSOLE_INPUT_CAPACITY ||
        sink.level) {
        return EXIT_FAILURE;
    }

    sink.error = -EIO;
    if (vart_console_queue_input(&console, "E", 1) != 1 ||
        vart_console_drain_input(&console) != -EIO ||
        vart_console_pending_input(&console) != 1 ||
        (uart.lsr & VART_UART16550_LSR_DR)) {
        return EXIT_FAILURE;
    }
    sink.error = 0;
    if (vart_console_drain_input(&console) != 1 ||
        consume(&address_space, &console, 'E') != 0) {
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&address_space);
    puts("ok - queue UART input with bounded backpressure");
    return EXIT_SUCCESS;
}
