#include <asm/kvm.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/kvm.h"
#include "vart/memory.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

#define TEST_GUEST_ADDR UINT64_C(0x80000000)
#define TEST_MEMORY_SIZE (16 * 1024 * 1024)
#define TEST_MMIO_ADDR UINT64_C(0x10000000)

static int fail(const char *operation, int error)
{
    fprintf(stderr, "not ok - %s: %s\n", operation, strerror(-error));
    return EXIT_FAILURE;
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
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        ret = errno != 0 ? -errno : -EIO;
        goto out;
    }
    if (file_size == 0 || (unsigned long)file_size > memory->size) {
        ret = -EFBIG;
        goto out;
    }

    image = malloc((size_t)file_size);
    if (image == NULL) {
        ret = -ENOMEM;
        goto out;
    }
    if (fread(image, (size_t)file_size, 1, file) != 1) {
        ret = ferror(file) ? -EIO : -ENODATA;
        goto out;
    }
    ret = vart_memory_region_write(memory, TEST_GUEST_ADDR,
                                   image, (size_t)file_size);

out:
    free(image);
    fclose(file);
    return ret;
}

int main(int argc, char **argv)
{
    const unsigned char expected[] = { 0x78, 0x56, 0x34, 0x12 };
    VartMemoryRegion memory;
    VartVcpuExit exit;
    VartVcpu vcpu;
    VartKvm kvm;
    VartVm vm;
    int ret;

    if (argc != 2) {
        fprintf(stderr, "usage: %s GUEST-BINARY\n", argv[0]);
        return EXIT_FAILURE;
    }

    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        return fail("open KVM", ret);
    }
    ret = vart_vm_create(&vm, &kvm);
    if (ret < 0) {
        vart_kvm_close(&kvm);
        return fail("create VM", ret);
    }
    ret = vart_memory_region_create(&memory, TEST_GUEST_ADDR,
                                    TEST_MEMORY_SIZE, 0);
    if (ret < 0) {
        goto fail_vm;
    }
    ret = load_guest(&memory, argv[1]);
    if (ret < 0) {
        goto fail_memory;
    }
    ret = vart_memory_region_register(&memory, &vm);
    if (ret < 0) {
        goto fail_memory;
    }
    ret = vart_vcpu_create(&vcpu, &vm, 0);
    if (ret < 0) {
        goto fail_registered;
    }
    ret = vart_vcpu_set_pc(&vcpu, TEST_GUEST_ADDR);
    if (ret < 0) {
        goto fail_vcpu;
    }
    ret = vart_vcpu_set_mode(&vcpu, KVM_RISCV_MODE_S);
    if (ret < 0) {
        goto fail_vcpu;
    }

    ret = vart_vcpu_run(&vcpu, &exit);
    if (ret < 0) {
        goto fail_vcpu;
    }
    if (exit.type != VART_VCPU_EXIT_MMIO ||
        exit.kvm_reason != KVM_EXIT_MMIO ||
        exit.mmio.address != TEST_MMIO_ADDR || exit.mmio.size != 4 ||
        !exit.mmio.is_write ||
        memcmp(exit.mmio.data, expected, sizeof(expected)) != 0) {
        ret = -EIO;
        goto fail_vcpu;
    }

    vart_vcpu_destroy(&vcpu);
    vart_memory_region_unregister(&memory, &vm);
    vart_memory_region_destroy(&memory);
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    printf("ok - execute RISC-V guest to MMIO exit\n");
    return EXIT_SUCCESS;

fail_vcpu:
    vart_vcpu_destroy(&vcpu);
fail_registered:
    vart_memory_region_unregister(&memory, &vm);
fail_memory:
    vart_memory_region_destroy(&memory);
fail_vm:
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    return fail("execute MMIO guest", ret);
}
