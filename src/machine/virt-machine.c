#include <asm/kvm.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "vart/machine/virt-machine.h"
#include "vart/machine/virt.h"

static int add_devices(VartVirtMachine *machine,
                       const VartVirtMachineConfig *config)
{
    uint8_t virtio_net_devfn = config->virtio_blk_path != NULL ? 1 : 0;
    int ret;

    ret = vart_pci_root_complex_init(&machine->pcie, &vart_system.system_bus,
                                     &vart_system.system_address_space);
    if (ret < 0) {
        return ret;
    }
    machine->pcie_initialized = true;
    ret = vart_bus_add_device(&vart_system.system_bus,
                              &machine->pcie.parent);
    if (ret < 0) {
        return ret;
    }
    ret = vart_device_realize(&machine->pcie.parent);
    if (ret < 0) {
        return ret;
    }

    if (config->virtio_blk_path != NULL) {
        ret = vart_virtio_blk_init(&machine->virtio_blk,
                                   config->virtio_blk_path,
                                   config->virtio_blk_readonly,
                                   &vart_system.system_address_space);
        if (ret < 0) {
            return ret;
        }
        machine->virtio_blk_initialized = true;
        if (machine->irq_routing_initialized && machine->vm.kvm->irqfd) {
            ret = vart_pci_msix_irqfd_init(&machine->virtio_blk_irqfd,
                                           &machine->virtio_blk.pci.pci,
                                           &machine->irq_routing,
                                           VART_VIRT_PCIE_IRQ_BASE);
            if (ret < 0) {
                return ret;
            }
            machine->virtio_blk_irqfd_initialized = true;
        } else {
            ret = vart_pci_device_connect_msix_aia(
                &machine->virtio_blk.pci.pci, &machine->aia);
            if (ret < 0) {
                return ret;
            }
        }
        ret = vart_pci_bus_add_device(&machine->pcie.root_bus,
                                      &machine->virtio_blk.pci.pci, 0);
        if (ret < 0) {
            return ret;
        }
        ret = vart_device_realize(&machine->virtio_blk.pci.pci.parent);
        if (ret < 0) {
            return ret;
        }
    }

    if (config->enable_virtio_net || config->virtio_net_tap != NULL) {
        if (config->virtio_net_tap != NULL &&
            config->virtio_net_backend_valid) {
            return -EINVAL;
        }
        ret = vart_virtio_net_init(&machine->virtio_net, (const uint8_t[]){
                                       0x52, 0x54, 0x00, 0x12, 0x34, 0x57,
                                   }, &vart_system.system_address_space);
        if (ret < 0) {
            return ret;
        }
        machine->virtio_net_initialized = true;
        if (config->virtio_net_tap != NULL) {
            ret = vart_virtio_net_open_tap(&machine->virtio_net,
                                           config->virtio_net_tap);
            if (ret < 0) {
                return ret;
            }
        } else if (config->virtio_net_backend_valid) {
            ret = vart_virtio_net_attach_backend(&machine->virtio_net,
                                                 config->virtio_net_backend_fd);
            if (ret < 0) {
                return ret;
            }
        }
        if (machine->irq_routing_initialized && machine->vm.kvm->irqfd) {
            ret = vart_pci_msix_irqfd_init(&machine->virtio_net_irqfd,
                                           &machine->virtio_net.pci.pci,
                                           &machine->irq_routing,
                                           VART_VIRT_PCIE_IRQ_BASE + 4);
            if (ret < 0) {
                return ret;
            }
            machine->virtio_net_irqfd_initialized = true;
        } else {
            ret = vart_pci_device_connect_msix_aia(
                &machine->virtio_net.pci.pci, &machine->aia);
            if (ret < 0) {
                return ret;
            }
        }
        ret = vart_pci_bus_add_device(&machine->pcie.root_bus,
                                      &machine->virtio_net.pci.pci,
                                      virtio_net_devfn);
        if (ret < 0) {
            return ret;
        }
        ret = vart_device_realize(&machine->virtio_net.pci.pci.parent);
        if (ret < 0) {
            return ret;
        }
    }

    ret = vart_uart16550_init(&machine->uart, VART_VIRT_UART_BASE,
                              config->uart_output,
                              config->uart_output_opaque);
    if (ret < 0) {
        return ret;
    }
    machine->uart_initialized = true;
    ret = vart_uart16550_connect_irq(&machine->uart, &machine->uart_irq);
    if (ret < 0) {
        return ret;
    }
    ret = vart_bus_add_device(&vart_system.system_bus,
                              &machine->uart.parent);
    if (ret < 0) {
        return ret;
    }
    ret = vart_device_realize(&machine->uart.parent);
    if (ret < 0) {
        return ret;
    }
    if (!config->enable_test_device) {
        return 0;
    }
    ret = vart_test_device_init(&machine->test_device,
                                VART_VIRT_TEST_BASE, NULL, NULL);
    if (ret < 0) {
        return ret;
    }
    machine->test_device_initialized = true;
    ret = vart_bus_add_device(&vart_system.system_bus,
                              &machine->test_device.parent);
    if (ret < 0) {
        return ret;
    }
    return vart_device_realize(&machine->test_device.parent);
}

