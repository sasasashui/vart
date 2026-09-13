#include <errno.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../fixtures/riscv-direct-boot.h"
#include "vart/address-space.h"
#include "vart/devices/test-device.h"
#include "vart/exec.h"
#include "vart/kvm.h"
#include "vart/memory.h"
#include "vart/riscv-cpu.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

#define GUEST_RAM_SIZE (16 * 1024 * 1024)

typedef struct BootContext {
    VartExecution execution;
    VartTestDevice device;
    unsigned int exits;
} BootContext;

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
    ret = vart_memory_region_write(memory, VART_DIRECT_BOOT_ENTRY, image,
                                   (size_t)file_size);
out:
    free(image);
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

static int verify_host_state(VartVcpu *vcpu,
                             const VartRiscvBootInfo *boot)
{
    VartRiscvCpuState *state = &vcpu->cpu_state;
    unsigned int i;
    int ret;

    ret = vart_riscv_vcpu_get_registers(vcpu, VART_RISCV_REG_CORE |
                                              VART_RISCV_REG_CSR);
    if (ret < 0) {
        return ret;
    }
    if (state->core.pc != boot->entry ||
        state->core.gpr[10] != vcpu->hart_id ||
        state->core.gpr[11] != boot->fdt_addr ||
        state->core.mode != KVM_RISCV_MODE_S) {
        return -EIO;
    }
    for (i = 0; i < 32; i++) {
        if (i != 10 && i != 11 && state->core.gpr[i] != 0) {
            return -EIO;
        }
    }
    return state->csr.sstatus == 0 && state->csr.satp == 0 &&
           state->csr.stvec == 0 ? 0 : -EIO;
}

int main(int argc, char **argv)
{
    const VartRiscvBootInfo boot = {
        .entry = VART_DIRECT_BOOT_ENTRY,
        .fdt_addr = VART_DIRECT_BOOT_FDT_ADDR,
    };
    VartRiscvBootInfo invalid_boot;
    VartAddressSpace address_space;
    VartMemoryRegion memory;
    VartVcpu secondary;
    BootContext context;
    VartVcpu primary;
    VartKvm kvm;
    VartVm vm;
    uint32_t mp_state;
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
    ret = vart_memory_region_create(&memory, VART_DIRECT_BOOT_ENTRY,
                                    GUEST_RAM_SIZE, 0);
    if (ret < 0) {
        goto fail_memory_create;
    }
    if ((ret = load_guest(&memory, argv[1])) < 0 ||
        (ret = vart_memory_region_register(&memory, &vm)) < 0) {
        goto fail_memory;
    }
    ret = vart_vcpu_create(&primary, &vm, 0);
    if (ret < 0) {
        goto fail_registered;
    }
    ret = vart_vcpu_create(&secondary, &vm, 1);
    if (ret < 0) {
        goto fail_primary;
    }

    if (vart_riscv_vcpu_init_boot(NULL, &boot) != -EINVAL ||
        vart_riscv_vcpu_init_boot(&primary, NULL) != -EINVAL) {
        ret = -EINVAL;
        goto fail_secondary;
    }
    invalid_boot = boot;
    invalid_boot.entry = 0;
    if (vart_riscv_vcpu_init_boot(&primary, &invalid_boot) != -EINVAL) {
        ret = -EINVAL;
        goto fail_secondary;
    }
    invalid_boot = boot;
    invalid_boot.entry++;
    if (vart_riscv_vcpu_init_boot(&primary, &invalid_boot) != -EINVAL) {
        ret = -EINVAL;
        goto fail_secondary;
    }
    invalid_boot = boot;
    invalid_boot.fdt_addr = 0;
    if (vart_riscv_vcpu_init_boot(&primary, &invalid_boot) != -EINVAL) {
        ret = -EINVAL;
        goto fail_secondary;
    }
    invalid_boot = boot;
    invalid_boot.fdt_addr += 4;
    if (vart_riscv_vcpu_init_boot(&primary, &invalid_boot) != -EINVAL) {
        ret = -EINVAL;
        goto fail_secondary;
    }
    primary.thread_state = VART_VCPU_THREAD_RUNNING;
    ret = vart_riscv_vcpu_init_boot(&primary, &boot);
    primary.thread_state = VART_VCPU_THREAD_CREATED;
    if (ret != -EBUSY) {
        ret = -EINVAL;
        goto fail_secondary;
    }
    ret = vart_riscv_vcpu_init_boot(&primary, &boot);
    if (ret < 0 || (ret = verify_host_state(&primary, &boot)) < 0 ||
        (ret = vart_vcpu_get_mp_state(&primary, &mp_state)) < 0 ||
        mp_state != KVM_MP_STATE_RUNNABLE) {
        ret = ret < 0 ? ret : -EIO;
        goto fail_secondary;
    }
    ret = vart_riscv_vcpu_init_boot(&secondary, &boot);
    if (ret < 0 || (ret = verify_host_state(&secondary, &boot)) < 0 ||
        (ret = vart_vcpu_get_mp_state(&secondary, &mp_state)) < 0 ||
        mp_state != KVM_MP_STATE_STOPPED) {
        ret = ret < 0 ? ret : -EIO;
        goto fail_secondary;
    }

    vart_address_space_init(&address_space);
    ret = vart_test_device_init(&context.device, VART_DIRECT_BOOT_DEVICE,
                                NULL, NULL);
    if (ret < 0) {
        goto fail_address_space;
    }
    ret = vart_address_space_add(&address_space, &context.device.region);
    if (ret < 0) {
        goto fail_address_space;
    }
    vart_execution_init(&context.execution, &address_space);
    context.exits = 0;

    ret = vart_vcpu_start(&primary, handle_exit, &context);
    if (ret < 0) {
        goto fail_address_space;
    }
    ret = vart_vcpu_join(&primary);
    if (ret < 0 || context.device.status != VART_TEST_STATUS_PASS) {
        ret = ret < 0 ? ret : -EIO;
        goto fail_address_space;
    }

    vart_address_space_destroy(&address_space);
    vart_vcpu_destroy(&secondary);
    vart_vcpu_destroy(&primary);
    vart_memory_region_unregister(&memory, &vm);
    vart_memory_region_destroy(&memory);
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    alarm(0);
    printf("ok - initialize the RISC-V KVM direct-boot state\n");
    return EXIT_SUCCESS;

fail_address_space:
    vart_address_space_destroy(&address_space);
fail_secondary:
    vart_vcpu_destroy(&secondary);
fail_primary:
    vart_vcpu_destroy(&primary);
fail_registered:
    vart_memory_region_unregister(&memory, &vm);
fail_memory:
    vart_memory_region_destroy(&memory);
fail_memory_create:
    vart_vm_destroy(&vm);
fail_vm:
    vart_kvm_close(&kvm);
    fprintf(stderr, "not ok - RISC-V direct boot: %d\n", ret);
    return EXIT_FAILURE;
}
