#include <errno.h>
#include <stdlib.h>

#include "vart/kvm-ioeventfd.h"
#include "vart/virtio-pci.h"
#include "vart/vm.h"

/* KVM consumes the notify write; the event loop serializes queue handling. */
static int virtio_pci_ioeventfd_notify(VartEventNotifier *notifier,
                                       uint64_t value, void *opaque)
{
    VartVirtioPciDevice *device = opaque;
    unsigned int queue_index;
    int ret;

    (void)value;
    if (device == NULL || !device->ioeventfd_enabled ||
        device->ioeventfd_vm == NULL || device->notify == NULL) {
        return -EINVAL;
    }
    for (queue_index = 0; queue_index < device->virtio.queue_count;
         queue_index++) {
        if (&device->ioeventfd_notifiers[queue_index] == notifier) {
            break;
        }
    }
    if (queue_index == device->virtio.queue_count) {
        return -EINVAL;
    }
    vart_mutex_lock(&device->ioeventfd_vm->big_lock);
    ret = device->notify(device, queue_index, device->notify_opaque);
    vart_mutex_unlock(&device->ioeventfd_vm->big_lock);
    return ret;
}

int vart_virtio_pci_enable_ioeventfd(VartVirtioPciDevice *device,
                                     struct VartVm *vm,
                                     VartEventLoop *loop)
{
    unsigned int i;
    uint64_t address;
    int ret;

    if (device == NULL || !device->initialized || vm == NULL ||
        vm->fd < 0 || vm->kvm == NULL || loop == NULL ||
        !loop->initialized) {
        return -EINVAL;
    }
    if (!vm->kvm->ioeventfd) {
        return -ENOTSUP;
    }
    if (device->ioeventfd_enabled) {
        return -EBUSY;
    }
    if (!device->pci.bars[0].implemented ||
        device->pci.bars[0].address > UINT64_MAX -
        VART_VIRTIO_PCI_NOTIFY_OFFSET -
        (uint64_t)(device->virtio.queue_count - 1) *
        VART_VIRTIO_PCI_NOTIFY_MULTIPLIER - 2) {
        return -EINVAL;
    }
    device->ioeventfd_notifiers = calloc(device->virtio.queue_count,
                                         sizeof(*device->ioeventfd_notifiers));
    device->ioeventfd_sources = calloc(device->virtio.queue_count,
                                       sizeof(*device->ioeventfd_sources));
    device->ioeventfd_assigned = calloc(device->virtio.queue_count,
                                        sizeof(*device->ioeventfd_assigned));
    device->ioeventfd_addresses = calloc(device->virtio.queue_count,
                                         sizeof(*device->ioeventfd_addresses));
    if (device->ioeventfd_notifiers == NULL ||
        device->ioeventfd_sources == NULL ||
        device->ioeventfd_assigned == NULL ||
        device->ioeventfd_addresses == NULL) {
        free(device->ioeventfd_notifiers);
        free(device->ioeventfd_sources);
        free(device->ioeventfd_assigned);
        free(device->ioeventfd_addresses);
        device->ioeventfd_notifiers = NULL;
        device->ioeventfd_sources = NULL;
        device->ioeventfd_assigned = NULL;
        device->ioeventfd_addresses = NULL;
        return -ENOMEM;
    }
    device->ioeventfd_vm = vm;
    for (i = 0; i < device->virtio.queue_count; i++) {
        ret = vart_event_notifier_init(&device->ioeventfd_notifiers[i]);
        if (ret < 0) {
            goto fail;
        }
        address = device->pci.bars[0].address +
                  VART_VIRTIO_PCI_NOTIFY_OFFSET +
                  (uint64_t)i * VART_VIRTIO_PCI_NOTIFY_MULTIPLIER;
        device->ioeventfd_addresses[i] = address;
        ret = vart_kvm_ioeventfd_assign(vm,
                                        &device->ioeventfd_notifiers[i],
                                        address, 2);
        if (ret < 0) {
            goto fail;
        }
        device->ioeventfd_assigned[i] = true;
        ret = vart_event_notifier_source_add(
            loop, &device->ioeventfd_sources[i],
            &device->ioeventfd_notifiers[i], virtio_pci_ioeventfd_notify,
            device);
        if (ret < 0) {
            goto fail;
        }
    }
    device->ioeventfd_enabled = true;
    return 0;

fail:
    device->ioeventfd_enabled = true;
    (void)vart_virtio_pci_disable_ioeventfd(device);
    return ret;
}

int vart_virtio_pci_disable_ioeventfd(VartVirtioPciDevice *device)
{
    unsigned int i;
    int first_error = 0;

    if (device == NULL) {
        return -EINVAL;
    }
    if (device->ioeventfd_notifiers == NULL ||
        device->ioeventfd_sources == NULL ||
        device->ioeventfd_assigned == NULL ||
        device->ioeventfd_addresses == NULL) {
        device->ioeventfd_enabled = false;
        device->ioeventfd_vm = NULL;
        return 0;
    }
    for (i = 0; i < device->virtio.queue_count; i++) {
        int ret;

        if (device->ioeventfd_sources[i].event_source.registered) {
            ret = vart_event_notifier_source_remove(
                &device->ioeventfd_sources[i]);
            if (ret < 0 && first_error == 0) {
                first_error = ret;
            }
        }
        if (device->ioeventfd_assigned[i] && device->ioeventfd_vm != NULL) {
            ret = vart_kvm_ioeventfd_deassign(
                device->ioeventfd_vm, &device->ioeventfd_notifiers[i],
                device->ioeventfd_addresses[i], 2);
            if (ret < 0 && first_error == 0) {
                first_error = ret;
            }
        }
        vart_event_notifier_destroy(&device->ioeventfd_notifiers[i]);
    }
    free(device->ioeventfd_notifiers);
    free(device->ioeventfd_sources);
    free(device->ioeventfd_assigned);
    free(device->ioeventfd_addresses);
    device->ioeventfd_notifiers = NULL;
    device->ioeventfd_sources = NULL;
    device->ioeventfd_assigned = NULL;
    device->ioeventfd_addresses = NULL;
    device->ioeventfd_vm = NULL;
    device->ioeventfd_enabled = false;
    return first_error;
}
