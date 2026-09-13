#ifndef VART_EXEC_H
#define VART_EXEC_H

#include "vart/address-space.h"
#include "vart/vcpu.h"

typedef struct VartExecution {
    VartAddressSpace *system_address_space;
} VartExecution;

void vart_execution_init(VartExecution *execution,
                         VartAddressSpace *system_address_space);
int vart_execution_handle_exit(VartExecution *execution, VartVcpu *vcpu,
                               const VartVcpuExit *exit);

#endif
