#include <errno.h>
#include <asm/kvm.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "vart/riscv-kvm.h"

#define RISCV_REG(type, index) \
    (KVM_REG_RISCV | KVM_REG_SIZE_U64 | (type) | (index))

typedef struct VartRiscvKvmProbe {
    uint64_t id;
    VartRiscvKvmFeature *feature;
} VartRiscvKvmProbe;

static const char *const isa_names[VART_RISCV_KVM_ISA_COUNT] = {
    [VART_RISCV_KVM_ISA_I] = "I",
    [VART_RISCV_KVM_ISA_M] = "M",
    [VART_RISCV_KVM_ISA_A] = "A",
    [VART_RISCV_KVM_ISA_SSTC] = "SSTC",
    [VART_RISCV_KVM_ISA_SSAIA] = "SSAIA",
};

static const char *const sbi_names[VART_RISCV_KVM_SBI_COUNT] = {
    [VART_RISCV_KVM_SBI_V01] = "legacy-0.1",
    [VART_RISCV_KVM_SBI_TIME] = "TIME",
    [VART_RISCV_KVM_SBI_IPI] = "IPI",
    [VART_RISCV_KVM_SBI_RFENCE] = "RFENCE",
    [VART_RISCV_KVM_SBI_SRST] = "SRST",
    [VART_RISCV_KVM_SBI_HSM] = "HSM",
    [VART_RISCV_KVM_SBI_DBCN] = "DBCN",
};

static bool reg_list_contains(const struct kvm_reg_list *list, uint64_t id)
{
    unsigned int i;

    for (i = 0; i < list->n; i++) {
        if (list->reg[i] == id) {
            return true;
        }
    }
    return false;
}

static int read_feature(const VartVcpu *vcpu,
                        const struct kvm_reg_list *list,
                        VartRiscvKvmProbe *probe)
{
    unsigned long value;
    int ret;

    if (!reg_list_contains(list, probe->id)) {
        return 0;
    }
    probe->feature->available = true;
    ret = vart_vcpu_get_one_reg(vcpu, probe->id, &value);
    if (ret < 0) {
        return ret;
    }
    probe->feature->enabled = value != 0;
    return 0;
}

int vart_riscv_kvm_probe_vcpu(const VartVcpu *vcpu,
                              VartRiscvKvmCaps *caps)
{
    struct kvm_reg_list empty = { 0 };
    struct kvm_reg_list *list;
    VartRiscvKvmProbe probes[] = {
        { RISCV_REG(KVM_REG_RISCV_ISA_EXT, KVM_RISCV_ISA_EXT_I),
          &caps->isa[VART_RISCV_KVM_ISA_I] },
        { RISCV_REG(KVM_REG_RISCV_ISA_EXT, KVM_RISCV_ISA_EXT_M),
          &caps->isa[VART_RISCV_KVM_ISA_M] },
        { RISCV_REG(KVM_REG_RISCV_ISA_EXT, KVM_RISCV_ISA_EXT_A),
          &caps->isa[VART_RISCV_KVM_ISA_A] },
        { RISCV_REG(KVM_REG_RISCV_ISA_EXT, KVM_RISCV_ISA_EXT_SSTC),
          &caps->isa[VART_RISCV_KVM_ISA_SSTC] },
        { RISCV_REG(KVM_REG_RISCV_ISA_EXT, KVM_RISCV_ISA_EXT_SSAIA),
          &caps->isa[VART_RISCV_KVM_ISA_SSAIA] },
        { RISCV_REG(KVM_REG_RISCV_SBI_EXT, KVM_RISCV_SBI_EXT_V01),
          &caps->sbi[VART_RISCV_KVM_SBI_V01] },
        { RISCV_REG(KVM_REG_RISCV_SBI_EXT, KVM_RISCV_SBI_EXT_TIME),
          &caps->sbi[VART_RISCV_KVM_SBI_TIME] },
        { RISCV_REG(KVM_REG_RISCV_SBI_EXT, KVM_RISCV_SBI_EXT_IPI),
          &caps->sbi[VART_RISCV_KVM_SBI_IPI] },
        { RISCV_REG(KVM_REG_RISCV_SBI_EXT, KVM_RISCV_SBI_EXT_RFENCE),
          &caps->sbi[VART_RISCV_KVM_SBI_RFENCE] },
        { RISCV_REG(KVM_REG_RISCV_SBI_EXT, KVM_RISCV_SBI_EXT_SRST),
          &caps->sbi[VART_RISCV_KVM_SBI_SRST] },
        { RISCV_REG(KVM_REG_RISCV_SBI_EXT, KVM_RISCV_SBI_EXT_HSM),
          &caps->sbi[VART_RISCV_KVM_SBI_HSM] },
        { RISCV_REG(KVM_REG_RISCV_SBI_EXT, KVM_RISCV_SBI_EXT_DBCN),
          &caps->sbi[VART_RISCV_KVM_SBI_DBCN] },
    };
    size_t size;
    unsigned int i;
    int ret;

    if (vcpu == NULL || caps == NULL) {
        return -EINVAL;
    }
    memset(caps, 0, sizeof(*caps));

    errno = 0;
    ret = ioctl(vcpu->fd, KVM_GET_REG_LIST, &empty);
    if (ret == 0 || errno != E2BIG || empty.n == 0) {
        return ret < 0 ? -errno : -EIO;
    }
    if (empty.n > (SIZE_MAX - sizeof(*list)) / sizeof(list->reg[0])) {
        return -EOVERFLOW;
    }
    size = sizeof(*list) + empty.n * sizeof(list->reg[0]);
    list = malloc(size);
    if (list == NULL) {
        return -ENOMEM;
    }
    list->n = empty.n;
    if (ioctl(vcpu->fd, KVM_GET_REG_LIST, list) < 0) {
        ret = -errno;
        goto out;
    }
    caps->reg_list = true;
    caps->core_mode = reg_list_contains(list,
        RISCV_REG(KVM_REG_RISCV_CORE, KVM_REG_RISCV_CORE_REG(mode)));
    caps->timer_frequency = reg_list_contains(list,
        RISCV_REG(KVM_REG_RISCV_TIMER,
                  KVM_REG_RISCV_TIMER_REG(frequency)));

    for (i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        ret = read_feature(vcpu, list, &probes[i]);
        if (ret < 0) {
            goto out;
        }
    }
    ret = 0;
out:
    free(list);
    return ret;
}

const char *vart_riscv_kvm_isa_name(VartRiscvKvmIsaFeature feature)
{
    return feature >= 0 && feature < VART_RISCV_KVM_ISA_COUNT ?
           isa_names[feature] : NULL;
}

const char *vart_riscv_kvm_sbi_name(VartRiscvKvmSbiFeature feature)
{
    return feature >= 0 && feature < VART_RISCV_KVM_SBI_COUNT ?
           sbi_names[feature] : NULL;
}
