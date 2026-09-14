#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "vart/address-space.h"
#include "vart/devices/test-device.h"
#include "vart/exec.h"
#include "vart/kvm.h"
#include "vart/machine/virt-loader.h"
#include "vart/machine/virt.h"
#include "vart/memory.h"
#include "vart/riscv-cpu.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

#define GUEST_RAM_SIZE (64 * 1024 * 1024)

typedef struct BootContext {
    VartExecution execution;
    VartTestDevice device;
    unsigned int exits;
} BootContext;

static int read_image(const char *path, void **data, size_t *size)
{
    unsigned char *image;
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
        goto out_file;
    }
    image = malloc((size_t)file_size);
    if (image == NULL) {
        ret = -ENOMEM;
        goto out_file;
    }
    if (fread(image, (size_t)file_size, 1, file) != 1) {
        free(image);
        ret = -EIO;
        goto out_file;
    }
    *data = image;
    *size = (size_t)file_size;
out_file:
    fclose(file);
    return ret;
}

static int handle_exit(VartVcpu *vcpu, const VartVcpuExit *exit,
                       void *opaque)
{
    BootContext *context = opaque;
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
    static const char *const extensions[] = {
        "i", "m", "a", "f", "d", "c", "sstc",
    };
    static const VartVirtFdtCpu cpu = {
        .hartid = 0,
        .isa = "rv64imafdc_sstc",
        .isa_base = "rv64i",
        .isa_extensions = extensions,
        .isa_extension_count = sizeof(extensions) / sizeof(extensions[0]),
        .mmu_type = "riscv,sv48",
    };
    VartVirtBootConfig config = {
        .cpus = &cpu,
        .cpu_count = 1,
        .timebase_frequency = 10000000,
        .bootargs = "console=ttyS0",
    };
    VartAddressSpace address_space;
    VartRiscvBootInfo boot;
    VartMemoryRegion memory;
    BootContext context;
    VartVcpu vcpu;
    VartKvm kvm;
    VartVm vm;
    void *image = NULL;
    size_t image_size = 0;
    int ret;

    if (argc != 2) {
        return EXIT_FAILURE;
    }
    alarm(10);
    ret = read_image(argv[1], &image, &image_size);
    if (ret < 0) {
        goto fail_image;
    }
    config.kernel = image;
    config.kernel_size = image_size;
    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        goto fail_kvm;
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
    if ((ret = vart_virt_boot_load(&memory, &config, &boot)) < 0 ||
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
    vart_vcpu_destroy(&vcpu);
    vart_memory_region_unregister(&memory, &vm);
    vart_memory_region_destroy(&memory);
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    free(image);
    alarm(0);
    printf("ok - boot with the generated machine DTB\n");
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
fail_kvm:
    free(image);
fail_image:
    fprintf(stderr, "not ok - generated machine DTB boot: %d\n", ret);
    return EXIT_FAILURE;
}
