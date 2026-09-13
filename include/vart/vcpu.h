#ifndef VART_VCPU_H
#define VART_VCPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>

#include <linux/kvm.h>

#include "vart/vm.h"

typedef enum VartVcpuExitType {
    VART_VCPU_EXIT_MMIO,
    VART_VCPU_EXIT_SYSTEM_EVENT,
    VART_VCPU_EXIT_SHUTDOWN,
    VART_VCPU_EXIT_INTERRUPTED,
    VART_VCPU_EXIT_UNKNOWN,
} VartVcpuExitType;

typedef struct VartVcpuExit {
    VartVcpuExitType type;
    unsigned int kvm_reason;
    union {
        struct {
            uint64_t address;
            uint8_t data[8];
            uint32_t size;
            bool is_write;
        } mmio;
        struct {
            uint32_t type;
            uint64_t flags;
        } system_event;
    };
} VartVcpuExit;

typedef enum VartVcpuThreadState {
    VART_VCPU_THREAD_CREATED,
    VART_VCPU_THREAD_RUNNING,
    VART_VCPU_THREAD_STOPPED,
} VartVcpuThreadState;

typedef struct VartVcpu VartVcpu;

/* Return zero to resume the guest, positive to stop, or a negative errno. */
typedef int (*VartVcpuExitHandler)(VartVcpu *vcpu,
                                   const VartVcpuExit *exit, void *opaque);

struct VartVcpu {
    VartVm *vm;
    int fd;
    unsigned long hart_id;
    struct kvm_run *run;
    size_t run_size;
    bool mmio_read_pending;
    unsigned int mmio_read_size;
    pthread_t thread;
    VartVcpuThreadState thread_state;
    bool thread_created;
    bool thread_joined;
    int thread_result;
    VartVcpuExitHandler exit_handler;
    void *exit_opaque;
    atomic_bool kick_requested;
    atomic_bool stop_requested;
    struct VartVcpu *next;
};

int vart_vcpu_create(VartVcpu *vcpu, VartVm *vm, unsigned long hart_id);
void vart_vcpu_destroy(VartVcpu *vcpu);

int vart_vcpu_get_one_reg(const VartVcpu *vcpu, uint64_t reg_id,
                          void *value);
int vart_vcpu_set_one_reg(const VartVcpu *vcpu, uint64_t reg_id,
                          const void *value);
int vart_vcpu_get_pc(const VartVcpu *vcpu, uint64_t *value);
int vart_vcpu_set_pc(const VartVcpu *vcpu, uint64_t value);
int vart_vcpu_set_mode(const VartVcpu *vcpu, unsigned long mode);
int vart_vcpu_get_gpr(const VartVcpu *vcpu, unsigned int index,
                      uint64_t *value);
int vart_vcpu_set_gpr(const VartVcpu *vcpu, unsigned int index,
                      uint64_t value);
int vart_vcpu_run(VartVcpu *vcpu, VartVcpuExit *exit);
int vart_vcpu_complete_mmio_read(VartVcpu *vcpu, uint64_t value);
int vart_vcpu_start(VartVcpu *vcpu, VartVcpuExitHandler handler,
                    void *opaque);
int vart_vcpu_join(VartVcpu *vcpu);
VartVcpuThreadState vart_vcpu_thread_state(VartVcpu *vcpu);
int vart_vcpu_kick(VartVcpu *vcpu);
int vart_vcpu_request_stop(VartVcpu *vcpu);

#endif
