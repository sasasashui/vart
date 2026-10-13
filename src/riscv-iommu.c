#include <errno.h>
#include <string.h>

#include "vart/riscv-iommu.h"

#define VART_IOMMU_PAGE_SIZE UINT64_C(0x1000)
#define VART_IOMMU_MAX_QUEUE_LOG2 13

static uint64_t load_le64(const uint8_t *data);
static void store_le64(uint8_t *data, uint64_t value);

static void iommu_fault_handler(VartIommuContext *context, uint64_t address,
                                size_t size, bool write, int error,
                                void *opaque);
static int iommu_remap_msi(VartPciDevice *device, unsigned int vector,
                           uint64_t *address, uint32_t *data, void *opaque);

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

static int iommu_enqueue_fault(VartRiscvIommu *iommu,
                               VartIommuContext *context, uint64_t address,
                               size_t size, bool write, int error)
{
    uint64_t queue_base;
    unsigned int log2sz;
    uint32_t entries;
    uint32_t mask;
    uint32_t head;
    uint32_t tail;
    uint32_t next;
    uint64_t record_address;
    uint8_t record[VART_RISCV_IOMMU_FQ_RECORD_SIZE] = { 0 };

    if (!(iommu->fqcsr & VART_RISCV_IOMMU_FQCSR_FQON)) {
        return 0;
    }
    log2sz = iommu->fqb & VART_RISCV_IOMMU_QUEUE_LOG2SZ_MASK;
    if (log2sz == 0 || log2sz > VART_IOMMU_MAX_QUEUE_LOG2) {
        iommu->fqcsr |= VART_RISCV_IOMMU_FQCSR_FQMF;
        return -EINVAL;
    }
    entries = UINT32_C(1) << log2sz;
    mask = entries - 1;
    head = iommu->fqh & mask;
    tail = iommu->fqt & mask;
    next = (tail + 1) & mask;
    if (next == head) {
        iommu->fqcsr |= VART_RISCV_IOMMU_FQCSR_FQOF;
        return -ENOSPC;
    }
    queue_base = ((iommu->fqb & VART_RISCV_IOMMU_QUEUE_PPN_MASK) >> 10)
                 << 12;
    if ((uint64_t)tail > (UINT64_MAX - queue_base -
                          VART_RISCV_IOMMU_FQ_RECORD_SIZE) /
                             VART_RISCV_IOMMU_FQ_RECORD_SIZE) {
        iommu->fqcsr |= VART_RISCV_IOMMU_FQCSR_FQMF;
        return -EFAULT;
    }
    record_address = queue_base + (uint64_t)tail *
                     VART_RISCV_IOMMU_FQ_RECORD_SIZE;
    store_le64(record, (write ? VART_RISCV_IOMMU_FQ_CAUSE_WRITE_FAULT :
                            VART_RISCV_IOMMU_FQ_CAUSE_READ_FAULT) |
                       ((uint64_t)(context != NULL ? context->requester_id : 0)
                        << VART_RISCV_IOMMU_FQ_HDR_DID_SHIFT));
    store_le64(record + 16, address);
    store_le64(record + 24, (uint64_t)(uint32_t)(-error) |
                             ((uint64_t)size << 32));
    if (vart_pci_device_dma_write(&iommu->pci, record_address, record,
                                  sizeof(record)) < 0) {
        iommu->fqcsr |= VART_RISCV_IOMMU_FQCSR_FQMF;
        return -EFAULT;
    }
    iommu->fqt = next;
    return 0;
}

