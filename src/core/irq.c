#include <errno.h>
#include <string.h>

#include "vart/irq.h"

int vart_irq_init(VartIrq *irq, VartIrqSet set, void *opaque,
                  uint32_t line)
{
    if (irq == NULL || set == NULL) {
        return -EINVAL;
    }
    memset(irq, 0, sizeof(*irq));
    irq->set = set;
    irq->opaque = opaque;
    irq->line = line;
    return 0;
}

int vart_irq_set(VartIrq *irq, bool level)
{
    int ret;

    if (irq == NULL || irq->set == NULL) {
        return -EINVAL;
    }
    if (irq->level == level) {
        return 0;
    }
    ret = irq->set(irq->opaque, irq->line, level);
    if (ret < 0) {
        return ret;
    }
    irq->level = level;
    return 0;
}

int vart_irq_raise(VartIrq *irq)
{
    return vart_irq_set(irq, true);
}

int vart_irq_lower(VartIrq *irq)
{
    return vart_irq_set(irq, false);
}

int vart_irq_pulse(VartIrq *irq)
{
    int ret;

    if (irq == NULL || irq->set == NULL || irq->level) {
        return -EINVAL;
    }
    ret = vart_irq_raise(irq);
    if (ret < 0) {
        return ret;
    }
    return vart_irq_lower(irq);
}
