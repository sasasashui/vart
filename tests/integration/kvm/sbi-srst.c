#include <errno.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vart/kvm.h"
#include "vart/machine/virt.h"
#include "vart/memory.h"
#include "vart/riscv-cpu.h"
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

static int parse_u32(const char *text, uint32_t *value)
{
    unsigned long parsed;
    char *end;

    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || *text == '\0' || *end != '\0' ||
        parsed > UINT32_MAX) {
        return -EINVAL;
    }
    *value = (uint32_t)parsed;
    return 0;
}

int main(int argc, char **argv)
{
    const VartRiscvBootInfo boot = {
        .entry = VART_VIRT_DRAM_BASE,
        .fdt_addr = FDT_ADDR,
    };
    VartMemoryRegion memory;
    VartVcpuExit exit;
    VartVcpu vcpu;
    VartKvm kvm;
    VartVm vm;
    uint32_t expected_type;
    uint32_t expected_reason;
    bool memory_registered = false;
    bool memory_created = false;
    bool vcpu_created = false;
    bool vm_created = false;
    int ret;

    if (argc != 4 || parse_u32(argv[2], &expected_type) < 0 ||
        parse_u32(argv[3], &expected_reason) < 0) {
        return EXIT_FAILURE;
    }
    alarm(10);
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
    if (ret == 0 &&
        (exit.type != VART_VCPU_EXIT_SYSTEM_EVENT ||
         exit.kvm_reason != KVM_EXIT_SYSTEM_EVENT ||
         exit.system_event.type != expected_type ||
         exit.system_event.ndata != 1 ||
         exit.system_event.data[0] != expected_reason)) {
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
        fprintf(stderr, "not ok - KVM SBI SRST: %s\n", strerror(-ret));
        return EXIT_FAILURE;
    }
    printf("ok - KVM SBI SRST event %u reason %u\n",
           expected_type, expected_reason);
    return EXIT_SUCCESS;
}