/* Resolve one requester through the DDT and load its device context. */
static int iommu_apply_ddt_binding(VartRiscvIommu *iommu,
                                   VartPciDevice *device,
                                   VartIommuContext *context)
{
    uint64_t mode = iommu->ddtp & VART_RISCV_IOMMU_DDTP_MODE_MASK;
    uint64_t base;
    uint64_t dte = 0;
    uint64_t dc_base;
    uint64_t tc = 0;
    uint64_t fsc = 0;
    int ret;
    bool base_format = !(iommu->cap & VART_RISCV_IOMMU_CAP_MSI_FLAT);
    size_t dc_size = base_format ? 32 : 64;
    unsigned int ddi_bits;

    if (device == NULL || context == NULL) {
        return 0;
    }
    if (mode == 0) {
        (void)vart_iommu_context_set_mode(context,
                                          VART_IOMMU_CONTEXT_FAULT);
        return 0;
    }
    if (mode == 1) {
        return vart_iommu_context_set_mode(context,
                                           VART_IOMMU_CONTEXT_IDENTITY);
    }
    if (mode < 2 || mode > 4) {
        (void)vart_iommu_context_set_mode(context,
                                          VART_IOMMU_CONTEXT_FAULT);
        return -EINVAL;
    }

    base = ((iommu->ddtp & VART_RISCV_IOMMU_DDTP_PPN_MASK) >> 10) << 12;
    ddi_bits = (unsigned int)(mode - 2) * 9 + 6;
    if (base_format && mode != 4) {
        ddi_bits++;
    }
    if (ddi_bits < 16 &&
        device->requester_id >= (UINT16_C(1) << ddi_bits)) {
        goto invalid;
    }
    for (unsigned int depth = (unsigned int)mode - 2; depth > 0; depth--) {
        unsigned int split = (depth - 1) * 9 + 6 + base_format;
        uint64_t index = ((uint64_t)device->requester_id >> split) << 3;

        if (index > UINT64_MAX - base || index > 0xff8) {
            goto invalid;
        }
        ret = iommu_read_guest64(iommu, base + index, &dte);
        if (ret < 0 || !(dte & VART_RISCV_IOMMU_DDTE_VALID) ||
            (dte & ~(VART_RISCV_IOMMU_DDTE_PPN_MASK |
                     VART_RISCV_IOMMU_DDTE_VALID))) {
            goto invalid;
        }
        base = ((dte & VART_RISCV_IOMMU_DDTE_PPN_MASK) >> 10) << 12;
    }
    if ((uint64_t)device->requester_id >
        (UINT64_MAX - base) / dc_size) {
        goto invalid;
    }
    dc_base = base + ((uint64_t)device->requester_id * dc_size &
                      UINT64_C(0xfff));
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

static int iommu_apply_ddt(VartRiscvIommu *iommu)
{
    int ret = 0;

    for (size_t i = 0; i < iommu->binding_count; i++) {
        int current = iommu_apply_ddt_binding(iommu,
                                               iommu->bindings[i].device,
                                               iommu->bindings[i].context);

        if (current < 0 && ret == 0) {
            ret = current;
        }
    }
    return ret;
}

static bool iommu_context_is_bound(const VartRiscvIommu *iommu,
                                   const VartIommuContext *context)
{
    for (size_t i = 0; i < iommu->binding_count; i++) {
        if (iommu->bindings[i].context == context) {
            return true;
        }
    }
    return false;
}

static int iommu_remap_msi(VartPciDevice *device, unsigned int vector,
                           uint64_t *address, uint32_t *data, void *opaque)
{
    VartRiscvIommu *iommu = opaque;
    VartIommuContext *context = NULL;
    int ret;

    (void)device;
    (void)vector;
    (void)data;
    if (iommu == NULL || address == NULL || !iommu->lock_initialized) {
        return -EINVAL;
    }
    pthread_mutex_lock(&iommu->lock);
    if (!iommu->msi_remapping_enabled) {
        pthread_mutex_unlock(&iommu->lock);
        return 0;
    }
    for (size_t i = 0; i < iommu->msi_mapping_count; i++) {
        VartRiscvIommuMsiMapping *mapping = &iommu->msi_mappings[i];

        if (*address >= mapping->iova &&
            *address - mapping->iova <= mapping->size - sizeof(uint32_t)) {
            *address = mapping->target + (*address - mapping->iova);
            pthread_mutex_unlock(&iommu->lock);
            return 0;
        }
    }
    for (size_t i = 0; i < iommu->binding_count; i++) {
        if (iommu->bindings[i].device == device) {
            context = iommu->bindings[i].context;
            break;
        }
    }
    pthread_mutex_unlock(&iommu->lock);
    if (context == NULL || context->mode != VART_IOMMU_CONTEXT_SV39X4) {
        return -EACCES;
    }
    ret = vart_iommu_context_translate_address(context, *address, 4, true,
                                               address);
    return ret < 0 ? -EACCES : 0;
}

static uint64_t load_le64(const uint8_t *data)
{
    uint64_t value = 0;

    for (unsigned int i = 0; i < sizeof(value); i++) {
        value |= (uint64_t)data[i] << (i * 8);
    }
    return value;
}

static void store_le64(uint8_t *data, uint64_t value)
{
    for (unsigned int i = 0; i < sizeof(value); i++) {
        data[i] = value >> (i * 8);
    }
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

    if (opcode == VART_RISCV_IOMMU_CMD_IODIR && function == 0) {
        uint64_t requester_id;

        if (!(dword0 & VART_RISCV_IOMMU_CMD_IODIR_DV)) {
            for (size_t i = 0; i < iommu->binding_count; i++) {
                (void)iommu_apply_ddt_binding(
                    iommu, iommu->bindings[i].device,
                    iommu->bindings[i].context);
            }
            return 0;
        }
        requester_id = (dword0 >> VART_RISCV_IOMMU_CMD_IODIR_DID_SHIFT) &
                       UINT64_C(0xffffff);
        for (size_t i = 0; i < iommu->binding_count; i++) {
            if (iommu->bindings[i].device->requester_id == requester_id) {
                (void)iommu_apply_ddt_binding(
                    iommu, iommu->bindings[i].device,
                    iommu->bindings[i].context);
                return 0;
            }
        }
        return 0;
    }

    if (opcode == VART_RISCV_IOMMU_CMD_IOFENCE && function == 0) {
        return 0;
    }
    if (opcode != VART_RISCV_IOMMU_CMD_IOTINVAL || function != 0 ||
        (iommu->binding_count == 0 && iommu->context == NULL) ||
        dword0 & ((UINT64_C(1) << 32) | (UINT64_C(1) << 33))) {
        return -EINVAL;
    }
    if (dword0 & VART_RISCV_IOMMU_CMD_AV) {
        if (dword1 > UINT64_MAX / 4) {
            return -EFAULT;
        }
        address = (dword1 * 4) & ~(VART_IOMMU_PAGE_SIZE - 1);
    } else {
        address = 0;
    }
    int ret = 0;

    if (iommu->binding_count == 0) {
        return vart_iommu_context_invalidate(
            iommu->context, address,
            dword0 & VART_RISCV_IOMMU_CMD_AV ? VART_IOMMU_PAGE_SIZE :
                                                UINT64_C(1) << 41);
    }
    for (size_t i = 0; i < iommu->binding_count; i++) {
        int current = vart_iommu_context_invalidate(
            iommu->bindings[i].context, address,
            dword0 & VART_RISCV_IOMMU_CMD_AV ? VART_IOMMU_PAGE_SIZE :
                                                UINT64_C(1) << 41);

        if (current < 0 && ret == 0) {
            ret = current;
        }
    }
    return ret;
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
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_FQB, 8)) {
            goto invalid;
        }
        *value = iommu->fqb;
        break;
    case VART_RISCV_IOMMU_REG_FQH:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_FQH, 4)) {
            goto invalid;
        }
        *value = iommu->fqh;
        break;
    case VART_RISCV_IOMMU_REG_FQT:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_FQT, 4)) {
            goto invalid;
        }
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
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_FQCSR, 4)) {
            goto invalid;
        }
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
    case VART_RISCV_IOMMU_REG_ICVEC:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_ICVEC, 8)) {
            goto invalid;
        }
        *value = iommu->icvec;
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
        (void)iommu_apply_ddt(iommu);
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
    case VART_RISCV_IOMMU_REG_FQB:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_FQB, 8) ||
            value & ~(VART_RISCV_IOMMU_QUEUE_PPN_MASK |
                      VART_RISCV_IOMMU_QUEUE_LOG2SZ_MASK)) {
            ret = -EINVAL;
            break;
        }
        iommu->fqb = value & (VART_RISCV_IOMMU_QUEUE_PPN_MASK |
                              VART_RISCV_IOMMU_QUEUE_LOG2SZ_MASK);
        break;
    case VART_RISCV_IOMMU_REG_FQH:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_FQH, 4)) {
            ret = -EINVAL;
            break;
        }
        iommu->fqh = (uint32_t)value;
        break;
    case VART_RISCV_IOMMU_REG_FQCSR:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_FQCSR, 4)) {
            ret = -EINVAL;
            break;
        }
        iommu->fqcsr &= ~((uint32_t)value &
                          (VART_RISCV_IOMMU_FQCSR_FQMF |
                           VART_RISCV_IOMMU_FQCSR_FQOF));
        iommu->fqcsr = (iommu->fqcsr &
                        (VART_RISCV_IOMMU_FQCSR_FQMF |
                         VART_RISCV_IOMMU_FQCSR_FQOF)) |
                       ((uint32_t)value & (VART_RISCV_IOMMU_FQCSR_FQEN |
                                           VART_RISCV_IOMMU_FQCSR_FIE));
        if (iommu->fqcsr & VART_RISCV_IOMMU_FQCSR_FQEN) {
            if ((iommu->fqb & VART_RISCV_IOMMU_QUEUE_LOG2SZ_MASK) == 0 ||
                (iommu->fqb & VART_RISCV_IOMMU_QUEUE_LOG2SZ_MASK) >
                    VART_IOMMU_MAX_QUEUE_LOG2) {
                iommu->fqcsr |= VART_RISCV_IOMMU_FQCSR_FQMF;
                ret = -EINVAL;
            } else {
                iommu->fqcsr |= VART_RISCV_IOMMU_FQCSR_FQON;
            }
        } else {
            iommu->fqcsr &= ~VART_RISCV_IOMMU_FQCSR_FQON;
        }
        break;
    case VART_RISCV_IOMMU_REG_PQCSR:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_PQCSR, 4)) {
            ret = -EINVAL;
            break;
        }
        iommu->pqcsr = (uint32_t)value;
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
    case VART_RISCV_IOMMU_REG_IPSR:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_IPSR, 4)) {
            ret = -EINVAL;
            break;
        }
        if (value & UINT32_C(2)) {
            iommu->fault_status = 0;
        }
        break;
    case VART_RISCV_IOMMU_REG_ICVEC:
        if (!iommu_access_valid(offset, size,
                                VART_RISCV_IOMMU_REG_ICVEC, 8) ||
            value & ~UINT64_C(0xffff)) {
            ret = -EINVAL;
            break;
        }
        iommu->icvec = value;
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
    if (iommu->context != NULL &&
        !iommu_context_is_bound(iommu, iommu->context)) {
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
        !iommu->lock_initialized ||
        iommu->binding_count >= VART_RISCV_IOMMU_MAX_BINDINGS) {
        return -EINVAL;
    }
    pthread_mutex_lock(&iommu->lock);
    for (size_t i = 0; i < iommu->binding_count; i++) {
        if (iommu->bindings[i].device == device ||
            iommu->bindings[i].context == context ||
            iommu->bindings[i].device->requester_id == device->requester_id) {
            pthread_mutex_unlock(&iommu->lock);
            return -EEXIST;
        }
    }
    ret = vart_iommu_context_attach(context, device);
    if (ret == 0) {
        ret = vart_iommu_context_set_fault_handler(context,
                                                   iommu_fault_handler, iommu);
    }
    if (ret == 0 && device->msix.configured) {
        ret = vart_pci_device_set_msix_remapper(device, iommu_remap_msi,
                                                 iommu);
    }
    if (ret == 0) {
        iommu->bindings[iommu->binding_count++] = (VartRiscvIommuBinding){
            .device = device,
            .context = context,
        };
    } else if (context->attached_device == device) {
        if (device->msix.configured) {
            (void)vart_pci_device_set_msix_remapper(device, NULL, NULL);
        }
        (void)vart_iommu_context_set_fault_handler(context, NULL, NULL);
        (void)vart_iommu_context_detach(context, device);
    }
    pthread_mutex_unlock(&iommu->lock);
    return ret;
}

