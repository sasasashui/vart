#ifndef VART_RISCV_IOMMU_H
#define VART_RISCV_IOMMU_H

#include <pthread.h>
#include <stdint.h>

#include "vart/iommu.h"
#include "vart/pci.h"

#define VART_RISCV_IOMMU_VENDOR_ID UINT16_C(0x1b36)
#define VART_RISCV_IOMMU_DEVICE_ID UINT16_C(0x0014)
#define VART_RISCV_IOMMU_BAR_SIZE UINT64_C(0x1000)

#define VART_RISCV_IOMMU_REG_CAP UINT64_C(0x0000)
#define VART_RISCV_IOMMU_REG_FCTL UINT64_C(0x0008)
#define VART_RISCV_IOMMU_REG_DDTP UINT64_C(0x0010)
#define VART_RISCV_IOMMU_REG_CQB UINT64_C(0x0018)
#define VART_RISCV_IOMMU_REG_CQH UINT64_C(0x0020)
#define VART_RISCV_IOMMU_REG_CQT UINT64_C(0x0024)
#define VART_RISCV_IOMMU_REG_FQB UINT64_C(0x0028)
#define VART_RISCV_IOMMU_REG_FQH UINT64_C(0x0030)
#define VART_RISCV_IOMMU_REG_FQT UINT64_C(0x0034)
#define VART_RISCV_IOMMU_REG_PQB UINT64_C(0x0038)
#define VART_RISCV_IOMMU_REG_PQH UINT64_C(0x0040)
#define VART_RISCV_IOMMU_REG_PQT UINT64_C(0x0044)
#define VART_RISCV_IOMMU_REG_CQCSR UINT64_C(0x0048)
#define VART_RISCV_IOMMU_REG_FQCSR UINT64_C(0x004c)
#define VART_RISCV_IOMMU_REG_PQCSR UINT64_C(0x0050)
#define VART_RISCV_IOMMU_REG_IPSR UINT64_C(0x0054)
#define VART_RISCV_IOMMU_REG_FAULT_ADDR UINT64_C(0x0200)
#define VART_RISCV_IOMMU_REG_FAULT_INFO UINT64_C(0x0208)
#define VART_RISCV_IOMMU_REG_FAULT_STATUS UINT64_C(0x0210)

#define VART_RISCV_IOMMU_CAP_VERSION UINT64_C(0x10)
#define VART_RISCV_IOMMU_CAP_SV39X4 (UINT64_C(1) << 17)
#define VART_RISCV_IOMMU_CAP_PD8 (UINT64_C(1) << 38)
#define VART_RISCV_IOMMU_DDTP_MODE_MASK UINT64_C(0xf)
#define VART_RISCV_IOMMU_DDTP_PPN_MASK UINT64_C(0x003fffffffffc00)
#define VART_RISCV_IOMMU_DDTE_VALID UINT64_C(1)
#define VART_RISCV_IOMMU_DDTE_PPN_MASK UINT64_C(0x003fffffffffc00)
#define VART_RISCV_IOMMU_DC_TC_VALID UINT64_C(1)
#define VART_RISCV_IOMMU_DC_FSC_MODE_MASK UINT64_C(0xf000000000000000)
#define VART_RISCV_IOMMU_DC_FSC_PPN_MASK UINT64_C(0x00000ffffffffc00)
#define VART_RISCV_IOMMU_QUEUE_LOG2SZ_MASK UINT64_C(0x1f)
#define VART_RISCV_IOMMU_QUEUE_PPN_MASK UINT64_C(0x003fffffffffc00)
#define VART_RISCV_IOMMU_CQCSR_CQEN UINT32_C(1)
#define VART_RISCV_IOMMU_CQCSR_CIE UINT32_C(2)
#define VART_RISCV_IOMMU_CQCSR_CQMF (UINT32_C(1) << 8)
#define VART_RISCV_IOMMU_CQCSR_CMD_ILL (UINT32_C(1) << 10)
#define VART_RISCV_IOMMU_CQCSR_CQON (UINT32_C(1) << 16)
#define VART_RISCV_IOMMU_CMD_IOTINVAL UINT64_C(1)
#define VART_RISCV_IOMMU_CMD_IOFENCE UINT64_C(2)
#define VART_RISCV_IOMMU_CMD_AV (UINT64_C(1) << 10)
#define VART_RISCV_IOMMU_FAULT_STATUS_PENDING UINT32_C(1)
#define VART_RISCV_IOMMU_FAULT_INFO_WRITE UINT64_C(1)
#define VART_RISCV_IOMMU_MAX_BINDINGS 32

typedef struct VartRiscvIommuBinding {
    VartPciDevice *device;
    VartIommuContext *context;
} VartRiscvIommuBinding;

typedef struct VartRiscvIommu {
    VartPciDevice pci;
    VartAddressSpace *address_space;
    /* Kept as the default context for command and fault compatibility. */
    VartIommuContext *context;
    VartRiscvIommuBinding bindings[VART_RISCV_IOMMU_MAX_BINDINGS];
    size_t binding_count;
    uint64_t cap;
    uint32_t fctl;
    uint64_t ddtp;
    uint64_t cqb;
    uint32_t cqh;
    uint32_t cqt;
    uint64_t fqb;
    uint32_t fqh;
    uint32_t fqt;
    uint64_t pqb;
    uint32_t pqh;
    uint32_t pqt;
    uint32_t cqcsr;
    uint32_t fqcsr;
    uint32_t pqcsr;
    uint64_t fault_address;
    uint64_t fault_info;
    uint32_t fault_status;
    pthread_mutex_t lock;
    bool lock_initialized;
} VartRiscvIommu;

int vart_riscv_iommu_init(VartRiscvIommu *iommu,
                          VartAddressSpace *address_space);
int vart_riscv_iommu_set_context(VartRiscvIommu *iommu,
                                 VartIommuContext *context);
int vart_riscv_iommu_bind_device(VartRiscvIommu *iommu,
                                 VartPciDevice *device,
                                 VartIommuContext *context);
int vart_riscv_iommu_unbind_device(VartRiscvIommu *iommu,
                                   VartPciDevice *device);
int vart_riscv_iommu_set_msix_injector(VartRiscvIommu *iommu,
                                       VartPciMsixInject inject,
                                       void *opaque);
int vart_riscv_iommu_reset(VartRiscvIommu *iommu);
int vart_riscv_iommu_destroy(VartRiscvIommu *iommu);

#endif
