#ifndef VART_VM_H
#define VART_VM_H

#include "vart/kvm.h"
#include "vart/sync.h"

typedef struct VartVm {
    const VartKvm *kvm;
    int fd;
    VartMutex big_lock;
} VartVm;

int vart_vm_create(VartVm *vm, const VartKvm *kvm);
void vart_vm_destroy(VartVm *vm);
int vart_vm_check_device(const VartVm *vm, unsigned int type);

#endif
