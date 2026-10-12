#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/eventfd.h"
#include "vart/kvm-ioeventfd.h"
#include "vart/machine/virt-machine.h"

int main(void)
{
    VartEventNotifier notifier = { .fd = -1 };
    VartVirtMachine machine;
    VartVirtMachineConfig config = {
        .ram_size = 64 * 1024 * 1024,
        .vcpu_count = 1,
    };
    VartKvm kvm;
    int ret;

    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        fprintf(stderr, "not ok - open KVM for ioeventfd: %d\n", ret);
        return EXIT_FAILURE;
    }
    ret = vart_virt_machine_create(&machine, &kvm, &config);
    if (ret < 0) {
        fprintf(stderr, "not ok - create VM for ioeventfd: %d\n", ret);
        vart_kvm_close(&kvm);
        return EXIT_FAILURE;
    }
    if (!kvm.ioeventfd) {
        vart_virt_machine_destroy(&machine);
        vart_kvm_close(&kvm);
        puts("ok - KVM ioeventfd unavailable");
        return EXIT_SUCCESS;
    }
    ret = vart_event_notifier_init(&notifier);
    if (ret == 0) {
        ret = vart_kvm_ioeventfd_assign(&machine.vm, &notifier,
                                        UINT64_C(0x10000000), 1);
    }
    if (ret == 0) {
        ret = vart_kvm_ioeventfd_deassign(&machine.vm, &notifier,
                                          UINT64_C(0x10000000), 1);
    }
    vart_event_notifier_destroy(&notifier);
    vart_virt_machine_destroy(&machine);
    vart_kvm_close(&kvm);
    if (ret == -EINVAL || ret == -ENOTSUP) {
        puts("ok - KVM ioeventfd rejected by host");
        return EXIT_SUCCESS;
    }
    if (ret < 0) {
        fprintf(stderr, "not ok - assign and deassign KVM ioeventfd: %d\n",
                ret);
        return EXIT_FAILURE;
    }
    puts("ok - assign and deassign KVM ioeventfd");
    return EXIT_SUCCESS;
}