int vart_virt_machine_create(VartVirtMachine *machine, const VartKvm *kvm,
                             const VartVirtMachineConfig *config)
{
    size_t i;
    int ret;

    if (machine == NULL || kvm == NULL || config == NULL || kvm->fd < 0 ||
        config->ram_size == 0 || config->vcpu_count == 0 ||
        config->vcpu_count > VART_VIRT_MAX_CPUS) {
        return -EINVAL;
    }
    memset(machine, 0, sizeof(*machine));
    machine->vm.fd = -1;
    machine->aia.device.fd = -1;
    vart_address_space_init(&vart_system.system_address_space);
    vart_bus_init(&vart_system.system_bus, "system-bus", NULL,
                  &vart_system.system_address_space);
    machine->address_space_initialized = true;

    ret = vart_vm_create(&machine->vm, kvm);
    if (ret < 0) {
        goto fail;
    }
    machine->vm_created = true;
    ret = vart_memory_region_create(&machine->ram, VART_VIRT_DRAM_BASE,
                                    config->ram_size, 0);
    if (ret < 0) {
        goto fail;
    }
    machine->ram_created = true;
    ret = vart_memory_region_register(&machine->ram, &machine->vm);
    if (ret < 0) {
        goto fail;
    }
    machine->ram_registered = true;
    ret = vart_memory_region_map(&machine->ram,
                                 &vart_system.system_address_space);
    if (ret < 0) {
        goto fail;
    }
    machine->ram_mapped = true;

    machine->vcpus = calloc(config->vcpu_count, sizeof(*machine->vcpus));
    if (machine->vcpus == NULL) {
        ret = -ENOMEM;
        goto fail;
    }
    machine->vcpu_count = config->vcpu_count;
    for (i = 0; i < machine->vcpu_count; i++) {
        ret = vart_vcpu_create(&machine->vcpus[i], &machine->vm, i);
        if (ret < 0) {
            goto fail;
        }
        machine->vcpus_created++;
    }
    ret = vart_riscv_aia_create(&machine->aia, &machine->vm,
                                KVM_DEV_RISCV_AIA_MODE_AUTO);
    if (ret < 0) {
        goto fail;
    }
    machine->aia_created = true;
    ret = vart_riscv_aia_init_aplic(&machine->aia, machine->vcpu_count,
                                    VART_VIRT_IMSIC_S_BASE,
                                    VART_VIRT_IMSIC_NUM_IDS,
                                    VART_VIRT_APLIC_S_BASE,
                                    VART_VIRT_APLIC_NUM_SOURCES);
    if (ret < 0) {
        goto fail;
    }
    ret = vart_kvm_irq_routing_init(&machine->irq_routing, &machine->vm);
    if (ret == 0) {
        machine->irq_routing_initialized = true;
    } else if (ret != -ENOTSUP) {
        goto fail;
    }
    ret = vart_riscv_aia_connect_irq(&machine->aia, &machine->uart_irq,
                                     VART_VIRT_UART_IRQ);
    if (ret < 0) {
        goto fail;
    }
    ret = add_devices(machine, config);
    if (ret < 0) {
        goto fail;
    }
    vart_execution_init(&machine->execution,
                        &vart_system.system_address_space);
    machine->initialized = true;
    return 0;

fail:
    vart_virt_machine_destroy(machine);
    return ret;
}

