#ifndef VART_KVM_H
#define VART_KVM_H

#include <stdbool.h>

typedef struct VartKvm {
    int fd;
    int api_version;
    int vcpu_mmap_size;
    int recommended_vcpus;
    int max_vcpus;
    bool user_memory;
    bool one_reg;
    bool irqfd;
    bool ioeventfd;
    bool immediate_exit;
} VartKvm;

int vart_kvm_open(VartKvm *kvm);
void vart_kvm_close(VartKvm *kvm);
int vart_kvm_check_extension(const VartKvm *kvm, int capability);

#endif
