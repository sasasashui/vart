#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/kvm.h"
#include "vart/memory.h"
#include "vart/vm.h"

#define TEST_GUEST_ADDR UINT64_C(0x80000000)
#define TEST_MEMORY_SIZE (16 * 1024 * 1024)

static int fail(const char *operation, int error)
{
    fprintf(stderr, "not ok - %s: %s\n", operation, strerror(-error));
    return EXIT_FAILURE;
}

int main(void)
{
    VartMemoryRegion memory;
    VartKvm kvm;
    VartVm vm;
    uint64_t *word;
    int ret;

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
        vart_vm_destroy(&vm);
        vart_kvm_close(&kvm);
        return fail("allocate guest memory", ret);
    }

    word = memory.host_addr;
    word[0] = UINT64_C(0x1122334455667788);
    word[TEST_MEMORY_SIZE / sizeof(*word) - 1] =
        UINT64_C(0x8877665544332211);

    ret = vart_memory_region_register(&memory, &vm);
    if (ret < 0) {
        goto fail_memory;
    }
    ret = vart_memory_region_unregister(&memory, &vm);
    if (ret < 0) {
        goto fail_memory;
    }

    vart_memory_region_destroy(&memory);
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    printf("ok - create VM and register guest memory\n");
    return EXIT_SUCCESS;

fail_memory:
    if (memory.registered) {
        vart_memory_region_unregister(&memory, &vm);
    }
    vart_memory_region_destroy(&memory);
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    return fail("register guest memory", ret);
}
