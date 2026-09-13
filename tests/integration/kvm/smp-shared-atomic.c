#include <asm/kvm.h>
#include <errno.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../fixtures/smp-shared.h"
#include "vart/address-space.h"
#include "vart/devices/test-device.h"
#include "vart/exec.h"
#include "vart/kvm.h"
#include "vart/machine/virt.h"
#include "vart/memory.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

#define GUEST_BASE VART_VIRT_DRAM_BASE
#define GUEST_RAM_SIZE (16 * 1024 * 1024)

typedef struct SharedState {
    uint32_t arrived;
    uint32_t finished;
    uint64_t counter;
    uint64_t markers[VART_SMP_HARTS];
} SharedState;

typedef struct SmpContext {
    VartExecution *execution;
    VartTestDevice *device;
    unsigned int exits;
} SmpContext;

_Static_assert(offsetof(SharedState, arrived) == VART_SMP_ARRIVED_OFFSET,
               "arrived offset mismatch");
_Static_assert(offsetof(SharedState, finished) == VART_SMP_FINISHED_OFFSET,
               "finished offset mismatch");
_Static_assert(offsetof(SharedState, counter) == VART_SMP_COUNTER_OFFSET,
               "counter offset mismatch");
_Static_assert(offsetof(SharedState, markers) == VART_SMP_MARKERS_OFFSET,
               "markers offset mismatch");

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
    return vart_memory_region_write(memory, GUEST_BASE, image, size);
}

static int handle_exit(VartVcpu *vcpu, const VartVcpuExit *exit,
                       void *opaque)
{
    SmpContext *context = opaque;
    int ret;

    vart_mutex_assert_held(&vcpu->vm->big_lock);
    if (vcpu->hart_id != 0 || context->exits++ >= 4 ||
        exit->type != VART_VCPU_EXIT_MMIO) {
        return -EIO;
    }
    ret = vart_execution_handle_exit(context->execution, vcpu, exit);
    if (ret < 0) {
        return ret;
    }
    return context->device->status == VART_TEST_STATUS_NONE ? 0 : 1;
}

static int prepare_vcpu(VartVcpu *vcpu, VartVm *vm, unsigned long hart_id)
{
    uint32_t state;
    int ret;

    ret = vart_vcpu_create(vcpu, vm, hart_id);
    if (ret < 0) {
        return ret;
    }
    ret = vart_vcpu_set_pc(vcpu, GUEST_BASE);
    if (ret == 0) {
        ret = vart_vcpu_set_mode(vcpu, KVM_RISCV_MODE_S);
    }
    if (ret == 0) {
        ret = vart_vcpu_set_gpr(vcpu, 10, hart_id);
    }
    if (ret == 0) {
        ret = vart_vcpu_set_mp_state(vcpu, KVM_MP_STATE_RUNNABLE);
    }
    if (ret == 0) {
        ret = vart_vcpu_get_mp_state(vcpu, &state);
    }
    if (ret == 0 && state != KVM_MP_STATE_RUNNABLE) {
        ret = -EIO;
    }
    if (ret < 0) {
        vart_vcpu_destroy(vcpu);
    }
    return ret;
}

int main(int argc, char **argv)
{
    VartAddressSpace address_space;
    VartMemoryRegion memory;
    VartTestDevice device;
    VartExecution execution;
    SmpContext context;
    VartVcpu vcpus[VART_SMP_HARTS];
    bool vcpu_created[VART_SMP_HARTS] = { false, false };
    VartKvm kvm;
    VartVm vm;
    SharedState *shared = NULL;
    bool address_space_created = false;
    bool memory_created = false;
    bool memory_registered = false;
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
    ret = vart_memory_region_create(&memory, GUEST_BASE, GUEST_RAM_SIZE, 0);
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
                             VART_SMP_SHARED_GPA - GUEST_BASE);

    vart_address_space_init(&address_space);
    address_space_created = true;
    vart_test_device_init(&device, VART_VIRT_TEST_BASE, NULL, NULL);
    ret = vart_address_space_add(&address_space, &device.region);
    if (ret < 0) {
        goto out;
    }
    vart_execution_init(&execution, &address_space);
    context.execution = &execution;
    context.device = &device;
    context.exits = 0;

    for (i = 0; i < VART_SMP_HARTS; i++) {
        ret = prepare_vcpu(&vcpus[i], &vm, (unsigned long)i);
        if (ret < 0) {
            goto out;
        }
        vcpu_created[i] = true;
        ret = vart_vcpu_start(&vcpus[i], handle_exit, &context);
        if (ret < 0) {
            goto out;
        }
    }

    ret = vart_vcpu_join(&vcpus[0]);
    if (ret < 0 || device.status != VART_TEST_STATUS_PASS ||
        shared->arrived != VART_SMP_HARTS ||
        shared->finished != VART_SMP_HARTS ||
        shared->counter != VART_SMP_ITERATIONS * VART_SMP_HARTS ||
        shared->markers[0] != 1 || shared->markers[1] != 2) {
        ret = -EIO;
        goto out;
    }
    ret = vart_vm_request_shutdown(&vm);
    if (ret == 0) {
        ret = vart_vcpu_join(&vcpus[1]);
    }

out:
    if (vm_created) {
        vart_vm_request_shutdown(&vm);
    }
    for (i = VART_SMP_HARTS - 1; i >= 0; i--) {
        if (vcpu_created[i]) {
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
        fprintf(stderr, "not ok - shared-RAM atomic SMP: %s\n",
                strerror(-ret));
        return EXIT_FAILURE;
    }
    printf("ok - coordinate vCPUs through shared-RAM atomics\n");
    return EXIT_SUCCESS;
}
