#include <asm/kvm.h>
#include <errno.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "vart/address-space.h"
#include "vart/devices/test-device.h"
#include "vart/devices/uart16550.h"
#include "vart/exec.h"
#include "vart/kvm.h"
#include "vart/machine/virt.h"
#include "vart/memory.h"
#include "vart/riscv-aia.h"
#include "vart/riscv-cpu.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

#define GUEST_RAM_SIZE (16 * 1024 * 1024)

typedef struct TestContext {
    VartExecution execution;
    VartTestDevice test;
    VartUart16550 uart;
    bool injected;
    unsigned int exits;
} TestContext;

static int load_guest(VartMemoryRegion *memory, const char *path)
{
    unsigned char *image = NULL;
    long size;
    FILE *file;
    int ret = 0;

    file = fopen(path, "rb");
    if (file == NULL) {
        return -errno;
    }
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET)) {
        ret = -EIO;
        goto out;
    }
    image = malloc((size_t)size);
    if (image == NULL) {
        ret = -ENOMEM;
        goto out;
    }
    if (fread(image, (size_t)size, 1, file) != 1) {
        ret = -EIO;
        goto out;
    }
    ret = vart_memory_region_write(memory, VART_VIRT_DRAM_BASE, image,
                                   (size_t)size);
out:
    free(image);
    fclose(file);
    return ret;
}

static int handle_exit(VartVcpu *vcpu, const VartVcpuExit *exit,
                       void *opaque)
{
    TestContext *context = opaque;
    int ret;

    vart_mutex_assert_held(&vcpu->vm->big_lock);
    if (context->exits++ == 12 || exit->type != VART_VCPU_EXIT_MMIO) {
        return -EIO;
    }
    ret = vart_execution_handle_exit(&context->execution, vcpu, exit);
    if (ret < 0) {
        return ret;
    }
    if (!context->injected && (context->uart.ier & 1)) {
        ret = vart_uart16550_receive(&context->uart, 'V');
        if (ret < 0) {
            return ret;
        }
        context->injected = true;
    }
    return context->test.status == VART_TEST_STATUS_NONE ? 0 : 1;
}

int main(int argc, char **argv)
{
    const VartRiscvBootInfo boot = {
        .entry = VART_VIRT_DRAM_BASE,
        .fdt_addr = VART_VIRT_DRAM_BASE + UINT64_C(0x1000),
    };
    VartAddressSpace address_space;
    VartMemoryRegion memory;
    TestContext context = { 0 };
    VartRiscvAia aia;
    VartIrq irq;
    VartVcpu vcpu;
    VartKvm kvm;
    VartVm vm;
    int ret;

    if (argc != 2) {
        return EXIT_FAILURE;
    }
    alarm(10);
    if ((ret = vart_kvm_open(&kvm)) < 0) {
        return EXIT_FAILURE;
    }
    if ((ret = vart_vm_create(&vm, &kvm)) < 0) {
        goto fail_vm;
    }
    if ((ret = vart_memory_region_create(&memory, VART_VIRT_DRAM_BASE,
                                         GUEST_RAM_SIZE, 0)) < 0) {
        goto fail_memory_create;
    }
    if ((ret = load_guest(&memory, argv[1])) < 0 ||
        (ret = vart_memory_region_register(&memory, &vm)) < 0) {
        goto fail_memory;
    }
    if ((ret = vart_vcpu_create(&vcpu, &vm, 0)) < 0) {
        goto fail_registered;
    }
    if ((ret = vart_riscv_aia_create(&aia, &vm,
                                     KVM_DEV_RISCV_AIA_MODE_AUTO)) < 0) {
        goto fail_aia_create;
    }
    if ((ret = vart_riscv_aia_init_aplic(&aia, 1,
                                         VART_VIRT_IMSIC_S_BASE,
                                         VART_VIRT_IMSIC_NUM_IDS,
                                         VART_VIRT_APLIC_S_BASE,
                                         VART_VIRT_APLIC_NUM_SOURCES)) < 0 ||
        (ret = vart_riscv_aia_connect_irq(&aia, &irq,
                                          VART_VIRT_UART_IRQ)) < 0 ||
        (ret = vart_riscv_vcpu_init_boot(&vcpu, &boot)) < 0) {
        goto fail_aia;
    }

    vart_address_space_init(&address_space);
    ret = vart_test_device_init(&context.test, VART_VIRT_TEST_BASE,
                                NULL, NULL);
    if (ret == 0) {
        ret = vart_uart16550_init(&context.uart, VART_VIRT_UART_BASE,
                                  NULL, NULL);
    }
    if (ret == 0) {
        ret = vart_uart16550_connect_irq(&context.uart, &irq);
    }
    if (ret == 0) {
        ret = vart_address_space_add(&address_space, &context.test.region);
    }
    if (ret == 0) {
        ret = vart_address_space_add(&address_space, &context.uart.region);
    }
    if (ret < 0) {
        goto fail_address_space;
    }
    vart_execution_init(&context.execution, &address_space);
    ret = vart_vcpu_start(&vcpu, handle_exit, &context);
    if (ret == 0) {
        ret = vart_vcpu_join(&vcpu);
    }
    if (ret < 0 || context.test.status != VART_TEST_STATUS_PASS ||
        !context.injected || irq.level) {
        ret = ret < 0 ? ret : -EIO;
        goto fail_address_space;
    }

    vart_address_space_destroy(&address_space);
    vart_riscv_aia_destroy(&aia);
    vart_vcpu_destroy(&vcpu);
    vart_memory_region_unregister(&memory, &vm);
    vart_memory_region_destroy(&memory);
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    alarm(0);
    puts("ok - deliver a UART receive interrupt through KVM AIA");
    return EXIT_SUCCESS;

fail_address_space:
    vart_address_space_destroy(&address_space);
fail_aia:
    vart_riscv_aia_destroy(&aia);
fail_aia_create:
    vart_vcpu_destroy(&vcpu);
fail_registered:
    vart_memory_region_unregister(&memory, &vm);
fail_memory:
    vart_memory_region_destroy(&memory);
fail_memory_create:
    vart_vm_destroy(&vm);
fail_vm:
    vart_kvm_close(&kvm);
    fprintf(stderr, "not ok - UART receive interrupt: %d\n", ret);
    return EXIT_FAILURE;
}
