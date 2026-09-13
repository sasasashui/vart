#include <asm/kvm.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../fixtures/smp-hart-state.h"
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

typedef struct HartStateContext {
    VartExecution execution;
    VartTestDevice device;
    VartVcpu *active_vcpu;
    VartVcpu *secondary;
    uint32_t *stopped_ack;
    int control_error;
    bool secondary_stopped;
    unsigned int start_commands;
    unsigned int stop_commands;
} HartStateContext;

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

static void control_hart(void *opaque, unsigned char command)
{
    HartStateContext *context = opaque;
    VartVcpu *vcpu = context->active_vcpu;
    int ret = -EINVAL;

    if (vcpu == NULL) {
        context->control_error = -EINVAL;
        return;
    }
    vart_mutex_assert_held(&vcpu->vm->big_lock);
    if (command == VART_HART_STATE_START_COMMAND && vcpu->hart_id == 0 &&
        context->start_commands++ == 0) {
        ret = vart_vcpu_set_mp_state_locked(context->secondary,
                                             KVM_MP_STATE_RUNNABLE);
    } else if (command == VART_HART_STATE_STOP_COMMAND &&
               vcpu == context->secondary && context->stop_commands++ == 0) {
        uint32_t state;

        ret = vart_vcpu_set_mp_state_locked(context->secondary,
                                             KVM_MP_STATE_STOPPED);
        if (ret == 0) {
            ret = vart_vcpu_get_mp_state(context->secondary, &state);
        }
        if (ret == 0 && state != KVM_MP_STATE_STOPPED) {
            ret = -EIO;
        }
        if (ret == 0) {
            context->secondary_stopped = true;
            __atomic_store_n(context->stopped_ack, 1, __ATOMIC_RELEASE);
        }
    }
    if (ret < 0) {
        context->control_error = ret;
    }
}

static int handle_exit(VartVcpu *vcpu, const VartVcpuExit *exit,
                       void *opaque)
{
    HartStateContext *context = opaque;
    int ret;

    vart_mutex_assert_held(&vcpu->vm->big_lock);
    if (exit->type == VART_VCPU_EXIT_INTERRUPTED) {
        return 0;
    }
    if (exit->type != VART_VCPU_EXIT_MMIO) {
        return -EIO;
    }

    context->active_vcpu = vcpu;
    ret = vart_execution_handle_exit(&context->execution, vcpu, exit);
    context->active_vcpu = NULL;
    if (ret < 0) {
        return ret;
    }
    if (context->control_error < 0) {
        return context->control_error;
    }
    return context->device.status == VART_TEST_STATUS_NONE ? 0 : 1;
}

static int prepare_vcpu(VartVcpu *vcpu, VartVm *vm, unsigned long hart_id,
                        uint32_t mp_state)
{
    uint32_t actual_state;
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
        ret = vart_vcpu_set_mp_state(vcpu, mp_state);
    }
    if (ret == 0) {
        ret = vart_vcpu_get_mp_state(vcpu, &actual_state);
    }
    if (ret == 0 && actual_state != mp_state) {
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
    HartStateContext context = { 0 };
    VartVcpu vcpus[VART_HART_STATE_HARTS];
    bool vcpu_created[VART_HART_STATE_HARTS] = { false, false };
    VartKvm kvm;
    VartVm vm;
    uint32_t *shared = NULL;
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
    shared = (uint32_t *)((char *)memory.host_addr +
                          VART_HART_STATE_SHARED_GPA - GUEST_BASE);
    context.stopped_ack = &shared[VART_HART_STATE_STOPPED_ACK_OFFSET /
                                  sizeof(*shared)];

    vart_address_space_init(&address_space);
    address_space_created = true;
    vart_test_device_init(&context.device, VART_VIRT_TEST_BASE,
                          control_hart, &context);
    ret = vart_address_space_add(&address_space, &context.device.region);
    if (ret < 0) {
        goto out;
    }
    vart_execution_init(&context.execution, &address_space);

    ret = prepare_vcpu(&vcpus[0], &vm, 0, KVM_MP_STATE_RUNNABLE);
    if (ret < 0) {
        goto out;
    }
    vcpu_created[0] = true;
    ret = prepare_vcpu(&vcpus[1], &vm, 1, KVM_MP_STATE_STOPPED);
    if (ret < 0) {
        goto out;
    }
    vcpu_created[1] = true;
    context.secondary = &vcpus[1];

    ret = vart_vcpu_start(&vcpus[0], handle_exit, &context);
    if (ret < 0) {
        goto out;
    }
    ret = vart_vcpu_start(&vcpus[1], handle_exit, &context);
    if (ret < 0) {
        goto out;
    }
    nanosleep(&settle_time, NULL);
    if (vart_vcpu_thread_state(&vcpus[1]) != VART_VCPU_THREAD_RUNNING ||
        __atomic_load_n(&shared[VART_HART_STATE_MARKER_OFFSET /
                                sizeof(*shared)], __ATOMIC_ACQUIRE) != 0) {
        ret = -EIO;
        goto out;
    }

    __atomic_store_n(&shared[VART_HART_STATE_PRIMARY_GATE_OFFSET /
                             sizeof(*shared)], 1, __ATOMIC_RELEASE);
    ret = vart_vcpu_join(&vcpus[0]);
    if (ret < 0 || context.device.status != VART_TEST_STATUS_PASS ||
        context.start_commands != 1 || context.stop_commands != 1 ||
        context.control_error < 0 || !context.secondary_stopped ||
        __atomic_load_n(context.stopped_ack, __ATOMIC_ACQUIRE) != 1) {
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
    for (i = VART_HART_STATE_HARTS - 1; i >= 0; i--) {
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
        fprintf(stderr, "not ok - secondary hart MP state: %s\n",
                strerror(-ret));
        return EXIT_FAILURE;
    }
    printf("ok - start and stop a secondary hart\n");
    return EXIT_SUCCESS;
}
