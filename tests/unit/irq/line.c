#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/irq.h"

typedef struct Sink {
    uint32_t line;
    bool levels[4];
    unsigned int count;
    int error;
} Sink;

static int set_irq(void *opaque, uint32_t line, bool level)
{
    Sink *sink = opaque;

    if (sink->error != 0) {
        return sink->error;
    }
    sink->line = line;
    sink->levels[sink->count++] = level;
    return 0;
}

int main(void)
{
    Sink sink = { 0 };
    VartIrq irq;

    if (vart_irq_init(NULL, set_irq, &sink, 7) != -EINVAL ||
        vart_irq_init(&irq, NULL, &sink, 7) != -EINVAL ||
        vart_irq_init(&irq, set_irq, &sink, 7) < 0 ||
        vart_irq_lower(&irq) < 0 || sink.count != 0 ||
        vart_irq_raise(&irq) < 0 || vart_irq_raise(&irq) < 0 ||
        sink.count != 1 || sink.line != 7 || !sink.levels[0] ||
        vart_irq_lower(&irq) < 0 || sink.count != 2 || sink.levels[1] ||
        vart_irq_pulse(&irq) < 0 || sink.count != 4 ||
        !sink.levels[2] || sink.levels[3]) {
        return EXIT_FAILURE;
    }
    sink.error = -EIO;
    if (vart_irq_raise(&irq) != -EIO || irq.level) {
        return EXIT_FAILURE;
    }
    printf("ok - drive a generic interrupt line\n");
    return EXIT_SUCCESS;
}
