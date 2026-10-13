#include <errno.h>
#include <string.h>

#include "vart/riscv-iommu.h"

#define VART_IOMMU_PAGE_SIZE UINT64_C(0x1000)
#define VART_IOMMU_MAX_QUEUE_LOG2 8

static uint64_t load_le64(const uint8_t *data);

static void iommu_fault_handler(VartIommuContext *context, uint64_t address,
                                size_t size, bool write, int error,
                                void *opaque);

static void iommu_raise_msix(VartRiscvIommu *iommu, unsigned int vector)
{
    (void)vart_pci_device_raise_msix(&iommu->pci, vector);
}

static int iommu_read_guest64(VartRiscvIommu *iommu, uint64_t address,
                              uint64_t *value)
{
    uint8_t data[sizeof(*value)];
    int ret;

    ret = vart_pci_device_dma_read(&iommu->pci, address, data, sizeof(data));
    if (ret < 0) {
        return ret;
    }
    *value = load_le64(data);
    return 0;
}

/* Apply the one-level DDT entry for the single software-bound requester. */
static int iommu_apply_ddt(VartRiscvIommu *iommu)
{
    VartIommuContext *context = iommu->context;
    uint64_t mode = iommu->ddtp & VART_RISCV_IOMMU_DDTP_MODE_MASK;
    uint64_t base;
    uint64_t dte;
    uint64_t dc_base;
    uint64_t tc;
    uint64_t fsc;
    int ret;

    if (iommu->bound_device == NULL || context == NULL) {
        return 0;
    }
    if (mode == 0) {
        (void)vart_iommu_context_set_mode(context,
                                          VART_IOMMU_CONTEXT_FAULT);
        return -EACCES;
    }
    if (mode == 1) {
        return vart_iommu_context_set_mode(context,
                                           VART_IOMMU_CONTEXT_IDENTITY);
    }
    if (mode != 2) {
        (void)vart_iommu_context_set_mode(context,
                                          VART_IOMMU_CONTEXT_FAULT);
        return -EINVAL;
    }

    base = ((iommu->ddtp & VART_RISCV_IOMMU_DDTP_PPN_MASK) >> 10) << 12;
    if (iommu->bound_device->requester_id >= 64 ||
        (uint64_t)iommu->bound_device->requester_id >
            (UINT64_MAX - base - 8) / 64) {
        goto invalid;
    }
    ret = iommu_read_guest64(
        iommu, base + (uint64_t)iommu->bound_device->requester_id * 64,
        &dte);
    if (ret < 0 || !(dte & VART_RISCV_IOMMU_DDTE_VALID) ||
        (dte & ~(VART_RISCV_IOMMU_DDTE_PPN_MASK |
                 VART_RISCV_IOMMU_DDTE_VALID))) {
        goto invalid;
    }
    dc_base = ((dte & VART_RISCV_IOMMU_DDTE_PPN_MASK) >> 10) << 12;
    ret = iommu_read_guest64(iommu, dc_base, &tc);
    if (ret < 0 || !(tc & VART_RISCV_IOMMU_DC_TC_VALID)) {
        goto invalid;
    }
    ret = iommu_read_guest64(iommu, dc_base + 24, &fsc);
    if (ret < 0) {
        goto invalid;
    }
    switch ((fsc & VART_RISCV_IOMMU_DC_FSC_MODE_MASK) >> 60) {
    case 0:
        return vart_iommu_context_set_mode(context,
                                           VART_IOMMU_CONTEXT_IDENTITY);
    case 8:
        return vart_iommu_context_set_sv39x4(
            context,
            (fsc & VART_RISCV_IOMMU_DC_FSC_PPN_MASK) >> 10, false);
    default:
        break;
    }
invalid:
    (void)vart_iommu_context_set_mode(context, VART_IOMMU_CONTEXT_FAULT);
    return -EINVAL;
}

static uint64_t load_le64(const uint8_t *data)
{
    uint64_t value = 0;

    for (unsigned int i = 0; i < sizeof(value); i++) {
        value |= (uint64_t)data[i] << (i * 8);
    }
    return value;
}

static bool iommu_access_valid(uint64_t offset, unsigned int size,
                               uint64_t reg, unsigned int reg_size)
{
    return offset == reg && size == reg_size;
}

