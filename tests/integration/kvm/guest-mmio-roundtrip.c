#include <asm/kvm.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/exec.h"
#include "vart/kvm.h"
#include "vart/memory.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

#define GUEST_BASE UINT64_C(0x80000000)
#define GUEST_RAM_SIZE (16 * 1024 * 1024)
#define DEVICE_BASE UINT64_C(0x10000000)
#define EXPECTED_VALUE UINT64_C(0x12345678)

typedef struct TestDevice {
    uint64_t written;
} TestDevice;

static int device_read(void *opaque, uint64_t offset, unsigned int size,
                       uint64_t *value)
{
    (void)opaque;
    if (offset != 0 || size != 4) {
        return -EINVAL;
    }
    *value = EXPECTED_VALUE;
    return 0;
}

static int device_write(void *opaque, uint64_t offset, unsigned int size,
                        uint64_t value)
{
    TestDevice *device = opaque;

    if (offset != 8 || size != 4) {
        return -EINVAL;
    }
    device->written = value;
    return 0;
}

static int load_guest(VartMemoryRegion *memory, const char *path)
{
    unsigned char *image = NULL;
    long file_size;
    FILE *file;
    int ret = 0;

    file = fopen(path, "rb");
    if (file == NULL) {
        return -errno;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        ret = -EIO;
        goto out;
    }
    if ((unsigned long)file_size > memory->size) {
        ret = -EFBIG;
        goto out;
    }
    image = malloc((size_t)file_size);
    if (image == NULL) {
        ret = -ENOMEM;
        goto out;
    }
    if (fread(image, (size_t)file_size, 1, file) != 1) {
        ret = -EIO;
        goto out;
    }
    ret = vart_memory_region_write(memory, GUEST_BASE, image,
                                   (size_t)file_size);
out:
    free(image);
    fclose(file);
    return ret;
}

int main(int argc, char **argv)
{
    const VartMmioOps ops = { .read = device_read, .write = device_write };
    VartAddressSpace address_space;
    VartAddressRegion device_region;
    VartMemoryRegion memory;
    VartExecution execution;
    TestDevice device = { 0 };
    VartVcpuExit exit;
    VartVcpu vcpu;
    VartKvm kvm;
    VartVm vm;
    int ret;

    if (argc != 2) {
        return EXIT_FAILURE;
    }
    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        return EXIT_FAILURE;
    }
    ret = vart_vm_create(&vm, &kvm);
    if (ret < 0) {
        goto fail_vm;
    }
    ret = vart_memory_region_create(&memory, GUEST_BASE, GUEST_RAM_SIZE, 0);
    if (ret < 0) {
        goto fail_memory_create;
    }
    if ((ret = load_guest(&memory, argv[1])) < 0 ||
        (ret = vart_memory_region_register(&memory, &vm)) < 0) {
        goto fail_memory;
    }
    ret = vart_vcpu_create(&vcpu, &vm, 0);
    if (ret < 0) {
        goto fail_registered;
    }
    if ((ret = vart_vcpu_set_pc(&vcpu, GUEST_BASE)) < 0 ||
        (ret = vart_vcpu_set_mode(&vcpu, KVM_RISCV_MODE_S)) < 0) {
        goto fail_vcpu;
    }

    vart_address_space_init(&address_space);
    vart_address_region_init_mmio(&device_region, DEVICE_BASE, 0x100, 0,
                                  &device, &ops, &device);
    vart_address_space_add(&address_space, &device_region);
    vart_execution_init(&execution, &address_space);

    ret = vart_vcpu_run(&vcpu, &exit);
    if (ret < 0 || exit.type != VART_VCPU_EXIT_MMIO || exit.mmio.is_write ||
        (ret = vart_execution_handle_exit(&execution, &vcpu, &exit)) < 0) {
        goto fail_address_space;
    }
    ret = vart_vcpu_run(&vcpu, &exit);
    if (ret < 0 || exit.type != VART_VCPU_EXIT_MMIO || !exit.mmio.is_write ||
        (ret = vart_execution_handle_exit(&execution, &vcpu, &exit)) < 0 ||
        device.written != EXPECTED_VALUE) {
        ret = ret < 0 ? ret : -EIO;
        goto fail_address_space;
    }

    vart_address_space_destroy(&address_space);
    vart_vcpu_destroy(&vcpu);
    vart_memory_region_unregister(&memory, &vm);
    vart_memory_region_destroy(&memory);
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    printf("ok - complete guest MMIO read and write roundtrip\n");
    return EXIT_SUCCESS;

fail_address_space:
    vart_address_space_destroy(&address_space);
fail_vcpu:
    vart_vcpu_destroy(&vcpu);
fail_registered:
    vart_memory_region_unregister(&memory, &vm);
fail_memory:
    vart_memory_region_destroy(&memory);
fail_memory_create:
    vart_vm_destroy(&vm);
fail_vm:
    vart_kvm_close(&kvm);
    fprintf(stderr, "not ok - guest MMIO roundtrip: %d\n", ret);
    return EXIT_FAILURE;
}
