#ifndef VART_VM_H
#define VART_VM_H

#include "vart/kvm.h"
#include "vart/sync.h"

struct VartVcpu;

typedef struct VartVm {
    const VartKvm *kvm;
    int fd;
    VartMutex big_lock;
    struct VartVcpu *vcpus;
    bool shutdown_requested;
} VartVm;

int vart_vm_create(VartVm *vm, const VartKvm *kvm);
void vart_vm_destroy(VartVm *vm);
int vart_vm_check_device(const VartVm *vm, unsigned int type);
int vart_vm_request_shutdown(VartVm *vm);
int vart_vm_request_shutdown_locked(VartVm *vm);

#endif
