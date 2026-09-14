#include <asm/kvm.h>
#include <errno.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "vart/address-space.h"
#include "vart/devices/test-device.h"
#include "vart/exec.h"
#include "vart/kvm.h"
#include "vart/machine/virt.h"
#include "vart/memory.h"
#include "vart/riscv-aia.h"
#include "vart/riscv-cpu.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

#define GUEST_RAM_SIZE (16 * 1024 * 1024)
#define INTERRUPT_ID 1

typedef struct TestContext {
    VartExecution execution;
    VartTestDevice device;
    unsigned int exits;
} TestContext;

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
    TestContext *context = opaque;
    int ret;

    vart_mutex_assert_held(&vcpu->vm->big_lock);
    if (context->exits++ == 8 || exit->type != VART_VCPU_EXIT_MMIO) {
        return -EIO;
    }
    ret = vart_execution_handle_exit(&context->execution, vcpu, exit);
    if (ret < 0) {
        return ret;
    }
    return context->device.status == VART_TEST_STATUS_NONE ? 0 : 1;
}

int main(int argc, char **argv)
{
    const VartRiscvBootInfo boot = {
        .entry = VART_VIRT_DRAM_BASE,
        .fdt_addr = VART_VIRT_DRAM_BASE + UINT64_C(0x1000),
    };
    VartAddressSpace address_space;
    VartMemoryRegion memory;
    TestContext context;
    VartRiscvAia aia;
    VartVcpu vcpu;
    VartKvm kvm;
    VartVm vm;
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
    ret = vart_riscv_aia_create(&aia, &vm,
                                KVM_DEV_RISCV_AIA_MODE_AUTO);
    if (ret < 0) {
        goto fail_aia_create;
    }
    if (vart_riscv_aia_signal_msi(&aia, 0, INTERRUPT_ID) != -EINVAL ||
        vart_riscv_aia_init_imsic(&aia, 0, VART_VIRT_IMSIC_S_BASE,
                                  VART_VIRT_IMSIC_NUM_IDS) != -EINVAL ||
        vart_riscv_aia_init_imsic(&aia, 1,
                                  VART_VIRT_IMSIC_S_BASE + 1,
                                  VART_VIRT_IMSIC_NUM_IDS) != -EINVAL) {
        ret = -EINVAL;
        goto fail_aia;
    }
    ret = vart_riscv_aia_init_imsic(&aia, 1, VART_VIRT_IMSIC_S_BASE,
                                    VART_VIRT_IMSIC_NUM_IDS);
    if (ret < 0) {
        goto fail_aia;
    }
    if (vart_riscv_aia_init_imsic(&aia, 1, VART_VIRT_IMSIC_S_BASE,
                                  VART_VIRT_IMSIC_NUM_IDS) != -EINVAL ||
        vart_riscv_aia_signal_msi(&aia, 1, INTERRUPT_ID) != -EINVAL ||
        vart_riscv_aia_signal_msi(&aia, 0, 0) != -EINVAL ||
        vart_riscv_aia_signal_msi(&aia, 0,
                                  VART_VIRT_IMSIC_NUM_IDS + 1) != -EINVAL) {
        ret = -EINVAL;
        goto fail_aia;
    }
    ret = vart_riscv_vcpu_init_boot(&vcpu, &boot);
    if (ret < 0 ||
        (ret = vart_riscv_aia_signal_msi(&aia, 0, INTERRUPT_ID)) < 0) {
        goto fail_aia;
    }

    vart_address_space_init(&address_space);
    ret = vart_test_device_init(&context.device, VART_VIRT_TEST_BASE,
                                NULL, NULL);
    if (ret < 0 || (ret = vart_address_space_add(
                        &address_space, &context.device.region)) < 0) {
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

    vart_address_space_destroy(&address_space);
    vart_riscv_aia_destroy(&aia);
    vart_vcpu_destroy(&vcpu);
    vart_memory_region_unregister(&memory, &vm);
    vart_memory_region_destroy(&memory);
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    alarm(0);
    printf("ok - inject one KVM IMSIC interrupt\n");
    return EXIT_SUCCESS;

fail_address_space:
    vart_address_space_destroy(&address_space);
fail_aia:
    vart_riscv_aia_destroy(&aia);
fail_aia_create:
    vart_vcpu_destroy(&vcpu);
fail_registered:
    vart_memory_region_unregister(&memory, &vm);
fail_memory:
    vart_memory_region_destroy(&memory);
fail_memory_create:
    vart_vm_destroy(&vm);
fail_vm:
    vart_kvm_close(&kvm);
    fprintf(stderr, "not ok - single IMSIC interrupt: %d\n", ret);
    return EXIT_FAILURE;
}
