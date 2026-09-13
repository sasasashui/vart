#include <asm/kvm.h>
#include <errno.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../fixtures/smp-mmio.h"
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

typedef struct MmioContext {
    VartExecution execution;
    VartTestDevice device;
    VartVm *vm;
    atomic_bool callback_active;
    atomic_bool concurrent_entry;
    atomic_bool invalid_value;
    atomic_uint writes[VART_SMP_MMIO_HARTS];
    unsigned int exits[VART_SMP_MMIO_HARTS];
} MmioContext;

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

static void record_write(void *opaque, unsigned char value)
{
    MmioContext *context = opaque;

    vart_mutex_assert_held(&context->vm->big_lock);
    if (atomic_exchange_explicit(&context->callback_active, true,
                                 memory_order_acq_rel)) {
        atomic_store_explicit(&context->concurrent_entry, true,
                              memory_order_release);
    }
    sched_yield();
    if (value == 'A' || value == 'B') {
        atomic_fetch_add_explicit(&context->writes[value - 'A'], 1,
                                  memory_order_relaxed);
    } else {
        atomic_store_explicit(&context->invalid_value, true,
                              memory_order_release);
    }
    atomic_store_explicit(&context->callback_active, false,
                          memory_order_release);
}

static int handle_exit(VartVcpu *vcpu, const VartVcpuExit *exit,
                       void *opaque)
{
    MmioContext *context = opaque;
    unsigned long hart_id = vcpu->hart_id;
    int ret;

    vart_mutex_assert_held(&vcpu->vm->big_lock);
    if (hart_id >= VART_SMP_MMIO_HARTS ||
        context->exits[hart_id]++ > VART_SMP_MMIO_ITERATIONS + 2 ||
        exit->type != VART_VCPU_EXIT_MMIO) {
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
    MmioContext context = { 0 };
    VartVcpu vcpus[VART_SMP_MMIO_HARTS];
    bool vcpu_created[VART_SMP_MMIO_HARTS] = { false, false };
    VartKvm kvm;
    VartVm vm;
    bool address_space_created = false;
    bool memory_created = false;
    bool memory_registered = false;
    bool vm_created = false;
    int ret;
    int i;

    if (argc != 2) {
        return EXIT_FAILURE;
    }
    alarm(20);

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

    vart_address_space_init(&address_space);
    address_space_created = true;
    context.vm = &vm;
    atomic_init(&context.callback_active, false);
    atomic_init(&context.concurrent_entry, false);
    atomic_init(&context.invalid_value, false);
    for (i = 0; i < VART_SMP_MMIO_HARTS; i++) {
        atomic_init(&context.writes[i], 0);
    }
    vart_test_device_init(&context.device, VART_VIRT_TEST_BASE,
                          record_write, &context);
    ret = vart_address_space_add(&address_space, &context.device.region);
    if (ret < 0) {
        goto out;
    }
    vart_execution_init(&context.execution, &address_space);

    for (i = 0; i < VART_SMP_MMIO_HARTS; i++) {
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
    if (ret < 0 || context.device.status != VART_TEST_STATUS_PASS ||
        atomic_load_explicit(&context.concurrent_entry,
                             memory_order_acquire) ||
        atomic_load_explicit(&context.invalid_value, memory_order_acquire) ||
        atomic_load_explicit(&context.writes[0], memory_order_relaxed) !=
            VART_SMP_MMIO_ITERATIONS ||
        atomic_load_explicit(&context.writes[1], memory_order_relaxed) !=
            VART_SMP_MMIO_ITERATIONS) {
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
    for (i = VART_SMP_MMIO_HARTS - 1; i >= 0; i--) {
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
        fprintf(stderr, "not ok - concurrent SMP MMIO: %s\n",
                strerror(-ret));
        return EXIT_FAILURE;
    }
    printf("ok - serialize concurrent vCPU MMIO\n");
    return EXIT_SUCCESS;
}
