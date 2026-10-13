#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vart/address-space.h"
#include "vart/bus.h"
#include "vart/machine/virt.h"
#include "vart/virtio-blk.h"

#define DESC_BASE 0x100
#define AVAIL_BASE 0x200
#define USED_BASE 0x300
#define HEADER_BASE 0x400
#define DATA_BASE 0x500
#define STATUS_BASE 0x800

#define DESC_F_NEXT UINT16_C(1)
#define DESC_F_WRITE UINT16_C(2)

static void store16(uint8_t *data, uint16_t value)
{
    data[0] = value;
    data[1] = value >> 8;
}

static void store32(uint8_t *data, uint32_t value)
{
    data[0] = value;
    data[1] = value >> 8;
    data[2] = value >> 16;
    data[3] = value >> 24;
}

static uint16_t load16(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)data[1] << 8;
}

static uint32_t load32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static int check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "not ok - %s\n", message);
        return -1;
    }
    return 0;
}

int main(void)
{
    char path[] = "/tmp/vart-virtio-blk-XXXXXX";
    uint8_t guest[0x2000] = { 0 };
    uint8_t disk_data[512];
    uint8_t read_data[512];
    VartAddressSpace address_space;
    VartAddressRegion ram;
    VartBus parent_bus;
    VartPciRootComplex complex;
    VartVirtioBlkDevice device;
    uint64_t bar_base;
    uint64_t value;
    uint16_t avail_index;
    int fd = -1;
    int ret;
    bool parent_initialized = false;
    bool complex_initialized = false;
    bool device_initialized = false;
    bool device_attached = false;
    bool device_realized = false;
    bool ram_added = false;

    fd = mkstemp(path);
    if (fd < 0 || ftruncate(fd, 4096) < 0) {
        return 1;
    }
    close(fd);
    fd = -1;
    for (unsigned int i = 0; i < sizeof(disk_data); i++) {
        disk_data[i] = (uint8_t)i;
    }

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
    if (ret < 0 || vart_bus_add_device(&parent_bus, &complex.parent) < 0 ||
        vart_device_realize(&complex.parent) < 0) {
        goto fail;
    }
    complex_initialized = true;
    ret = vart_virtio_blk_init(&device, path, false, &address_space);
    if (ret < 0) {
        goto fail;
    }
    device_initialized = true;
    if (vart_pci_bus_add_device(&complex.root_bus, &device.pci.pci, 0) < 0 ||
        vart_device_realize(&device.pci.pci.parent) < 0) {
        goto fail;
    }
    device_attached = true;
    device_realized = true;
    bar_base = device.pci.pci.bars[0].address;

    if (check(vart_address_space_read(&address_space,
                                      VART_VIRT_PCIE_ECAM_BASE + 0x0b, 1,
                                      &value) == 0 && value == 1,
              "virtio-blk PCI class") < 0 ||
        check((device.pci.virtio.device_features &
               ((UINT64_C(1) << 1) | (UINT64_C(1) << 2))) ==
                  ((UINT64_C(1) << 1) | (UINT64_C(1) << 2)),
              "advertise virtio-blk segment limits") < 0 ||
        check(vart_address_space_write(&address_space,
                                       VART_VIRT_PCIE_ECAM_BASE + 4, 2,
                                       0x6) == 0,
              "enable virtio-blk bus mastering") < 0 ||
        check(vart_address_space_read(&address_space, bar_base + 0x300, 8,
                                      &value) == 0 && value == 8,
              "read virtio-blk capacity") < 0 ||
        check(vart_address_space_read(&address_space, bar_base + 0x314, 4,
                                      &value) == 0 && value == 512,
              "read virtio-blk block size") < 0 ||
        check(device.pci.pci.msix.configured &&
              device.pci.pci.msix.vector_count == 2,
              "configure virtio-blk MSI-X") < 0) {
        goto fail;
    }

    store32(guest + DESC_BASE + 0, VART_VIRT_DRAM_BASE + HEADER_BASE);
    store32(guest + DESC_BASE + 4, 0);
    store32(guest + DESC_BASE + 8, 16);
    store16(guest + DESC_BASE + 12, DESC_F_NEXT);
    store16(guest + DESC_BASE + 14, 1);
    store32(guest + DESC_BASE + 16, VART_VIRT_DRAM_BASE + DATA_BASE);
    store32(guest + DESC_BASE + 20, 0);
    store32(guest + DESC_BASE + 24, sizeof(disk_data));
    store16(guest + DESC_BASE + 28, DESC_F_NEXT);
    store16(guest + DESC_BASE + 30, 2);
    store32(guest + DESC_BASE + 32, VART_VIRT_DRAM_BASE + STATUS_BASE);
    store32(guest + DESC_BASE + 36, 0);
    store32(guest + DESC_BASE + 40, 1);
    store16(guest + DESC_BASE + 44, DESC_F_WRITE);
    store16(guest + DESC_BASE + 46, 0);
    store16(guest + AVAIL_BASE, 0);
    store16(guest + AVAIL_BASE + 2, 1);
    store16(guest + AVAIL_BASE + 4, 0);
    store16(guest + USED_BASE, 0);
    store16(guest + USED_BASE + 2, 0);
    store32(guest + HEADER_BASE, 1);
    memcpy(guest + DATA_BASE, disk_data, sizeof(disk_data));

    if (check(vart_address_space_write(&address_space, bar_base + 0x08, 4,
                                       0) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x0c, 4,
                                       (UINT32_C(1) << 6) |
                                       (UINT32_C(1) << 9)) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x08, 4,
                                       1) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x0c, 4,
                                       1) == 0,
              "negotiate virtio-blk features") < 0 ||
        check(vart_address_space_write(&address_space, bar_base + 0x16, 2,
                                       0) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x18, 2,
                                       8) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x1a, 2,
                                       0) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x20, 4,
                                       VART_VIRTIO_BLK_SECTOR_SIZE) == 0,
              "configure virtio-blk queue") < 0) {
        goto fail;
    }
    store32(guest + DESC_BASE + 0, VART_VIRT_DRAM_BASE + HEADER_BASE);
    if (vart_address_space_write(&address_space, bar_base + 0x20, 4,
                                 VART_VIRT_DRAM_BASE + DESC_BASE) < 0 ||
        vart_address_space_write(&address_space, bar_base + 0x28, 4,
                                 VART_VIRT_DRAM_BASE + AVAIL_BASE) < 0 ||
        vart_address_space_write(&address_space, bar_base + 0x30, 4,
                                 VART_VIRT_DRAM_BASE + USED_BASE) < 0 ||
        vart_address_space_write(&address_space, bar_base + 0x1c, 2, 1) < 0 ||
        vart_address_space_write(&address_space, bar_base + 0x14, 1, 7) < 0 ||
        vart_address_space_write(&address_space, bar_base + 0x100, 2, 0) < 0) {
        goto fail;
    }
    if (check(guest[STATUS_BASE] == 0, "virtio-blk write completion") < 0 ||
        check((device.pci.pci.msix.pba[0] & 1) != 0,
              "virtio-blk queue interrupt pending") < 0 ||
        check(load16(guest + USED_BASE + 2) == 1 &&
              load16(guest + USED_BASE + 4) == 0 &&
              load32(guest + USED_BASE + 8) == sizeof(disk_data),
              "virtio-blk used ring completion") < 0) {
        goto fail;
    }
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0 || pread(fd, read_data, sizeof(read_data), 0) !=
                     (ssize_t)sizeof(read_data) ||
        memcmp(read_data, disk_data, sizeof(read_data)) != 0) {
        goto fail;
    }
    close(fd);
    fd = -1;
    avail_index = 2;
    memset(guest + DATA_BASE, 0, sizeof(read_data));
    store32(guest + HEADER_BASE, 0);
    store16(guest + AVAIL_BASE + 2, avail_index);
    guest[STATUS_BASE] = 0xff;
    store16(guest + DESC_BASE + 28, DESC_F_NEXT | DESC_F_WRITE);
    store16(guest + DESC_BASE + 44, DESC_F_WRITE);
    if (vart_address_space_write(&address_space, bar_base + 0x100, 2, 0) < 0 ||
        check(guest[STATUS_BASE] == 0 &&
              memcmp(guest + DATA_BASE, disk_data, sizeof(read_data)) == 0,
              "virtio-blk read completion") < 0) {
        goto fail;
    }

    store32(guest + HEADER_BASE, 4);
    store32(guest + DESC_BASE + 16, VART_VIRT_DRAM_BASE + STATUS_BASE);
    store32(guest + DESC_BASE + 20, 0);
    store32(guest + DESC_BASE + 24, 1);
    store16(guest + DESC_BASE + 28, DESC_F_WRITE);
    store16(guest + DESC_BASE + 30, 0);
    store16(guest + AVAIL_BASE + 2, 3);
    guest[STATUS_BASE] = 0xff;
    if (vart_address_space_write(&address_space, bar_base + 0x100, 2, 0) < 0 ||
        check(guest[STATUS_BASE] == 0,
              "virtio-blk flush completion") < 0) {
        goto fail;
    }

    vart_device_unrealize(&device.pci.pci.parent);
    device_realized = false;
    if (vart_pci_bus_remove_device(&complex.root_bus, &device.pci.pci) < 0 ||
        vart_virtio_blk_destroy(&device) < 0) {
        goto fail;
    }
    device_attached = false;
    device_initialized = false;
    if (vart_pci_root_complex_destroy(&complex) < 0) {
        goto fail;
    }
    complex_initialized = false;
    vart_address_space_remove(&address_space, &ram);
    ram_added = false;
    vart_bus_destroy(&parent_bus);
    parent_initialized = false;
    vart_address_space_destroy(&address_space);
    unlink(path);
    puts("ok - virtio-blk PCI read and write");
    return 0;

fail:
    if (fd >= 0) {
        close(fd);
    }
    if (device_realized) {
        vart_device_unrealize(&device.pci.pci.parent);
    }
    if (device_attached) {
        vart_pci_bus_remove_device(&complex.root_bus, &device.pci.pci);
    }
    if (device_initialized) {
        vart_virtio_blk_destroy(&device);
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
    unlink(path);
    return 1;
}
