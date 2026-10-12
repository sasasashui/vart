#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "vart/event-loop.h"
#include "vart/machine/virt-machine.h"

#define GUEST_RAM_SIZE (64 * 1024 * 1024)

int main(void)
{
    VartVirtMachineConfig config = {
        .ram_size = GUEST_RAM_SIZE,
        .vcpu_count = 1,
        .enable_virtio_net = true,
    };
    VartVirtMachine machine;
    VartKvm kvm;
    VartEventLoop loop;
    int backend[2] = { -1, -1 };
    bool machine_initialized = false;
    bool loop_initialized = false;
    bool event_source_attached = false;
    int ret;

    alarm(10);
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, backend) < 0) {
        fprintf(stderr, "not ok - virtio-net machine integration: %d\n",
                -errno);
        return EXIT_FAILURE;
    }
    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        close(backend[0]);
        close(backend[1]);
        fprintf(stderr, "not ok - virtio-net machine integration: %d\n",
                ret);
        return EXIT_FAILURE;
    }
    config.virtio_net_backend_valid = true;
    config.virtio_net_backend_fd = backend[0];
    backend[0] = -1;
    ret = vart_virt_machine_create(&machine, &kvm, &config);
    if (ret < 0) {
        goto fail;
    }
    machine_initialized = true;
    if (!machine.virtio_net_initialized ||
        machine.pcie.root_bus.functions[0] != &machine.virtio_net.pci.pci ||
        machine.virtio_net.pci.pci.msix.inject == NULL ||
        machine.virtio_net.pci.pci.parent.dma_as !=
            &vart_system.system_address_space) {
        ret = -EIO;
        goto fail;
    }
    if (machine.virtio_net.backend_fd < 0) {
        ret = -EIO;
        goto fail;
    }
    ret = vart_event_loop_init(&loop);
    if (ret < 0) {
        goto fail;
    }
    loop_initialized = true;
    ret = vart_virtio_net_attach_event_loop(&machine.virtio_net, &loop);
    if (ret < 0) {
        goto fail;
    }
    event_source_attached = true;
    ret = vart_virtio_net_detach_event_loop(&machine.virtio_net);
    if (ret < 0) {
        goto fail;
    }
    event_source_attached = false;
    vart_event_loop_destroy(&loop);
    loop_initialized = false;
    vart_virt_machine_destroy(&machine);
    machine_initialized = false;
    close(backend[1]);
    backend[1] = -1;
    vart_kvm_close(&kvm);
    alarm(0);
    puts("ok - attach virtio-net PCI device to virt machine");
    return EXIT_SUCCESS;

fail:
    if (event_source_attached) {
        (void)vart_virtio_net_detach_event_loop(&machine.virtio_net);
    }
    if (loop_initialized) {
        vart_event_loop_destroy(&loop);
    }
    if (machine_initialized) {
        vart_virt_machine_destroy(&machine);
    }
    if (backend[0] >= 0) {
        close(backend[0]);
    }
    if (backend[1] >= 0) {
        close(backend[1]);
    }
    vart_kvm_close(&kvm);
    alarm(0);
    fprintf(stderr, "not ok - virtio-net machine integration: %d\n", ret);
    return EXIT_FAILURE;
}
