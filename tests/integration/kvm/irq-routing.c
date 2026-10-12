#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/kvm-irq-routing.h"
#include "vart/machine/virt-machine.h"

int main(void)
{
    VartKvmIrqRouting routing = { 0 };
    VartVirtMachine machine;
    VartVirtMachineConfig config = {
        .ram_size = 64 * 1024 * 1024,
        .vcpu_count = 1,
    };
    VartKvm kvm;
    int ret;

    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        fprintf(stderr, "not ok - open KVM for irq routing: %d\n", ret);
        return EXIT_FAILURE;
    }
    ret = vart_virt_machine_create(&machine, &kvm, &config);
    if (ret < 0) {
        fprintf(stderr, "not ok - create VM for irq routing: %d\n", ret);
        vart_kvm_close(&kvm);
        return EXIT_FAILURE;
    }
    ret = vart_kvm_irq_routing_init(&routing, &machine.vm);
    if (ret == -ENOTSUP) {
        vart_virt_machine_destroy(&machine);
        vart_kvm_close(&kvm);
        puts("ok - KVM irq routing unavailable");
        return EXIT_SUCCESS;
    }
    if (ret == 0) {
        ret = vart_kvm_irq_routing_add_msi(&routing, 32,
                                           UINT64_C(0x28000000), 9, 0);
    }
    if (ret == 0) {
        ret = vart_kvm_irq_routing_commit(&routing);
    }
    if (ret == 0) {
        ret = vart_kvm_irq_routing_clear(&routing);
    }
    if (ret == 0) {
        ret = vart_kvm_irq_routing_commit(&routing);
    }
    vart_kvm_irq_routing_destroy(&routing);
    vart_virt_machine_destroy(&machine);
    vart_kvm_close(&kvm);
    if (ret < 0) {
        fprintf(stderr, "not ok - configure KVM MSI routing: %d\n", ret);
        return EXIT_FAILURE;
    }
    puts("ok - configure KVM MSI routing");
    return EXIT_SUCCESS;
}
