#include <errno.h>
#include <stdint.h>

#include "vart/exec.h"

static uint64_t mmio_decode_le(const uint8_t *data, unsigned int size)
{
    uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < size; i++) {
        value |= (uint64_t)data[i] << (i * 8);
    }
    return value;
}

void vart_execution_init(VartExecution *execution,
                         VartAddressSpace *system_address_space)
{
    execution->system_address_space = system_address_space;
}

int vart_execution_handle_exit(VartExecution *execution, VartVcpu *vcpu,
                               const VartVcpuExit *exit)
{
    uint64_t value;
    int ret;

    if (execution == NULL || execution->system_address_space == NULL ||
        exit == NULL) {
        return -EINVAL;
    }
    if (exit->type != VART_VCPU_EXIT_MMIO) {
        return -ENOTSUP;
    }
    if (exit->mmio.size != 1 && exit->mmio.size != 2 &&
        exit->mmio.size != 4 && exit->mmio.size != 8) {
        return -EINVAL;
    }

    if (exit->mmio.is_write) {
        value = mmio_decode_le(exit->mmio.data, exit->mmio.size);
        return vart_address_space_write(execution->system_address_space,
                                        exit->mmio.address,
                                        exit->mmio.size, value);
    }

    if (vcpu == NULL) {
        return -EINVAL;
    }
    ret = vart_address_space_read(execution->system_address_space,
                                  exit->mmio.address,
                                  exit->mmio.size, &value);
    if (ret < 0) {
        return ret;
    }
    return vart_vcpu_complete_mmio_read(vcpu, value);
}