static int iommu_invalidate_command(VartRiscvIommu *iommu,
                                    uint64_t dword0, uint64_t dword1)
{
    uint64_t opcode = dword0 & UINT64_C(0x7f);
    uint64_t function = (dword0 >> 7) & UINT64_C(0x7);
    uint64_t address;

    if (opcode == VART_RISCV_IOMMU_CMD_IOFENCE && function == 0) {
        return 0;
    }
    if (opcode != VART_RISCV_IOMMU_CMD_IOTINVAL || function != 0 ||
        iommu->context == NULL ||
        dword0 & ((UINT64_C(1) << 32) | (UINT64_C(1) << 33))) {
        return -EINVAL;
    }
    if (dword0 & VART_RISCV_IOMMU_CMD_AV) {
        if (dword1 > UINT64_MAX / 4) {
            return -EFAULT;
        }
        address = (dword1 * 4) & ~(VART_IOMMU_PAGE_SIZE - 1);
        return vart_iommu_context_invalidate(iommu->context, address,
                                             VART_IOMMU_PAGE_SIZE);
    }
    return vart_iommu_context_invalidate(iommu->context, 0,
                                         UINT64_C(1) << 41);
}

static int iommu_process_command_queue(VartRiscvIommu *iommu)
{
    uint64_t queue_base;
    unsigned int log2sz;
    uint32_t entries;

    if (!(iommu->cqcsr & VART_RISCV_IOMMU_CQCSR_CQON)) {
        return 0;
    }
    log2sz = iommu->cqb & VART_RISCV_IOMMU_QUEUE_LOG2SZ_MASK;
    if (log2sz == 0 || log2sz > VART_IOMMU_MAX_QUEUE_LOG2) {
        iommu->cqcsr |= VART_RISCV_IOMMU_CQCSR_CQMF;
        return -EINVAL;
    }
    entries = UINT32_C(1) << log2sz;
    queue_base = ((iommu->cqb & VART_RISCV_IOMMU_QUEUE_PPN_MASK) >> 10)
                 << 12;
    while (iommu->cqh != iommu->cqt) {
        uint8_t command[16];
        uint32_t index = iommu->cqh & (entries - 1);
        uint64_t address;
        int ret;

        if ((uint64_t)index > (UINT64_MAX - queue_base - 16) / 16) {
            iommu->cqcsr |= VART_RISCV_IOMMU_CQCSR_CQMF;
            iommu_raise_msix(iommu, 1);
            return -EFAULT;
        }
        address = queue_base + (uint64_t)index * 16;
        ret = vart_pci_device_dma_read(&iommu->pci, address, command,
                                       sizeof(command));
        if (ret < 0) {
            iommu->cqcsr |= VART_RISCV_IOMMU_CQCSR_CQMF;
            iommu_raise_msix(iommu, 1);
            return ret;
        }
        ret = iommu_invalidate_command(iommu, load_le64(command),
                                       load_le64(command + 8));
        iommu->cqh++;
        if (ret < 0) {
            iommu->cqcsr |= VART_RISCV_IOMMU_CQCSR_CMD_ILL;
            iommu_raise_msix(iommu, 1);
        }
    }
    return 0;
}

