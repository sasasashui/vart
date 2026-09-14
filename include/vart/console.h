#ifndef VART_CONSOLE_H
#define VART_CONSOLE_H

#include <stddef.h>
#include <sys/types.h>

#include "vart/devices/uart16550.h"

#define VART_CONSOLE_INPUT_CAPACITY 4096

typedef struct VartConsole {
    VartUart16550 *uart;
    unsigned char input[VART_CONSOLE_INPUT_CAPACITY];
    size_t input_head;
    size_t input_count;
} VartConsole;

int vart_console_init(VartConsole *console, VartUart16550 *uart);
ssize_t vart_console_queue_input(VartConsole *console, const void *data,
                                 size_t size);
int vart_console_drain_input(VartConsole *console);
size_t vart_console_pending_input(const VartConsole *console);
size_t vart_console_input_space(const VartConsole *console);

#endif
