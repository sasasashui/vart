#include <errno.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../fixtures/sbi-userspace.h"
#include "vart/kvm.h"
#include "vart/machine/virt.h"
#include "vart/memory.h"
#include "vart/riscv-cpu.h"
#include "vart/riscv-sbi.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

#define GUEST_RAM_SIZE (16 * 1024 * 1024)
#define FDT_ADDR (VART_VIRT_DRAM_BASE + GUEST_RAM_SIZE - 0x1000)

static int load_guest(VartMemoryRegion *memory, const char *path)
{
    unsigned char image[4096];
    size_t size;
    FILE *file;
    int ret;

    file = fopen(path, "rb");
    if (file == NULL) {
        return -errno;
    }
    size = fread(image, 1, sizeof(image), file);
    ret = ferror(file) ? -EIO : 0;
    if (ret == 0 && !feof(file)) {
        ret = -EFBIG;
    }
    if (ret == 0 && size == 0) {
        ret = -ENODATA;
    }
    fclose(file);
    if (ret < 0) {
        return ret;
    }
    return vart_memory_region_write(memory, VART_VIRT_DRAM_BASE,
                                    image, size);
}

static int handle_vendor(const VartVcpuExit *exit,
                         VartRiscvSbiResponse *response, void *opaque)
{
    unsigned int *calls = opaque;
    unsigned int i;

    if (exit->sbi.function_id != VART_SBI_USER_FUNCTION) {
        return -EIO;
    }
    for (i = 0; i < 6; i++) {
        if (exit->sbi.args[i] != 6 - i) {
            return -EIO;
        }
    }
    (*calls)++;
    response->error = 0;
    response->value = VART_SBI_USER_VALUE;
    return 0;
}

static int check_experimental_exit(const VartVcpuExit *exit)
{
    unsigned int i;

    if (exit->type != VART_VCPU_EXIT_RISCV_SBI ||
        exit->kvm_reason != KVM_EXIT_RISCV_SBI ||
        exit->sbi.extension_id != VART_SBI_USER_EXPERIMENTAL_EXT ||
        exit->sbi.function_id != VART_SBI_USER_FUNCTION) {
        return -EIO;
    }
    for (i = 0; i < 6; i++) {
        if (exit->sbi.args[i] != i + 1) {
            return -EIO;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    const VartRiscvBootInfo boot = {
        .entry = VART_VIRT_DRAM_BASE,
        .fdt_addr = FDT_ADDR,
    };
    VartRiscvSbiDispatcher dispatcher;
    VartRiscvSbiHandler vendor;
    VartRiscvSbiHandler overlap;
    VartMemoryRegion memory;
    VartVcpuExit exit;
    VartVcpu vcpu;
    VartKvm kvm;
    VartVm vm;
    unsigned int calls = 0;
    bool memory_registered = false;
    bool memory_created = false;
    bool vcpu_created = false;
    bool vm_created = false;
    int ret;

    if (argc != 2) {
        return EXIT_FAILURE;
    }
    alarm(10);
    vart_riscv_sbi_dispatcher_init(&dispatcher);
    vart_riscv_sbi_handler_init(&vendor, VART_SBI_USER_VENDOR_EXT,
                                VART_SBI_USER_VENDOR_EXT,
                                handle_vendor, &calls);
    ret = vart_riscv_sbi_add_handler(&dispatcher, &vendor);
    vart_riscv_sbi_handler_init(&overlap, VART_SBI_USER_VENDOR_EXT - 1,
                                VART_SBI_USER_VENDOR_EXT,
                                handle_vendor, &calls);
    if (ret < 0 || vart_riscv_sbi_add_handler(&dispatcher, &overlap) !=
                   -EEXIST) {
        return EXIT_FAILURE;
    }

    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        return EXIT_FAILURE;
    }
    ret = vart_vm_create(&vm, &kvm);
    if (ret < 0) {
        goto out;
    }
    vm_created = true;
    ret = vart_memory_region_create(&memory, VART_VIRT_DRAM_BASE,
                                    GUEST_RAM_SIZE, 0);
    if (ret < 0) {
        goto out;
    }
    memory_created = true;
    ret = load_guest(&memory, argv[1]);
    if (ret == 0) {
        ret = vart_memory_region_register(&memory, &vm);
    }
    if (ret < 0) {
        goto out;
    }
    memory_registered = true;
    ret = vart_vcpu_create(&vcpu, &vm, 0);
    if (ret < 0) {
        goto out;
    }
    vcpu_created = true;
    ret = vart_riscv_vcpu_init_boot(&vcpu, &boot);
    if (ret == 0) {
        ret = vart_vcpu_run(&vcpu, &exit);
    }
    if (ret == 0) {
        ret = check_experimental_exit(&exit);
    }
    if (ret == 0) {
        ret = vart_riscv_sbi_dispatch(&dispatcher, &vcpu, &exit);
    }
    if (ret == 0 && vart_vcpu_complete_sbi(&vcpu, 0, 0) != -EINVAL) {
        ret = -EIO;
    }

    if (ret == 0) {
        ret = vart_vcpu_run(&vcpu, &exit);
    }
    if (ret == 0 &&
        (exit.type != VART_VCPU_EXIT_RISCV_SBI ||
         exit.sbi.extension_id != VART_SBI_USER_VENDOR_EXT)) {
        ret = -EIO;
    }
    if (ret == 0) {
        ret = vart_riscv_sbi_dispatch(&dispatcher, &vcpu, &exit);
    }
    if (ret == 0) {
        ret = vart_vcpu_run(&vcpu, &exit);
    }
    if (ret == 0 &&
        (exit.type != VART_VCPU_EXIT_MMIO ||
         exit.mmio.address != VART_VIRT_TEST_BASE + 16 ||
         exit.mmio.size != 4 || !exit.mmio.is_write ||
         exit.mmio.data[0] != 1 || calls != 1)) {
        ret = -EIO;
    }

out:
    if (vcpu_created) {
        vart_vcpu_destroy(&vcpu);
    }
    if (memory_registered) {
        vart_memory_region_unregister(&memory, &vm);
    }
    if (memory_created) {
        vart_memory_region_destroy(&memory);
    }
    if (vm_created) {
        vart_vm_destroy(&vm);
    }
    vart_kvm_close(&kvm);
    alarm(0);

    if (ret < 0) {
        fprintf(stderr, "not ok - userspace SBI dispatch: %s\n",
                strerror(-ret));
        return EXIT_FAILURE;
    }
    printf("ok - dispatch userspace SBI extensions\n");
    return EXIT_SUCCESS;
}
