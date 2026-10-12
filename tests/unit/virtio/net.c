#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "vart/address-space.h"
#include "vart/bus.h"
#include "vart/machine/virt.h"
#include "vart/virtio-net.h"

#define VIRTIO_NET_HDR_SIZE 12

static int check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "not ok - %s\n", message);
        return -1;
    }
    return 0;
}

static void store_le16(uint8_t *data, uint16_t value)
{
    data[0] = value;
    data[1] = value >> 8;
}

static void store_le32(uint8_t *data, uint32_t value)
{
    data[0] = value;
    data[1] = value >> 8;
    data[2] = value >> 16;
    data[3] = value >> 24;
}

static void store_le64(uint8_t *data, uint64_t value)
{
    for (unsigned int i = 0; i < sizeof(value); i++) {
        data[i] = value >> (i * 8);
    }
}

static uint16_t load_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)data[1] << 8;
}

static uint32_t load_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

int main(void)
{
    static const uint8_t mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    uint8_t guest[0x4000] = { 0 };
    uint8_t frame[60];
    uint8_t received[sizeof(frame)];
    VartAddressSpace address_space;
    VartAddressRegion ram;
    VartEventLoop event_loop;
    VartBus parent_bus;
    VartPciRootComplex complex;
    VartVirtioNetDevice device;
    uint64_t bar_base;
    uint64_t value;
    int backend[2] = { -1, -1 };
    int ret;
    bool parent_initialized = false;
    bool ram_added = false;
    bool complex_initialized = false;
    bool device_initialized = false;
    bool device_attached = false;
    bool device_realized = false;
    bool event_loop_initialized = false;

    vart_address_space_init(&address_space);
    vart_bus_init(&parent_bus, "test-parent", NULL, &address_space);
    parent_initialized = true;
    ret = vart_address_region_init_ram(&ram, VART_VIRT_DRAM_BASE,
                                       sizeof(guest), -1, NULL, guest);
    if (ret < 0 || vart_address_space_add(&address_space, &ram) < 0) {
        goto fail;
    }
    ram_added = true;
    ret = vart_pci_root_complex_init(&complex, &parent_bus, &address_space);
    if (ret < 0) {
        goto fail;
    }
    complex_initialized = true;
    if (vart_bus_add_device(&parent_bus, &complex.parent) < 0 ||
        vart_device_realize(&complex.parent) < 0) {
        goto fail;
    }
    ret = vart_virtio_net_init(&device, mac, &address_space);
    if (ret == 0) {
        device_initialized = true;
    }
    if (ret == 0) {
        ret = vart_pci_bus_add_device(&complex.root_bus, &device.pci.pci, 0);
        if (ret == 0) {
            device_attached = true;
        }
    }
    if (ret == 0) {
        ret = vart_device_realize(&device.pci.pci.parent);
        if (ret == 0) {
            device_realized = true;
        }
    }
    if (ret < 0) {
        goto fail;
    }
    bar_base = device.pci.pci.bars[0].address;
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, backend) < 0 ||
        vart_virtio_net_attach_backend(&device, backend[0]) < 0) {
        goto fail;
    }
    backend[0] = -1;
    if (vart_event_loop_init(&event_loop) < 0) {
        goto fail;
    }
    event_loop_initialized = true;
    if (vart_virtio_net_attach_event_loop(&device, &event_loop) < 0) {
        goto fail;
    }
    if (check(vart_address_space_read(&address_space,
                                      VART_VIRT_PCIE_ECAM_BASE + 0x00, 4,
                                      &value) == 0 &&
              value == UINT64_C(0x10411af4), "virtio-net PCI identity") < 0 ||
        check(vart_address_space_read(&address_space,
                                      VART_VIRT_PCIE_ECAM_BASE + 0x0b, 1,
                                      &value) == 0 && value == 0x02,
              "virtio-net PCI class") < 0 ||
        check(vart_address_space_read(&address_space, bar_base + 0x300, 4,
                                      &value) == 0 && value == 0x12005452,
              "read virtio-net MAC low") < 0 ||
        check(vart_address_space_read(&address_space, bar_base + 0x304, 2,
                                      &value) == 0 && value == 0x5634,
              "read virtio-net MAC high") < 0 ||
        check(vart_address_space_read(&address_space, bar_base + 0x306, 2,
                                      &value) == 0 && value == 1,
              "read virtio-net link status") < 0 ||
        check(device.pci.pci.msix.configured &&
              device.pci.pci.msix.vector_count == 3,
              "configure virtio-net MSI-X") < 0 ||
        check(vart_address_space_write(&address_space,
                                       VART_VIRT_PCIE_ECAM_BASE + 4, 2,
                                       0x6) == 0,
              "enable virtio-net bus mastering") < 0) {
        goto fail;
    }

    for (unsigned int i = 0; i < sizeof(frame); i++) {
        frame[i] = (uint8_t)(i + 1);
    }
    memcpy(guest + 0x500, frame, sizeof(frame));
    store_le64(guest + 0x100, VART_VIRT_DRAM_BASE + 0x400);
    store_le32(guest + 0x108, 12);
    store_le16(guest + 0x10c, VART_VIRTIO_DESC_F_NEXT);
    store_le16(guest + 0x10e, 1);
    store_le64(guest + 0x110, VART_VIRT_DRAM_BASE + 0x500);
    store_le32(guest + 0x118, sizeof(frame));
    store_le16(guest + 0x11c, 0);
    store_le16(guest + 0x11e, 0);
    store_le16(guest + 0x202, 1);
    store_le16(guest + 0x204, 0);
    store_le16(guest + 0x302, 0);

    if (check(vart_address_space_write(&address_space, bar_base + 0x14, 1,
                                       VART_VIRTIO_STATUS_ACKNOWLEDGE |
                                       VART_VIRTIO_STATUS_DRIVER |
                                       VART_VIRTIO_STATUS_DRIVER_OK) == 0,
              "set virtio-net driver status") < 0 ||
        check(vart_address_space_write(&address_space, bar_base + 0x16, 2,
                                       1) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x18, 2,
                                       8) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x20, 4,
                                       VART_VIRT_DRAM_BASE + 0x100) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x28, 4,
                                       VART_VIRT_DRAM_BASE + 0x200) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x30, 4,
                                       VART_VIRT_DRAM_BASE + 0x300) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x1c, 2,
                                       1) == 0,
              "configure virtio-net TX queue") < 0 ||
        check(vart_address_space_write(&address_space, bar_base + 0x104, 2,
                                       1) == 0,
              "notify virtio-net TX queue") < 0 ||
        check(recv(backend[1], received, sizeof(received), MSG_DONTWAIT) ==
                  (ssize_t)sizeof(received),
              "receive transmitted Ethernet frame") < 0 ||
        check(memcmp(received, frame, sizeof(frame)) == 0,
              "preserve transmitted Ethernet frame") < 0 ||
        check(load_le16(guest + 0x302) == 1 &&
              load_le32(guest + 0x308) == sizeof(frame),
              "publish virtio-net TX used entry") < 0) {
        goto fail;
    }

    memset(guest + 0x600, 0, 64 * 16);
    store_le64(guest + 0x600, VART_VIRT_DRAM_BASE + 0x900);
    store_le32(guest + 0x608, VIRTIO_NET_HDR_SIZE);
    store_le16(guest + 0x60c, VART_VIRTIO_DESC_F_NEXT |
                              VART_VIRTIO_DESC_F_WRITE);
    store_le16(guest + 0x60e, 1);
    store_le64(guest + 0x610, VART_VIRT_DRAM_BASE + 0xa00);
    store_le32(guest + 0x618, sizeof(frame));
    store_le16(guest + 0x61c, VART_VIRTIO_DESC_F_WRITE);
    store_le16(guest + 0x61e, 0);
    store_le16(guest + 0x702, 1);
    store_le16(guest + 0x704, 0);
    store_le16(guest + 0x802, 0);
    if (check(write(backend[1], frame, sizeof(frame)) ==
                  (ssize_t)sizeof(frame),
              "inject RX frame before guest buffer") < 0 ||
        check(vart_event_loop_run_once(&event_loop, 0) == 1,
              "hold RX frame without guest buffer") < 0 ||
        check(vart_address_space_write(&address_space, bar_base + 0x16, 2,
                                       0) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x18, 2,
                                       8) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x20, 4,
                                       VART_VIRT_DRAM_BASE + 0x600) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x28, 4,
                                       VART_VIRT_DRAM_BASE + 0x700) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x30, 4,
                                       VART_VIRT_DRAM_BASE + 0x800) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x1c, 2,
                                       1) == 0,
              "configure virtio-net RX queue") < 0 ||
        check(vart_address_space_write(&address_space, bar_base + 0x100, 2,
                                       0) == 0,
              "notify virtio-net RX queue") < 0 ||
        check(memcmp(guest + 0xa00, frame, sizeof(frame)) == 0,
              "copy received Ethernet frame to guest") < 0 ||
        check(load_le16(guest + 0x802) == 1 &&
              load_le32(guest + 0x808) == VIRTIO_NET_HDR_SIZE +
                                      sizeof(frame),
              "publish virtio-net RX used entry") < 0) {
        goto fail;
    }

    vart_device_unrealize(&device.pci.pci.parent);
    device_realized = false;
    if (vart_pci_bus_remove_device(&complex.root_bus, &device.pci.pci) < 0 ||
        vart_virtio_net_destroy(&device) < 0 ||
        vart_pci_root_complex_destroy(&complex) < 0) {
        goto fail;
    }
    device_attached = false;
    device_initialized = false;
    complex_initialized = false;
    vart_event_loop_destroy(&event_loop);
    event_loop_initialized = false;
    close(backend[1]);
    backend[1] = -1;
    vart_address_space_remove(&address_space, &ram);
    ram_added = false;
    vart_bus_destroy(&parent_bus);
    parent_initialized = false;
    vart_address_space_destroy(&address_space);
    puts("ok - virtio-net PCI TX and RX backend");
    return EXIT_SUCCESS;

fail:
    if (backend[1] >= 0) {
        close(backend[1]);
    }
    if (backend[0] >= 0) {
        close(backend[0]);
    }
    if (device_realized) {
        vart_device_unrealize(&device.pci.pci.parent);
    }
    if (device_attached) {
        vart_pci_bus_remove_device(&complex.root_bus, &device.pci.pci);
    }
    if (device_initialized) {
        vart_virtio_net_destroy(&device);
    }
    if (event_loop_initialized) {
        vart_event_loop_destroy(&event_loop);
    }
    if (complex_initialized) {
        vart_pci_root_complex_destroy(&complex);
    }
    if (ram_added) {
        vart_address_space_remove(&address_space, &ram);
    }
    if (parent_initialized) {
        vart_bus_destroy(&parent_bus);
    }
    vart_address_space_destroy(&address_space);
    return EXIT_FAILURE;
}
