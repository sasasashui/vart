#ifndef VART_IRQ_H
#define VART_IRQ_H

#include <stdbool.h>
#include <stdint.h>

typedef int (*VartIrqSet)(void *opaque, uint32_t line, bool level);

typedef struct VartIrq {
    VartIrqSet set;
    void *opaque;
    uint32_t line;
    bool level;
} VartIrq;

/* Callers serialize line state, normally with the VM big lock. */
int vart_irq_init(VartIrq *irq, VartIrqSet set, void *opaque,
                  uint32_t line);
int vart_irq_set(VartIrq *irq, bool level);
int vart_irq_raise(VartIrq *irq);
int vart_irq_lower(VartIrq *irq);
int vart_irq_pulse(VartIrq *irq);

#endif
