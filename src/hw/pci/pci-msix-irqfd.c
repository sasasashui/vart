#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vart/kvm-irqfd.h"
#include "vart/pci-msix-irqfd.h"

static uint64_t pci_msix_irqfd_load_le(const uint8_t *data,
                                       unsigned int size)
{
    uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < size; i++) {
        value |= (uint64_t)data[i] << (i * 8);
    }
    return value;
}

static int pci_msix_irqfd_inject(VartPciDevice *device, unsigned int vector,
                                 void *opaque)
{
    VartPciMsixIrqfd *backend = opaque;
    const uint8_t *entry;
    uint64_t address;
    uint32_t data;
    uint32_t gsi;
    int ret;

    if (backend == NULL || !backend->connected ||
        backend->device != device || vector >= backend->vector_count) {
        return -EINVAL;
    }
    entry = &device->msix.table[vector * 16];
    address = pci_msix_irqfd_load_le(entry, 8);
    data = (uint32_t)pci_msix_irqfd_load_le(entry + 8, 4);
    ret = vart_pci_device_remap_msix_message(device, vector, &address, &data);
    if (ret < 0) {
        return ret;
    }
    if (address == 0) {
        return -EINVAL;
    }
    gsi = backend->first_gsi + vector;
    if (!backend->route_added[vector]) {
        ret = vart_kvm_irq_routing_add_msi(backend->routing, gsi, address,
                                           data, 0);
        if (ret < 0) {
            return ret;
        }
        ret = vart_kvm_irq_routing_commit(backend->routing);
        if (ret < 0) {
            (void)vart_kvm_irq_routing_remove_msi(backend->routing, gsi);
            return ret;
        }
        backend->route_added[vector] = true;
    }
    if (!backend->assigned[vector]) {
        ret = vart_kvm_irqfd_assign(backend->routing->vm,
                                    &backend->notifiers[vector], gsi);
        if (ret < 0) {
            return ret;
        }
        backend->assigned[vector] = true;
    }
    return vart_event_notifier_notify(&backend->notifiers[vector], 1);
}

int vart_pci_msix_irqfd_init(VartPciMsixIrqfd *backend,
                             VartPciDevice *device,
                             VartKvmIrqRouting *routing,
                             uint32_t first_gsi)
{
    unsigned int i;
    int ret;

    if (backend == NULL || device == NULL || routing == NULL ||
        !routing->initialized || !device->msix.configured ||
        device->msix.vector_count == 0 ||
        first_gsi > UINT32_MAX - device->msix.vector_count + 1) {
        return -EINVAL;
    }
    memset(backend, 0, sizeof(*backend));
    backend->notifiers = calloc(device->msix.vector_count,
                                sizeof(*backend->notifiers));
    backend->route_added = calloc(device->msix.vector_count,
                                  sizeof(*backend->route_added));
    backend->assigned = calloc(device->msix.vector_count,
                               sizeof(*backend->assigned));
    if (backend->notifiers == NULL || backend->route_added == NULL ||
        backend->assigned == NULL) {
        free(backend->notifiers);
        free(backend->route_added);
        free(backend->assigned);
        memset(backend, 0, sizeof(*backend));
        return -ENOMEM;
    }
    for (i = 0; i < device->msix.vector_count; i++) {
        ret = vart_event_notifier_init(&backend->notifiers[i]);
        if (ret < 0) {
            while (i != 0) {
                i--;
                vart_event_notifier_destroy(&backend->notifiers[i]);
            }
            free(backend->notifiers);
            free(backend->route_added);
            free(backend->assigned);
            memset(backend, 0, sizeof(*backend));
            return ret;
        }
    }
    backend->device = device;
    backend->routing = routing;
    backend->first_gsi = first_gsi;
    backend->vector_count = device->msix.vector_count;
    ret = vart_pci_device_set_msix_injector(device, pci_msix_irqfd_inject,
                                            backend);
    if (ret < 0) {
        for (i = 0; i < backend->vector_count; i++) {
            vart_event_notifier_destroy(&backend->notifiers[i]);
        }
        free(backend->notifiers);
        free(backend->route_added);
        free(backend->assigned);
        memset(backend, 0, sizeof(*backend));
        return ret;
    }
    backend->connected = true;
    return 0;
}

int vart_pci_msix_irqfd_destroy(VartPciMsixIrqfd *backend)
{
    int first_error = 0;
    bool routes_changed = false;
    unsigned int i;

    if (backend == NULL || !backend->connected) {
        return 0;
    }
    (void)vart_pci_device_set_msix_injector(backend->device, NULL, NULL);
    for (i = 0; i < backend->vector_count; i++) {
        int ret;

        if (backend->assigned[i]) {
            ret = vart_kvm_irqfd_deassign(backend->routing->vm,
                                           &backend->notifiers[i],
                                           backend->first_gsi + i);
            if (ret < 0 && first_error == 0) {
                first_error = ret;
            }
        }
        if (backend->route_added[i]) {
            ret = vart_kvm_irq_routing_remove_msi(
                backend->routing, backend->first_gsi + i);
            if (ret == 0) {
                routes_changed = true;
            } else if (ret != -ENOENT && first_error == 0) {
                first_error = ret;
            }
        }
    }
    if (routes_changed) {
        int ret = vart_kvm_irq_routing_commit(backend->routing);

        if (ret < 0 && first_error == 0) {
            first_error = ret;
        }
    }
    for (i = 0; i < backend->vector_count; i++) {
        vart_event_notifier_destroy(&backend->notifiers[i]);
    }
    free(backend->notifiers);
    free(backend->route_added);
    free(backend->assigned);
    memset(backend, 0, sizeof(*backend));
    return first_error;
}