int vart_virt_machine_init_boot(VartVirtMachine *machine,
                                const VartRiscvBootInfo *boot)
{
    size_t i;
    int ret;

    if (machine == NULL || !machine->initialized || boot == NULL) {
        return -EINVAL;
    }
    for (i = 0; i < machine->vcpu_count; i++) {
        ret = vart_riscv_vcpu_init_boot(&machine->vcpus[i], boot);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

int vart_virt_machine_start(VartVirtMachine *machine,
                            VartVcpuExitHandler handler, void *opaque)
{
    size_t i;
    int ret;

    if (machine == NULL || !machine->initialized || handler == NULL) {
        return -EINVAL;
    }
    for (i = 0; i < machine->vcpu_count; i++) {
        ret = vart_vcpu_start(&machine->vcpus[i], handler, opaque);
        if (ret < 0) {
            while (i > 0) {
                i--;
                vart_vcpu_request_stop(&machine->vcpus[i]);
            }
            return ret;
        }
    }
    return 0;
}

int vart_virt_machine_join(VartVirtMachine *machine)
{
    size_t i;
    int result = 0;

    if (machine == NULL || !machine->initialized) {
        return -EINVAL;
    }
    for (i = 0; i < machine->vcpu_count; i++) {
        int ret = vart_vcpu_join(&machine->vcpus[i]);

        if (result == 0 && ret != 0) {
            result = ret;
        }
    }
    return result;
}

void vart_virt_machine_destroy(VartVirtMachine *machine)
{
    size_t i;

    if (machine == NULL) {
        return;
    }
    for (i = 0; i < machine->vcpus_created; i++) {
        if (machine->vcpus[i].thread_created &&
            !machine->vcpus[i].thread_joined) {
            vart_vcpu_request_stop(&machine->vcpus[i]);
        }
    }
    for (i = 0; i < machine->vcpus_created; i++) {
        if (machine->vcpus[i].thread_created &&
            !machine->vcpus[i].thread_joined) {
            vart_vcpu_join(&machine->vcpus[i]);
        }
    }
    if (machine->test_device_initialized) {
        vart_device_unrealize(&machine->test_device.parent);
        if (machine->test_device.parent.bus != NULL) {
            vart_bus_remove_device(&vart_system.system_bus,
                                   &machine->test_device.parent);
        }
    }
    if (machine->uart_initialized) {
        vart_device_unrealize(&machine->uart.parent);
        if (machine->uart.parent.bus != NULL) {
            vart_bus_remove_device(&vart_system.system_bus,
                                   &machine->uart.parent);
        }
    }
    if (machine->virtio_blk_initialized) {
        if (machine->virtio_blk_irqfd_initialized) {
            (void)vart_pci_msix_irqfd_destroy(&machine->virtio_blk_irqfd);
            machine->virtio_blk_irqfd_initialized = false;
        }
        vart_device_unrealize(&machine->virtio_blk.pci.pci.parent);
        if (machine->virtio_blk.pci.pci.parent.bus != NULL) {
            vart_pci_bus_remove_device(&machine->pcie.root_bus,
                                       &machine->virtio_blk.pci.pci);
        }
        vart_virtio_blk_destroy(&machine->virtio_blk);
    }
    if (machine->virtio_net_initialized) {
        if (machine->virtio_net_irqfd_initialized) {
            (void)vart_pci_msix_irqfd_destroy(&machine->virtio_net_irqfd);
            machine->virtio_net_irqfd_initialized = false;
        }
        vart_device_unrealize(&machine->virtio_net.pci.pci.parent);
        if (machine->virtio_net.pci.pci.parent.bus != NULL) {
            vart_pci_bus_remove_device(&machine->pcie.root_bus,
                                       &machine->virtio_net.pci.pci);
        }
        vart_virtio_net_destroy(&machine->virtio_net);
    }
    if (machine->irq_routing_initialized) {
        vart_kvm_irq_routing_destroy(&machine->irq_routing);
        machine->irq_routing_initialized = false;
    }
    if (machine->pcie_initialized) {
        vart_pci_root_complex_destroy(&machine->pcie);
    }
    if (machine->ram_mapped) {
        vart_memory_region_unmap(&machine->ram);
        machine->ram_mapped = false;
    }
    if (machine->address_space_initialized) {
        vart_bus_destroy(&vart_system.system_bus);
        vart_address_space_destroy(&vart_system.system_address_space);
    }
    if (machine->aia_created) {
        vart_riscv_aia_destroy(&machine->aia);
    }
    for (i = 0; i < machine->vcpus_created; i++) {
        vart_vcpu_destroy(&machine->vcpus[i]);
    }
    free(machine->vcpus);
    if (machine->ram_registered) {
        vart_memory_region_unregister(&machine->ram, &machine->vm);
    }
    if (machine->ram_created) {
        vart_memory_region_destroy(&machine->ram);
    }
    if (machine->vm_created) {
        vart_vm_destroy(&machine->vm);
    }
    memset(machine, 0, sizeof(*machine));
    machine->vm.fd = -1;
    machine->aia.device.fd = -1;
}