static int iommu_mmio_read(void *opaque, uint64_t offset, unsigned int size,
                           uint64_t *value)
{
    VartRiscvIommu *iommu = opaque;

    if (iommu == NULL || value == NULL) {
        return -EINVAL;
    }
    pthread_mutex_lock(&iommu->lock);
    switch (offset) {
    case VART_RISCV_IOMMU_REG_CAP:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_CAP, 8)) {
            goto invalid;
        }
        *value = iommu->cap;
        break;
    case VART_RISCV_IOMMU_REG_FCTL:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_FCTL, 4)) {
            goto invalid;
        }
        *value = iommu->fctl;
        break;
    case VART_RISCV_IOMMU_REG_DDTP:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_DDTP, 8)) {
            goto invalid;
        }
        *value = iommu->ddtp;
        break;
    case VART_RISCV_IOMMU_REG_CQB:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_CQB, 8)) {
            goto invalid;
        }
        *value = iommu->cqb;
        break;
    case VART_RISCV_IOMMU_REG_CQH:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_CQH, 4)) {
            goto invalid;
        }
        *value = iommu->cqh;
        break;
    case VART_RISCV_IOMMU_REG_CQT:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_CQT, 4)) {
            goto invalid;
        }
        *value = iommu->cqt;
        break;
    case VART_RISCV_IOMMU_REG_CQCSR:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_CQCSR, 4)) {
            goto invalid;
        }
        *value = iommu->cqcsr;
        break;
    case VART_RISCV_IOMMU_REG_FQB:
        *value = iommu->fqb;
        break;
    case VART_RISCV_IOMMU_REG_FQH:
        *value = iommu->fqh;
        break;
    case VART_RISCV_IOMMU_REG_FQT:
        *value = iommu->fqt;
        break;
    case VART_RISCV_IOMMU_REG_PQB:
        *value = iommu->pqb;
        break;
    case VART_RISCV_IOMMU_REG_PQH:
        *value = iommu->pqh;
        break;
    case VART_RISCV_IOMMU_REG_PQT:
        *value = iommu->pqt;
        break;
    case VART_RISCV_IOMMU_REG_FQCSR:
        *value = iommu->fqcsr;
        break;
    case VART_RISCV_IOMMU_REG_PQCSR:
        *value = iommu->pqcsr;
        break;
    case VART_RISCV_IOMMU_REG_IPSR:
        *value = iommu->fault_status != 0 ? UINT32_C(2) : 0;
        break;
    case VART_RISCV_IOMMU_REG_FAULT_ADDR:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_FAULT_ADDR, 8)) {
            goto invalid;
        }
        *value = iommu->fault_address;
        break;
    case VART_RISCV_IOMMU_REG_FAULT_INFO:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_FAULT_INFO, 8)) {
            goto invalid;
        }
        *value = iommu->fault_info;
        break;
    case VART_RISCV_IOMMU_REG_FAULT_STATUS:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_FAULT_STATUS, 4)) {
            goto invalid;
        }
        *value = iommu->fault_status;
        break;
    default:
        goto invalid;
    }
    pthread_mutex_unlock(&iommu->lock);
    return 0;
invalid:
    pthread_mutex_unlock(&iommu->lock);
    return -EINVAL;
}

