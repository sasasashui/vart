#include <errno.h>
#include <string.h>

#include "vart/console.h"

int vart_console_init(VartConsole *console, VartUart16550 *uart)
{
    if (console == NULL || uart == NULL) {
        return -EINVAL;
    }
    memset(console, 0, sizeof(*console));
    console->uart = uart;
    return 0;
}

ssize_t vart_console_queue_input(VartConsole *console, const void *data,
                                 size_t size)
{
    const unsigned char *bytes = data;
    size_t space;
    size_t tail;
    size_t first;

    if (console == NULL || console->uart == NULL ||
        (data == NULL && size != 0)) {
        return -EINVAL;
    }
    if (size == 0) {
        return 0;
    }
    space = VART_CONSOLE_INPUT_CAPACITY - console->input_count;
    if (space == 0) {
        return -EAGAIN;
    }
    if (size > space) {
        size = space;
    }
    tail = (console->input_head + console->input_count) %
           VART_CONSOLE_INPUT_CAPACITY;
    first = VART_CONSOLE_INPUT_CAPACITY - tail;
    if (first > size) {
        first = size;
    }
    memcpy(&console->input[tail], bytes, first);
    memcpy(console->input, bytes + first, size - first);
    console->input_count += size;
    return (ssize_t)size;
}

int vart_console_drain_input(VartConsole *console)
{
    int delivered = 0;

    if (console == NULL || console->uart == NULL) {
        return -EINVAL;
    }
    while (console->input_count != 0) {
        int ret = vart_uart16550_receive(
            console->uart, console->input[console->input_head]);

        if (ret == -EAGAIN) {
            break;
        }
        if (ret < 0) {
            return ret;
        }
        console->input_head = (console->input_head + 1) %
                              VART_CONSOLE_INPUT_CAPACITY;
        console->input_count--;
        delivered++;
    }
    return delivered;
}

size_t vart_console_pending_input(const VartConsole *console)
{
    return console == NULL ? 0 : console->input_count;
}

size_t vart_console_input_space(const VartConsole *console)
{
    return console == NULL ? 0 :
           VART_CONSOLE_INPUT_CAPACITY - console->input_count;
}
