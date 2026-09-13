#include <errno.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../fixtures/sbi-hsm.h"
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

typedef struct SharedState {
    uint32_t starts;
    uint32_t stop_request;
    uint32_t stop_returned;
    uint32_t reserved;
    uint64_t hart_id;
    uint64_t opaque;
    uint64_t reserved2;
    uint64_t entry;
} SharedState;

typedef struct HsmContext {
    VartExecution execution;
    VartTestDevice device;
    unsigned int exits;
} HsmContext;

_Static_assert(offsetof(SharedState, entry) == VART_SBI_HSM_ENTRY_OFFSET,
               "entry offset mismatch");

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

static int handle_exit(VartVcpu *vcpu, const VartVcpuExit *exit,
                       void *opaque)
{
    HsmContext *context = opaque;
    int ret;

    vart_mutex_assert_held(&vcpu->vm->big_lock);
    if (vcpu->hart_id != 0 || context->exits++ != 0 ||
        exit->type != VART_VCPU_EXIT_MMIO) {
        /* HSM calls and secondary-hart state changes must stay in KVM. */
        return -EIO;
    }
    ret = vart_execution_handle_exit(&context->execution, vcpu, exit);
    if (ret < 0) {
        return ret;
    }
    return context->device.status == VART_TEST_STATUS_NONE ? 0 : 1;
}

static int prepare_vcpu(VartVcpu *vcpu, VartVm *vm, unsigned long hart_id)
{
    const VartRiscvBootInfo boot = {
        .entry = VART_VIRT_DRAM_BASE,
        .fdt_addr = FDT_ADDR,
    };
    uint32_t expected = hart_id == 0 ? KVM_MP_STATE_RUNNABLE :
                                      KVM_MP_STATE_STOPPED;
    uint32_t state;
    int ret;

    ret = vart_vcpu_create(vcpu, vm, hart_id);
    if (ret < 0) {
        return ret;
    }
    ret = vart_riscv_vcpu_init_boot(vcpu, &boot);
    if (ret == 0) {
        ret = vart_vcpu_get_mp_state(vcpu, &state);
    }
    if (ret == 0 && state != expected) {
        ret = -EIO;
    }
    if (ret < 0) {
        vart_vcpu_destroy(vcpu);
    }
    return ret;
}

int main(int argc, char **argv)
{
    static const struct timespec settle_time = {
        .tv_nsec = 20 * 1000 * 1000,
    };
    VartAddressSpace address_space;
    VartMemoryRegion memory;
    VartVcpu vcpus[VART_SBI_HSM_HARTS];
    bool created[VART_SBI_HSM_HARTS] = { false, false };
    bool started[VART_SBI_HSM_HARTS] = { false, false };
    bool joined[VART_SBI_HSM_HARTS] = { false, false };
    HsmContext context = { 0 };
    SharedState *shared = NULL;
    VartKvm kvm;
    VartVm vm;
    bool address_space_created = false;
    bool memory_registered = false;
    bool memory_created = false;
    bool vm_created = false;
    int ret;
    int i;

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
    shared = (SharedState *)((char *)memory.host_addr +
             VART_SBI_HSM_SHARED_GPA - VART_VIRT_DRAM_BASE);

    vart_address_space_init(&address_space);
    address_space_created = true;
    ret = vart_test_device_init(&context.device, VART_VIRT_TEST_BASE,
                                NULL, NULL);
    if (ret == 0) {
        ret = vart_address_space_add(&address_space,
                                     &context.device.region);
    }
    if (ret < 0) {
        goto out;
    }
    vart_execution_init(&context.execution, &address_space);

    for (i = 0; i < VART_SBI_HSM_HARTS; i++) {
        ret = prepare_vcpu(&vcpus[i], &vm, (unsigned long)i);
        if (ret < 0) {
            goto out;
        }
        created[i] = true;
    }

    /* Start the STOPPED worker first and prove that it executes no guest code. */
    ret = vart_vcpu_start(&vcpus[1], handle_exit, &context);
    if (ret < 0) {
        goto out;
    }
    started[1] = true;
    nanosleep(&settle_time, NULL);
    if (vart_vcpu_thread_state(&vcpus[1]) != VART_VCPU_THREAD_RUNNING ||
        __atomic_load_n(&shared->starts, __ATOMIC_ACQUIRE) != 0) {
        ret = -EIO;
        goto out;
    }

    ret = vart_vcpu_start(&vcpus[0], handle_exit, &context);
    if (ret < 0) {
        goto out;
    }
    started[0] = true;
    ret = vart_vcpu_join(&vcpus[0]);
    if (ret < 0) {
        goto out;
    }
    joined[0] = true;
    if (context.device.status != VART_TEST_STATUS_PASS ||
        context.exits != 1 ||
        __atomic_load_n(&shared->starts, __ATOMIC_ACQUIRE) != 2 ||
        __atomic_load_n(&shared->stop_returned, __ATOMIC_ACQUIRE) != 0 ||
        shared->hart_id != 1 ||
        shared->opaque != VART_SBI_HSM_OPAQUE_SECOND) {
        ret = -EIO;
        goto out;
    }
    ret = 0;

out:
    if (vm_created && (started[0] || started[1])) {
        int shutdown_ret = vart_vm_request_shutdown(&vm);

        if (ret == 0 && shutdown_ret < 0) {
            ret = shutdown_ret;
        }
    }
    for (i = 0; i < VART_SBI_HSM_HARTS; i++) {
        if (started[i] && !joined[i]) {
            int join_ret = vart_vcpu_join(&vcpus[i]);

            if (ret == 0 && join_ret < 0) {
                ret = join_ret;
            }
        }
    }
    for (i = VART_SBI_HSM_HARTS - 1; i >= 0; i--) {
        if (created[i]) {
            vart_vcpu_destroy(&vcpus[i]);
        }
    }
    if (address_space_created) {
        vart_address_space_destroy(&address_space);
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
        fprintf(stderr, "not ok - KVM SBI HSM: %s\n", strerror(-ret));
        return EXIT_FAILURE;
    }
    printf("ok - KVM SBI HSM hart lifecycle\n");
    return EXIT_SUCCESS;
}