static int iommu_mmio_write(void *opaque, uint64_t offset, unsigned int size,
                            uint64_t value)
{
    VartRiscvIommu *iommu = opaque;
    int ret = 0;

    if (iommu == NULL) {
        return -EINVAL;
    }
    pthread_mutex_lock(&iommu->lock);
    switch (offset) {
    case VART_RISCV_IOMMU_REG_FCTL:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_FCTL, 4) ||
            value & ~UINT32_C(0x7)) {
            ret = -EINVAL;
            break;
        }
        iommu->fctl = (uint32_t)value;
        break;
    case VART_RISCV_IOMMU_REG_DDTP:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_DDTP, 8) ||
            (value & VART_RISCV_IOMMU_DDTP_MODE_MASK) > 4) {
            ret = -EINVAL;
            break;
        }
        iommu->ddtp = value & (VART_RISCV_IOMMU_DDTP_PPN_MASK |
                               VART_RISCV_IOMMU_DDTP_MODE_MASK);
        ret = iommu_apply_ddt(iommu);
        break;
    case VART_RISCV_IOMMU_REG_CQB:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_CQB, 8) ||
            value & ~(VART_RISCV_IOMMU_QUEUE_PPN_MASK |
                      VART_RISCV_IOMMU_QUEUE_LOG2SZ_MASK)) {
            ret = -EINVAL;
            break;
        }
        iommu->cqb = value & (VART_RISCV_IOMMU_QUEUE_PPN_MASK |
                              VART_RISCV_IOMMU_QUEUE_LOG2SZ_MASK);
        break;
    case VART_RISCV_IOMMU_REG_CQH:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_CQH, 4)) {
            ret = -EINVAL;
            break;
        }
        iommu->cqh = (uint32_t)value;
        break;
    case VART_RISCV_IOMMU_REG_CQT:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_CQT, 4)) {
            ret = -EINVAL;
            break;
        }
        iommu->cqt = (uint32_t)value;
        ret = iommu_process_command_queue(iommu);
        break;
    case VART_RISCV_IOMMU_REG_CQCSR:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_CQCSR, 4)) {
            ret = -EINVAL;
            break;
        }
        iommu->cqcsr = (iommu->cqcsr &
                        (VART_RISCV_IOMMU_CQCSR_CQMF |
                         VART_RISCV_IOMMU_CQCSR_CMD_ILL)) |
                       ((uint32_t)value & (VART_RISCV_IOMMU_CQCSR_CQEN |
                                           VART_RISCV_IOMMU_CQCSR_CIE));
        if (iommu->cqcsr & VART_RISCV_IOMMU_CQCSR_CQEN) {
            if ((iommu->cqb & VART_RISCV_IOMMU_QUEUE_LOG2SZ_MASK) == 0 ||
                (iommu->cqb & VART_RISCV_IOMMU_QUEUE_LOG2SZ_MASK) >
                    VART_IOMMU_MAX_QUEUE_LOG2) {
                iommu->cqcsr |= VART_RISCV_IOMMU_CQCSR_CQMF;
                ret = -EINVAL;
            } else {
                iommu->cqcsr |= VART_RISCV_IOMMU_CQCSR_CQON;
            }
        } else {
            iommu->cqcsr &= ~VART_RISCV_IOMMU_CQCSR_CQON;
        }
        break;
    case VART_RISCV_IOMMU_REG_FAULT_STATUS:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_FAULT_STATUS, 4)) {
            ret = -EINVAL;
            break;
        }
        iommu->fault_status &= ~((uint32_t)value &
                                 VART_RISCV_IOMMU_FAULT_STATUS_PENDING);
        break;
    default:
        ret = -EINVAL;
        break;
    }
    pthread_mutex_unlock(&iommu->lock);
    return ret;
}

static const VartMmioOps iommu_mmio_ops = {
    .read = iommu_mmio_read,
    .write = iommu_mmio_write,
};

int vart_riscv_iommu_init(VartRiscvIommu *iommu,
                          VartAddressSpace *address_space)
{
    int ret;

    if (iommu == NULL || address_space == NULL) {
        return -EINVAL;
    }
    memset(iommu, 0, sizeof(*iommu));
    ret = vart_pci_device_init(&iommu->pci, "riscv-iommu",
                               VART_RISCV_IOMMU_VENDOR_ID,
                               VART_RISCV_IOMMU_DEVICE_ID, 1, 0, 6, 8);
    if (ret < 0) {
        return ret;
    }
    ret = pthread_mutex_init(&iommu->lock, NULL);
    if (ret != 0) {
        return -ret;
    }
    iommu->lock_initialized = true;
    iommu->address_space = address_space;
    iommu->cap = VART_RISCV_IOMMU_CAP_VERSION |
                 VART_RISCV_IOMMU_CAP_SV39X4 |
                 VART_RISCV_IOMMU_CAP_PD8;
    ret = vart_pci_device_set_bar(&iommu->pci, 0,
                                  VART_RISCV_IOMMU_BAR_SIZE,
                                  VART_PCI_BAR_MEM_64, &iommu_mmio_ops, iommu);
    if (ret < 0) {
        vart_riscv_iommu_destroy(iommu);
        return ret;
    }
    ret = vart_pci_device_init_msix(&iommu->pci, 2, 0, 0x400, 0, 0x800);
    if (ret < 0) {
        vart_riscv_iommu_destroy(iommu);
        return ret;
    }
    return 0;
}

int vart_riscv_iommu_set_context(VartRiscvIommu *iommu,
                                 VartIommuContext *context)
{
    if (iommu == NULL || !iommu->lock_initialized ||
        iommu->pci.parent.realized) {
        return -EINVAL;
    }
    pthread_mutex_lock(&iommu->lock);
    if (iommu->context != NULL) {
        (void)vart_iommu_context_set_fault_handler(iommu->context, NULL,
                                                    NULL);
    }
    iommu->context = context;
    if (context != NULL) {
        int ret = vart_iommu_context_set_fault_handler(
            context, iommu_fault_handler, iommu);

        if (ret < 0) {
            iommu->context = NULL;
            pthread_mutex_unlock(&iommu->lock);
            return ret;
        }
    }
    pthread_mutex_unlock(&iommu->lock);
    return 0;
}