int vart_riscv_iommu_unbind_device(VartRiscvIommu *iommu,
                                   VartPciDevice *device)
{
    int ret;
    size_t index;

    if (iommu == NULL || device == NULL || !iommu->lock_initialized) {
        return -EINVAL;
    }
    pthread_mutex_lock(&iommu->lock);
    for (index = 0; index < iommu->binding_count; index++) {
        if (iommu->bindings[index].device == device) {
            break;
        }
    }
    if (index == iommu->binding_count) {
        pthread_mutex_unlock(&iommu->lock);
        return -ENOENT;
    }
    ret = vart_iommu_context_detach(iommu->bindings[index].context, device);
    if (ret == 0) {
        if (device->msix.configured) {
            (void)vart_pci_device_set_msix_remapper(device, NULL, NULL);
        }
        (void)vart_iommu_context_set_fault_handler(
            iommu->bindings[index].context, NULL, NULL);
        iommu->bindings[index] =
            iommu->bindings[iommu->binding_count - 1];
        iommu->binding_count--;
    }
    pthread_mutex_unlock(&iommu->lock);
    return ret;
}

int vart_riscv_iommu_set_msi_remapping(VartRiscvIommu *iommu, bool enabled)
{
    if (iommu == NULL || !iommu->lock_initialized) {
        return -EINVAL;
    }
    pthread_mutex_lock(&iommu->lock);
    iommu->msi_remapping_enabled = enabled;
    pthread_mutex_unlock(&iommu->lock);
    return 0;
}

