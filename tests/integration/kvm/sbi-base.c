#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../fixtures/sbi-base.h"
#include "vart/address-space.h"
#include "vart/devices/test-device.h"
#include "vart/exec.h"
#include "vart/kvm.h"
#include "vart/machine/virt.h"
#include "vart/memory.h"
#include "vart/riscv-cpu.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

#define GUEST_RAM_SIZE (16 * 1024 * 1024)
#define FDT_ADDR (VART_VIRT_DRAM_BASE + GUEST_RAM_SIZE - 0x1000)

typedef struct SbiContext {
    VartExecution execution;
    VartTestDevice device;
    unsigned int exits;
} SbiContext;

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
    ret = vart_memory_region_write(memory, VART_VIRT_DRAM_BASE, image,
                                   (size_t)file_size);
out:
    free(image);
    fclose(file);
    return ret;
}

static int handle_exit(VartVcpu *vcpu, const VartVcpuExit *exit,
                       void *opaque)
{
    SbiContext *context = opaque;
    int ret;

    vart_mutex_assert_held(&vcpu->vm->big_lock);
    if (context->exits++ == 8 || exit->type != VART_VCPU_EXIT_MMIO) {
        /* A BASE ecall reaching userspace is a contract failure. */
        return -EIO;
    }
    ret = vart_execution_handle_exit(&context->execution, vcpu, exit);
    if (ret < 0) {
        return ret;
    }
    return context->device.status == VART_TEST_STATUS_NONE ? 0 : 1;
}

static uint64_t result_at(const VartMemoryRegion *memory, size_t offset)
{
    const size_t base = VART_SBI_BASE_RESULTS_GPA - memory->guest_addr;
    uint64_t value;

    memcpy(&value, (const char *)memory->host_addr + base + offset,
           sizeof(value));
    return value;
}

int main(int argc, char **argv)
{
    const VartRiscvBootInfo boot = {
        .entry = VART_VIRT_DRAM_BASE,
        .fdt_addr = FDT_ADDR,
    };
    VartAddressSpace address_space;
    VartMemoryRegion memory;
    SbiContext context;
    VartVcpu vcpu;
    VartKvm kvm;
    VartVm vm;
    uint64_t spec;
    int ret;

    if (argc != 2) {
        return EXIT_FAILURE;
    }
    alarm(10);
    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        return EXIT_FAILURE;
    }
    ret = vart_vm_create(&vm, &kvm);
    if (ret < 0) {
        goto fail_vm;
    }
    ret = vart_memory_region_create(&memory, VART_VIRT_DRAM_BASE,
                                    GUEST_RAM_SIZE, 0);
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
    ret = vart_riscv_vcpu_init_boot(&vcpu, &boot);
    if (ret < 0) {
        goto fail_vcpu;
    }

    vart_address_space_init(&address_space);
    ret = vart_test_device_init(&context.device, VART_VIRT_TEST_BASE,
                                NULL, NULL);
    if (ret < 0 ||
        (ret = vart_address_space_add(&address_space,
                                      &context.device.region)) < 0) {
        goto fail_address_space;
    }
    vart_execution_init(&context.execution, &address_space);
    context.exits = 0;

    ret = vart_vcpu_start(&vcpu, handle_exit, &context);
    if (ret < 0 || (ret = vart_vcpu_join(&vcpu)) < 0 ||
        context.device.status != VART_TEST_STATUS_PASS) {
        ret = ret < 0 ? ret : -EIO;
        goto fail_address_space;
    }

    spec = result_at(&memory, VART_SBI_BASE_SPEC_OFFSET);
    if (result_at(&memory, VART_SBI_BASE_IMPL_ID_OFFSET) !=
            VART_SBI_IMPL_ID_KVM ||
        result_at(&memory, VART_SBI_BASE_TIME_PROBE_OFFSET) != 1 ||
        result_at(&memory, VART_SBI_BASE_UNKNOWN_PROBE_OFFSET) != 0) {
        ret = -EIO;
        goto fail_address_space;
    }

    printf("ok - KVM SBI BASE spec %lu.%lu implementation %lu version %lu\n",
           spec >> 24, spec & 0xffffff,
           result_at(&memory, VART_SBI_BASE_IMPL_ID_OFFSET),
           result_at(&memory, VART_SBI_BASE_IMPL_VERSION_OFFSET));
    vart_address_space_destroy(&address_space);
    vart_vcpu_destroy(&vcpu);
    vart_memory_region_unregister(&memory, &vm);
    vart_memory_region_destroy(&memory);
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    alarm(0);
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
    fprintf(stderr, "not ok - KVM SBI BASE: %d\n", ret);
    return EXIT_FAILURE;
}
