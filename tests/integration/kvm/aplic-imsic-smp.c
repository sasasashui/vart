#include <asm/kvm.h>
#include <errno.h>
#include <linux/kvm.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../fixtures/aia-aplic-smp.h"
#include "vart/address-space.h"
#include "vart/devices/test-device.h"
#include "vart/exec.h"
#include "vart/kvm.h"
#include "vart/machine/virt.h"
#include "vart/memory.h"
#include "vart/riscv-aia.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

#define GUEST_RAM_SIZE (16 * 1024 * 1024)
#define WAIT_ITERATIONS 10000000

typedef struct SharedState {
    _Atomic uint32_t ready;
    _Atomic uint32_t received[VART_AIA_APLIC_HARTS];
    _Atomic uint32_t error;
} SharedState;

typedef struct TestContext {
    VartExecution execution;
    VartTestDevice device;
    unsigned int exits[VART_AIA_APLIC_HARTS];
} TestContext;

_Static_assert(offsetof(SharedState, ready) == VART_AIA_APLIC_READY_OFFSET,
               "ready offset mismatch");
_Static_assert(offsetof(SharedState, received) ==
                   VART_AIA_APLIC_RECEIVED_OFFSET,
               "received offset mismatch");
_Static_assert(offsetof(SharedState, error) == VART_AIA_APLIC_ERROR_OFFSET,
               "error offset mismatch");

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

static int prepare_vcpu(VartVcpu *vcpu, VartVm *vm, unsigned long hart_id)
{
    int ret;

    ret = vart_vcpu_create(vcpu, vm, hart_id);
    if (ret == 0) {
        ret = vart_vcpu_set_pc(vcpu, VART_VIRT_DRAM_BASE);
    }
    if (ret == 0) {
        ret = vart_vcpu_set_mode(vcpu, KVM_RISCV_MODE_S);
    }
    if (ret == 0) {
        ret = vart_vcpu_set_gpr(vcpu, 10, hart_id);
    }
    if (ret == 0) {
        ret = vart_vcpu_set_mp_state(vcpu, KVM_MP_STATE_RUNNABLE);
    }
    if (ret < 0) {
        vart_vcpu_destroy(vcpu);
    }
    return ret;
}

static int handle_exit(VartVcpu *vcpu, const VartVcpuExit *exit,
                       void *opaque)
{
    TestContext *context = opaque;
    unsigned long hart = vcpu->hart_id;
    int ret;

    vart_mutex_assert_held(&vcpu->vm->big_lock);
    if (hart >= VART_AIA_APLIC_HARTS || context->exits[hart]++ >= 4 ||
        exit->type != VART_VCPU_EXIT_MMIO) {
        return -EIO;
    }
    ret = vart_execution_handle_exit(&context->execution, vcpu, exit);
    if (ret < 0) {
        return ret;
    }
    return context->device.status == VART_TEST_STATUS_NONE ? 0 : 1;
}

static int wait_for_value(_Atomic uint32_t *value, uint32_t expected)
{
    unsigned int i;

    for (i = 0; i < WAIT_ITERATIONS; i++) {
        if (atomic_load_explicit(value, memory_order_acquire) == expected) {
            return 0;
        }
        sched_yield();
    }
    return -ETIMEDOUT;
}

int main(int argc, char **argv)
{
    VartVcpu vcpus[VART_AIA_APLIC_HARTS];
    bool vcpu_created[VART_AIA_APLIC_HARTS] = { false, false };
    VartAddressSpace address_space;
    bool address_space_created = false;
    bool memory_registered = false;
    bool memory_created = false;
    bool aia_created = false;
    bool vm_created = false;
    VartMemoryRegion memory;
    TestContext context = { 0 };
    SharedState *shared;
    VartRiscvAia aia;
    VartKvm kvm;
    VartVm vm;
    uint64_t address;
    uint32_t hart_bits;
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
                             VART_AIA_APLIC_SHARED_GPA -
                             VART_VIRT_DRAM_BASE);

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

    for (i = 0; i < VART_AIA_APLIC_HARTS; i++) {
        ret = prepare_vcpu(&vcpus[i], &vm, (unsigned long)i);
        if (ret < 0) {
            goto out;
        }
        vcpu_created[i] = true;
    }
    ret = vart_riscv_aia_create(&aia, &vm,
                                KVM_DEV_RISCV_AIA_MODE_AUTO);
    if (ret < 0) {
        goto out;
    }
    aia_created = true;
    ret = vart_riscv_aia_init_aplic(&aia, VART_AIA_APLIC_HARTS,
                                    VART_VIRT_IMSIC_S_BASE,
                                    VART_VIRT_IMSIC_NUM_IDS,
                                    VART_VIRT_APLIC_S_BASE,
                                    VART_VIRT_APLIC_NUM_SOURCES);
    if (ret < 0) {
        goto out;
    }
    ret = vart_kvm_device_get_attr(&aia.device,
                                   KVM_DEV_RISCV_AIA_GRP_CONFIG,
                                   KVM_DEV_RISCV_AIA_CONFIG_HART_BITS,
                                   &hart_bits);
    for (i = 0; ret == 0 && i < VART_AIA_APLIC_HARTS; i++) {
        ret = vart_kvm_device_get_attr(
            &aia.device, KVM_DEV_RISCV_AIA_GRP_ADDR,
            KVM_DEV_RISCV_AIA_ADDR_IMSIC(i), &address);
        if (ret == 0 &&
            address != VART_VIRT_IMSIC_S_BASE +
                       (uint64_t)i * VART_VIRT_IMSIC_FILE_SIZE) {
            ret = -EIO;
        }
    }
    if (ret < 0 || hart_bits != 1) {
        ret = ret < 0 ? ret : -EIO;
        goto out;
    }
    for (i = 0; i < VART_AIA_APLIC_HARTS; i++) {
        ret = vart_vcpu_start(&vcpus[i], handle_exit, &context);
        if (ret < 0) {
            goto out;
        }
    }

    ret = wait_for_value(&shared->ready, 3);
    if (ret == 0) {
        ret = vart_riscv_aia_pulse_irq(
            &aia, VART_AIA_APLIC_SOURCE_HART1);
    }
    if (ret == 0) {
        ret = wait_for_value(&shared->received[1], 1);
    }
    if (ret == 0 &&
        atomic_load_explicit(&shared->received[0],
                             memory_order_acquire) != 0) {
        ret = -EIO;
    }
    if (ret == 0) {
        ret = vart_riscv_aia_pulse_irq(
            &aia, VART_AIA_APLIC_SOURCE_HART0);
    }
    if (ret == 0) {
        ret = vart_vcpu_join(&vcpus[0]);
    }
    if (ret < 0 || context.device.status != VART_TEST_STATUS_PASS ||
        atomic_load_explicit(&shared->received[0],
                             memory_order_acquire) != 2 ||
        atomic_load_explicit(&shared->received[1],
                             memory_order_acquire) != 1 ||
        atomic_load_explicit(&shared->error, memory_order_acquire) != 0) {
        ret = ret < 0 ? ret : -EIO;
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
    for (i = VART_AIA_APLIC_HARTS - 1; i >= 0; i--) {
        if (vcpu_created[i]) {
            vart_vcpu_destroy(&vcpus[i]);
        }
    }
    if (aia_created) {
        vart_riscv_aia_destroy(&aia);
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
        fprintf(stderr, "not ok - SMP APLIC-to-IMSIC routing: %s\n",
                strerror(-ret));
        return EXIT_FAILURE;
    }
    printf("ok - route APLIC sources to selected IMSIC files\n");
    return EXIT_SUCCESS;
}
