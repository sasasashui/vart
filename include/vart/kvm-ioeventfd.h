#ifndef VART_KVM_IOEVENTFD_H
#define VART_KVM_IOEVENTFD_H

#include <stdint.h>

#include "vart/eventfd.h"
#include "vart/vm.h"

int vart_kvm_ioeventfd_assign(VartVm *vm,
                              const VartEventNotifier *notifier,
                              uint64_t address, uint8_t length);
int vart_kvm_ioeventfd_deassign(VartVm *vm,
                                const VartEventNotifier *notifier,
                                uint64_t address, uint8_t length);

#endif