int vart_riscv_iommu_map_msi(VartRiscvIommu *iommu, uint64_t iova,
                             uint64_t target, uint64_t size)
{
    if (iommu == NULL || !iommu->lock_initialized || size < sizeof(uint32_t) ||
        (size & (VART_IOMMU_PAGE_SIZE - 1)) != 0 ||
        (iova & (VART_IOMMU_PAGE_SIZE - 1)) != 0 ||
        (target & (VART_IOMMU_PAGE_SIZE - 1)) != 0 ||
        iova > UINT64_MAX - (size - 1) ||
        target > UINT64_MAX - (size - 1)) {
        return -EINVAL;
    }
    pthread_mutex_lock(&iommu->lock);
    for (size_t i = 0; i < iommu->msi_mapping_count; i++) {
        VartRiscvIommuMsiMapping *mapping = &iommu->msi_mappings[i];
        bool overlap = iova < mapping->iova + mapping->size &&
                       mapping->iova < iova + size;

        if (overlap) {
            if (mapping->iova == iova && mapping->size == size) {
                mapping->target = target;
                pthread_mutex_unlock(&iommu->lock);
                return 0;
            }
            pthread_mutex_unlock(&iommu->lock);
            return -EEXIST;
        }
    }
    if (iommu->msi_mapping_count >= VART_RISCV_IOMMU_MAX_MSI_MAPPINGS) {
        pthread_mutex_unlock(&iommu->lock);
        return -ENOSPC;
    }
    iommu->msi_mappings[iommu->msi_mapping_count++] =
        (VartRiscvIommuMsiMapping){
            .iova = iova,
            .target = target,
            .size = size,
        };
    pthread_mutex_unlock(&iommu->lock);
    return 0;
}

int vart_riscv_iommu_unmap_msi(VartRiscvIommu *iommu, uint64_t iova,
                               uint64_t size)
{
    if (iommu == NULL || !iommu->lock_initialized || size == 0) {
        return -EINVAL;
    }
    pthread_mutex_lock(&iommu->lock);
    for (size_t i = 0; i < iommu->msi_mapping_count; i++) {
        if (iommu->msi_mappings[i].iova == iova &&
            iommu->msi_mappings[i].size == size) {
            iommu->msi_mappings[i] =
                iommu->msi_mappings[iommu->msi_mapping_count - 1];
            iommu->msi_mapping_count--;
            pthread_mutex_unlock(&iommu->lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&iommu->lock);
    return -ENOENT;
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
    for (size_t i = 0; i < iommu->binding_count; i++) {
        (void)vart_iommu_context_set_mode(iommu->bindings[i].context,
                                          VART_IOMMU_CONTEXT_FAULT);
    }
    if (iommu->binding_count == 0 && iommu->context != NULL) {
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
        iommu->binding_count != 0) {
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
    (void)iommu_enqueue_fault(iommu, context, address, size, write, error);
    iommu_raise_msix(iommu, 0);
    pthread_mutex_unlock(&iommu->lock);
}
