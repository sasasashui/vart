#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vart/machine/virt-machine.h"
#include "vart/machine/virt.h"

#define GUEST_RAM_SIZE (64 * 1024 * 1024)

typedef struct TestContext {
    VartVirtMachine *machine;
    unsigned int exits;
    unsigned char output[64];
    size_t output_size;
} TestContext;

static int read_image(const char *path, void **data, size_t *size)
{
    void *image;
    long file_size;
    FILE *file;
    int ret = 0;

    file = fopen(path, "rb");
    if (file == NULL) {
        return -errno;
    }
    if (fseek(file, 0, SEEK_END) || (file_size = ftell(file)) <= 0 ||
        fseek(file, 0, SEEK_SET)) {
        ret = -EIO;
        goto out;
    }
    image = malloc((size_t)file_size);
    if (image == NULL) {
        ret = -ENOMEM;
        goto out;
    }
    if (fread(image, (size_t)file_size, 1, file) != 1) {
        free(image);
        ret = -EIO;
        goto out;
    }
    *data = image;
    *size = (size_t)file_size;
out:
    fclose(file);
    return ret;
}

static void capture_uart(void *opaque, unsigned char value)
{
    TestContext *context = opaque;

    if (context->output_size < sizeof(context->output) - 1) {
        context->output[context->output_size++] = value;
        context->output[context->output_size] = '\0';
    }
}

static int handle_exit(VartVcpu *vcpu, const VartVcpuExit *exit,
                       void *opaque)
{
    TestContext *context = opaque;
    int ret;

    vart_mutex_assert_held(&vcpu->vm->big_lock);
    if (context->exits++ == 64) {
        return -EIO;
    }
    ret = vart_execution_handle_exit(&context->machine->execution,
                                     vcpu, exit);
    if (ret < 0) {
        return ret;
    }
    return context->machine->test_device.status ==
           VART_TEST_STATUS_NONE ? 0 : 1;
}

int main(int argc, char **argv)
{
    const VartRiscvBootInfo boot = {
        .entry = VART_VIRT_DRAM_BASE,
        .fdt_addr = VART_VIRT_DRAM_BASE + UINT64_C(0x1000),
    };
    VartVirtMachineConfig config = {
        .ram_size = GUEST_RAM_SIZE,
        .vcpu_count = 1,
        .enable_test_device = true,
    };
    VartVirtMachine machine;
    TestContext context = { .machine = &machine };
    void *image = NULL;
    size_t image_size = 0;
    VartKvm kvm;
    int ret;

    if (argc != 2) {
        return EXIT_FAILURE;
    }
    alarm(10);
    if (vart_virt_machine_create(NULL, &kvm, &config) != -EINVAL ||
        vart_virt_machine_create(&machine, NULL, &config) != -EINVAL) {
        return EXIT_FAILURE;
    }
    if ((ret = read_image(argv[1], &image, &image_size)) < 0 ||
        (ret = vart_kvm_open(&kvm)) < 0) {
        goto fail_kvm;
    }
    config.uart_output = capture_uart;
    config.uart_output_opaque = &context;
    ret = vart_virt_machine_create(&machine, &kvm, &config);
    if (ret < 0) {
        goto fail_machine;
    }
    if (!machine.initialized || machine.vcpu_count != 1 ||
        machine.system_address_space.count != 2 ||
        machine.ram.guest_addr != VART_VIRT_DRAM_BASE ||
        machine.aia.aplic_base != VART_VIRT_APLIC_S_BASE ||
        machine.uart_irq.line != VART_VIRT_UART_IRQ) {
        ret = -EIO;
        goto fail_run;
    }
    ret = vart_memory_region_write(&machine.ram, VART_VIRT_DRAM_BASE,
                                   image, image_size);
    if (ret == 0) {
        ret = vart_virt_machine_init_boot(&machine, &boot);
    }
    if (ret == 0) {
        ret = vart_virt_machine_start(&machine, handle_exit, &context);
    }
    if (ret == 0) {
        ret = vart_virt_machine_join(&machine);
    }
    if (ret < 0 || machine.test_device.status != VART_TEST_STATUS_PASS ||
        strcmp((char *)context.output, "Hello from VART UART\n") != 0) {
        ret = ret < 0 ? ret : -EIO;
        goto fail_run;
    }

    vart_virt_machine_destroy(&machine);
    config.vcpu_count = 2;
    config.enable_test_device = false;
    config.uart_output = NULL;
    config.uart_output_opaque = NULL;
    ret = vart_virt_machine_create(&machine, &kvm, &config);
    if (ret < 0 || machine.vcpu_count != 2 ||
        machine.vcpus[0].hart_id != 0 || machine.vcpus[1].hart_id != 1 ||
        machine.aia.vcpu_count != 2 ||
        machine.system_address_space.count != 1) {
        ret = ret < 0 ? ret : -EIO;
        goto fail_run;
    }
    vart_virt_machine_destroy(&machine);
    vart_kvm_close(&kvm);
    free(image);
    alarm(0);
    puts("ok - assemble and run a complete virt machine");
    return EXIT_SUCCESS;

fail_run:
    vart_virt_machine_destroy(&machine);
fail_machine:
    vart_kvm_close(&kvm);
fail_kvm:
    free(image);
    fprintf(stderr, "not ok - virt machine assembly: %d\n", ret);
    return EXIT_FAILURE;
}
