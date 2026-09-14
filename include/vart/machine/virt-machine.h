#ifndef VART_MACHINE_VIRT_MACHINE_H
#define VART_MACHINE_VIRT_MACHINE_H

#include <stdbool.h>
#include <stddef.h>

#include "vart/address-space.h"
#include "vart/devices/test-device.h"
#include "vart/devices/uart16550.h"
#include "vart/exec.h"
#include "vart/irq.h"
#include "vart/kvm.h"
#include "vart/memory.h"
#include "vart/riscv-aia.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

typedef struct VartVirtMachineConfig {
    size_t ram_size;
    size_t vcpu_count;
    bool enable_test_device;
    VartUartOutput uart_output;
    void *uart_output_opaque;
} VartVirtMachineConfig;

typedef struct VartVirtMachine {
    VartVm vm;
    VartMemoryRegion ram;
    VartVcpu *vcpus;
    size_t vcpu_count;
    size_t vcpus_created;
    VartAddressSpace system_address_space;
    VartExecution execution;
    VartRiscvAia aia;
    VartIrq uart_irq;
    VartUart16550 uart;
    VartTestDevice test_device;
    bool vm_created;
    bool ram_created;
    bool ram_registered;
    bool address_space_initialized;
    bool aia_created;
    bool uart_initialized;
    bool test_device_initialized;
    bool initialized;
} VartVirtMachine;

int vart_virt_machine_create(VartVirtMachine *machine, const VartKvm *kvm,
                             const VartVirtMachineConfig *config);
int vart_virt_machine_init_boot(VartVirtMachine *machine,
                                const VartRiscvBootInfo *boot);
int vart_virt_machine_start(VartVirtMachine *machine,
                            VartVcpuExitHandler handler, void *opaque);
int vart_virt_machine_join(VartVirtMachine *machine);
void vart_virt_machine_destroy(VartVirtMachine *machine);

#endif