int vart_riscv_iommu_bind_device(VartRiscvIommu *iommu,
                                 VartPciDevice *device,
                                 VartIommuContext *context)
{
    int ret;

    if (iommu == NULL || device == NULL || context == NULL ||
        !iommu->lock_initialized || iommu->pci.parent.realized ||
        iommu->bound_device != NULL || iommu->context != context) {
        return -EINVAL;
    }
    pthread_mutex_lock(&iommu->lock);
    ret = vart_iommu_context_attach(context, device);
    if (ret == 0) {
        iommu->bound_device = device;
    }
    pthread_mutex_unlock(&iommu->lock);
    return ret;
}

int vart_riscv_iommu_unbind_device(VartRiscvIommu *iommu,
                                   VartPciDevice *device)
{
    int ret;

    if (iommu == NULL || device == NULL || !iommu->lock_initialized ||
        iommu->bound_device != device || iommu->context == NULL) {
        return -EINVAL;
    }
    pthread_mutex_lock(&iommu->lock);
    ret = vart_iommu_context_detach(iommu->context, device);
    if (ret == 0) {
        iommu->bound_device = NULL;
    }
    pthread_mutex_unlock(&iommu->lock);
    return ret;
}

int vart_riscv_iommu_set_msix_injector(VartRiscvIommu *iommu,
                                       VartPciMsixInject inject,
                                       void *opaque)
{
    if (iommu == NULL || !iommu->lock_initialized) {
        return -EINVAL;
    }
    return vart_pci_device_set_msix_injector(&iommu->pci, inject, opaque);
}

int vart_riscv_iommu_reset(VartRiscvIommu *iommu)
{
    if (iommu == NULL || !iommu->lock_initialized) {
        return -EINVAL;
    }
    pthread_mutex_lock(&iommu->lock);
    iommu->fctl = 0;
    iommu->ddtp = 0;
    iommu->cqb = 0;
    iommu->cqh = 0;
    iommu->cqt = 0;
    iommu->cqcsr = 0;
    iommu->fqb = 0;
    iommu->fqh = 0;
    iommu->fqt = 0;
    iommu->pqb = 0;
    iommu->pqh = 0;
    iommu->pqt = 0;
    iommu->fqcsr = 0;
    iommu->pqcsr = 0;
    iommu->fault_address = 0;
    iommu->fault_info = 0;
    iommu->fault_status = 0;
    if (iommu->bound_device != NULL && iommu->context != NULL) {
        (void)vart_iommu_context_set_mode(iommu->context,
                                          VART_IOMMU_CONTEXT_FAULT);
    }
    pthread_mutex_unlock(&iommu->lock);
    return 0;
}

int vart_riscv_iommu_destroy(VartRiscvIommu *iommu)
{
    if (iommu == NULL || !iommu->lock_initialized ||
        iommu->pci.parent.realized || iommu->pci.pci_bus != NULL ||
        iommu->bound_device != NULL) {
        return -EINVAL;
    }
    if (iommu->context != NULL) {
        (void)vart_iommu_context_set_fault_handler(iommu->context, NULL,
                                                    NULL);
    }
    pthread_mutex_destroy(&iommu->lock);
    memset(iommu, 0, sizeof(*iommu));
    return 0;
}

static void iommu_fault_handler(VartIommuContext *context, uint64_t address,
                                size_t size, bool write, int error,
                                void *opaque)
{
    VartRiscvIommu *iommu = opaque;

    (void)context;
    pthread_mutex_lock(&iommu->lock);
    iommu->fault_address = address;
    iommu->fault_info = (write ? VART_RISCV_IOMMU_FAULT_INFO_WRITE : 0) |
                        ((uint64_t)(uint32_t)(-error) << 8) |
                        ((uint64_t)size << 32);
    iommu->fault_status |= VART_RISCV_IOMMU_FAULT_STATUS_PENDING;
    iommu_raise_msix(iommu, 0);
    pthread_mutex_unlock(&iommu->lock);
}
