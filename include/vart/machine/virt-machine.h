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
#include "vart/kvm-irq-routing.h"
#include "vart/memory.h"
#include "vart/pci.h"
#include "vart/pci-msix-irqfd.h"
#include "vart/riscv-aia.h"
#include "vart/riscv-iommu.h"
#include "vart/system.h"
#include "vart/vcpu.h"
#include "vart/vm.h"
#include "vart/virtio-blk.h"
#include "vart/virtio-net.h"

typedef struct VartVirtMachineConfig {
    size_t ram_size;
    size_t vcpu_count;
    bool enable_test_device;
    const char *virtio_blk_path;
    bool virtio_blk_readonly;
    bool enable_virtio_net;
    bool enable_pcie_switch;
    bool enable_riscv_iommu;
    const char *virtio_net_tap;
    bool virtio_net_backend_valid;
    int virtio_net_backend_fd;
    VartUartOutput uart_output;
    void *uart_output_opaque;
} VartVirtMachineConfig;

typedef struct VartVirtMachine {
    VartVm vm;
    VartMemoryRegion ram;
    VartVcpu *vcpus;
    size_t vcpu_count;
    size_t vcpus_created;
    VartExecution execution;
    VartRiscvAia aia;
    VartPciRootComplex pcie;
    VartRiscvIommu riscv_iommu;
    VartPciSwitch pcie_switch;
    VartIommuContext virtio_blk_iommu_context;
    VartIommuContext virtio_net_iommu_context;
    VartIrq uart_irq;
    VartUart16550 uart;
    VartTestDevice test_device;
    VartVirtioBlkDevice virtio_blk;
    VartVirtioNetDevice virtio_net;
    VartKvmIrqRouting irq_routing;
    VartPciMsixIrqfd virtio_blk_irqfd;
    VartPciMsixIrqfd virtio_net_irqfd;
    bool vm_created;
    bool ram_created;
    bool ram_registered;
    bool ram_mapped;
    bool address_space_initialized;
    bool aia_created;
    bool pcie_initialized;
    bool riscv_iommu_initialized;
    bool enable_riscv_iommu;
    bool virtio_blk_iommu_context_initialized;
    bool virtio_net_iommu_context_initialized;
    bool pcie_switch_initialized;
    bool uart_initialized;
    bool test_device_initialized;
    bool virtio_blk_initialized;
    bool irq_routing_initialized;
    bool virtio_blk_irqfd_initialized;
    bool virtio_net_initialized;
    bool virtio_net_irqfd_initialized;
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
