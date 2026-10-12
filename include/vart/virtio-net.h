#ifndef VART_VIRTIO_NET_H
#define VART_VIRTIO_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vart/address-space.h"
#include "vart/event-loop.h"
#include "vart/sync.h"
#include "vart/virtio-pci.h"

#define VART_VIRTIO_NET_CONFIG_SIZE 16

typedef struct VartVirtioNetDevice {
    VartVirtioPciDevice pci;
    uint8_t config[VART_VIRTIO_NET_CONFIG_SIZE];
    VartEventSource backend_source;
    VartEventLoop *event_loop;
    VartMutex lock;
    uint8_t *rx_packet;
    size_t rx_packet_length;
    int backend_fd;
    bool backend_owned;
    bool lock_initialized;
    bool initialized;
} VartVirtioNetDevice;

int vart_virtio_net_init(VartVirtioNetDevice *device, const uint8_t mac[6],
                         VartAddressSpace *dma_as);
int vart_virtio_net_attach_backend(VartVirtioNetDevice *device, int fd);
int vart_virtio_net_open_tap(VartVirtioNetDevice *device,
                             const char *interface_name);
int vart_virtio_net_attach_event_loop(VartVirtioNetDevice *device,
                                      VartEventLoop *loop);
int vart_virtio_net_detach_event_loop(VartVirtioNetDevice *device);
int vart_virtio_net_detach_backend(VartVirtioNetDevice *device);
int vart_virtio_net_destroy(VartVirtioNetDevice *device);

#endif
