#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "vart/machine/virt-machine.h"

#define GUEST_RAM_SIZE (64 * 1024 * 1024)

int main(void)
{
    char disk_path[] = "/tmp/vart-machine-blk-XXXXXX";
    VartVirtMachineConfig config = {
        .ram_size = GUEST_RAM_SIZE,
        .vcpu_count = 1,
        .virtio_blk_path = disk_path,
    };
    VartVirtMachine machine;
    VartKvm kvm;
    int fd;
    int ret;

    alarm(10);
    fd = mkstemp(disk_path);
    if (fd < 0 || ftruncate(fd, 4096) < 0) {
        return EXIT_FAILURE;
    }
    close(fd);
    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        unlink(disk_path);
        return EXIT_FAILURE;
    }
    ret = vart_virt_machine_create(&machine, &kvm, &config);
    if (ret < 0 || !machine.virtio_blk_initialized ||
        !machine.virtio_blk_irqfd_initialized ||
        machine.pcie.root_bus.functions[0] != &machine.virtio_blk.pci.pci ||
        machine.virtio_blk.pci.pci.msix.inject == NULL ||
        machine.virtio_blk.pci.pci.parent.dma_as !=
            &vart_system.system_address_space ||
        vart_system.system_address_space.count != 6) {
        if (ret == 0) {
            vart_virt_machine_destroy(&machine);
        }
        vart_kvm_close(&kvm);
        unlink(disk_path);
        fprintf(stderr, "not ok - virtio-blk machine integration: %d\n",
                ret < 0 ? ret : -EIO);
        return EXIT_FAILURE;
    }
    vart_virt_machine_destroy(&machine);
    vart_kvm_close(&kvm);
    unlink(disk_path);
    alarm(0);
    puts("ok - attach virtio-blk PCI device to virt machine");
    return EXIT_SUCCESS;
}
