#include <asm/kvm.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vart/kvm.h"
#include "vart/machine/virt.h"
#include "vart/memory.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

#define GUEST_BASE VART_VIRT_DRAM_BASE
#define GUEST_RAM_SIZE (16 * 1024 * 1024)

typedef struct KickContext {
    VartCond cond;
    unsigned int kicks;
    bool kick_unblocked;
} KickContext;

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

static int handle_kick(VartVcpu *vcpu, const VartVcpuExit *exit,
                       void *opaque)
{
    KickContext *context = opaque;
    sigset_t mask;

    vart_mutex_assert_held(&vcpu->vm->big_lock);
    if (exit->type != VART_VCPU_EXIT_INTERRUPTED) {
        return -EIO;
    }
    pthread_sigmask(SIG_BLOCK, NULL, &mask);
    context->kick_unblocked =
        sigismember(&mask, VART_VCPU_KICK_SIGNAL) == 0;
    context->kicks++;
    vart_cond_broadcast(&context->cond);
    return 0;
}

static int prepare_vcpu(VartVcpu *vcpu, VartVm *vm, unsigned long hart_id)
{
    int ret;

    ret = vart_vcpu_create(vcpu, vm, hart_id);
    if (ret < 0) {
        return ret;
    }
    ret = vart_vcpu_set_pc(vcpu, GUEST_BASE);
    if (ret == 0) {
        ret = vart_vcpu_set_mode(vcpu, KVM_RISCV_MODE_S);
    }
    if (ret < 0) {
        vart_vcpu_destroy(vcpu);
    }
    return ret;
}

int main(int argc, char **argv)
{
    KickContext context = { 0 };
    VartMemoryRegion memory;
    VartVcpu vcpus[2];
    bool vcpu_created[2] = { false, false };
    VartKvm kvm;
    VartVm vm;
    bool memory_created = false;
    bool memory_registered = false;
    bool cond_created = false;
    bool vm_created = false;
    sigset_t mask;
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
    ret = vart_cond_init(&context.cond);
    if (ret < 0) {
        goto out;
    }
    cond_created = true;

    for (i = 0; i < 2; i++) {
        ret = prepare_vcpu(&vcpus[i], &vm, (unsigned long)i);
        if (ret < 0) {
            goto out;
        }
        vcpu_created[i] = true;
        ret = vart_vcpu_start(&vcpus[i], handle_kick, &context);
        if (ret < 0) {
            goto out;
        }
    }

    pthread_sigmask(SIG_BLOCK, NULL, &mask);
    if (sigismember(&mask, VART_VCPU_KICK_SIGNAL) != 1) {
        ret = -EIO;
        goto out;
    }

    ret = vart_vcpu_kick(&vcpus[0]);
    if (ret < 0) {
        goto out;
    }
    vart_mutex_lock(&vm.big_lock);
    while (context.kicks == 0) {
        vart_cond_wait(&context.cond, &vm.big_lock);
    }
    if (!context.kick_unblocked) {
        ret = -EIO;
    }
    vart_mutex_unlock(&vm.big_lock);
    if (ret < 0) {
        goto out;
    }

    ret = vart_vcpu_request_stop(&vcpus[0]);
    if (ret < 0) {
        goto out;
    }
    ret = vart_vcpu_join(&vcpus[0]);
    if (ret < 0 || vart_vcpu_thread_state(&vcpus[0]) !=
                   VART_VCPU_THREAD_STOPPED) {
        ret = -EIO;
        goto out;
    }

    ret = vart_vm_request_shutdown(&vm);
    if (ret < 0) {
        goto out;
    }
    ret = vart_vcpu_join(&vcpus[1]);
    if (ret < 0 || vart_vcpu_thread_state(&vcpus[1]) !=
                   VART_VCPU_THREAD_STOPPED) {
        ret = -EIO;
        goto out;
    }

out:
    if (vm_created) {
        vart_vm_request_shutdown(&vm);
    }
    for (i = 1; i >= 0; i--) {
        if (vcpu_created[i]) {
            vart_vcpu_destroy(&vcpus[i]);
        }
    }
    if (cond_created) {
        vart_cond_destroy(&context.cond);
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
        fprintf(stderr, "not ok - kick and shutdown vCPUs: %s\n",
                strerror(-ret));
        return EXIT_FAILURE;
    }
    printf("ok - kick and shutdown vCPUs\n");
    return EXIT_SUCCESS;
}
