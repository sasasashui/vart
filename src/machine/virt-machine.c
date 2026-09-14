#include <asm/kvm.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "vart/machine/virt-machine.h"
#include "vart/machine/virt.h"

static int add_devices(VartVirtMachine *machine,
                       const VartVirtMachineConfig *config)
{
    int ret;

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
    ret = vart_address_space_add(&machine->system_address_space,
                                 &machine->uart.region);
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
    return vart_address_space_add(&machine->system_address_space,
                                  &machine->test_device.region);
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
    vart_address_space_init(&machine->system_address_space);
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
                        &machine->system_address_space);
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
    if (machine->address_space_initialized) {
        vart_address_space_destroy(&machine->system_address_space);
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
