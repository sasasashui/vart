#include <errno.h>
#include <stddef.h>

#include "vart/riscv-sbi.h"

void vart_riscv_sbi_dispatcher_init(VartRiscvSbiDispatcher *dispatcher)
{
    dispatcher->handlers = NULL;
}

void vart_riscv_sbi_handler_init(VartRiscvSbiHandler *handler,
                                 uint64_t extension_start,
                                 uint64_t extension_end,
                                 VartRiscvSbiCallback callback,
                                 void *opaque)
{
    handler->extension_start = extension_start;
    handler->extension_end = extension_end;
    handler->callback = callback;
    handler->opaque = opaque;
    handler->next = NULL;
}

int vart_riscv_sbi_add_handler(VartRiscvSbiDispatcher *dispatcher,
                               VartRiscvSbiHandler *handler)
{
    VartRiscvSbiHandler **link;

    if (dispatcher == NULL || handler == NULL || handler->callback == NULL ||
        handler->extension_start > handler->extension_end ||
        handler->next != NULL) {
        return -EINVAL;
    }

    for (link = &dispatcher->handlers; *link != NULL;
         link = &(*link)->next) {
        if (handler->extension_end < (*link)->extension_start) {
            break;
        }
        if (handler->extension_start <= (*link)->extension_end) {
            return -EEXIST;
        }
    }
    handler->next = *link;
    *link = handler;
    return 0;
}

int vart_riscv_sbi_dispatch(VartRiscvSbiDispatcher *dispatcher,
                            VartVcpu *vcpu, const VartVcpuExit *exit)
{
    VartRiscvSbiResponse response = {
        .error = VART_SBI_ERR_NOT_SUPPORTED,
    };
    VartRiscvSbiHandler *handler;
    int ret;

    if (dispatcher == NULL || vcpu == NULL || exit == NULL ||
        exit->type != VART_VCPU_EXIT_RISCV_SBI) {
        return -EINVAL;
    }

    for (handler = dispatcher->handlers; handler != NULL;
         handler = handler->next) {
        if (exit->sbi.extension_id < handler->extension_start) {
            break;
        }
        if (exit->sbi.extension_id <= handler->extension_end) {
            ret = handler->callback(exit, &response, handler->opaque);
            if (ret < 0) {
                return ret;
            }
            break;
        }
    }
    return vart_vcpu_complete_sbi(vcpu, response.error, response.value);
}
