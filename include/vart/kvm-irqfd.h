#ifndef VART_KVM_IRQFD_H
#define VART_KVM_IRQFD_H

#include <stdint.h>

#include "vart/eventfd.h"
#include "vart/vm.h"

int vart_kvm_irqfd_assign(VartVm *vm, const VartEventNotifier *notifier,
                          uint32_t gsi);
int vart_kvm_irqfd_deassign(VartVm *vm, const VartEventNotifier *notifier,
                            uint32_t gsi);

#endif
