#define _GNU_SOURCE

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vart/machine/virt-machine-loader.h"
#include "vart/machine/virt.h"

#define GUEST_RAM_SIZE (64 * 1024 * 1024)

typedef struct TestContext {
    VartVirtMachine *machine;
    unsigned int exits;
    char output[64];
    size_t output_size;
} TestContext;

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
    static const char *const extensions[] = {
        "i", "m", "a", "ssaia",
    };
    static const unsigned char initrd_data[] = {
        0x56, 0x41, 0x52, 0x54,
    };
    VartVirtMachineBootFiles files = {
        .bootargs = "console=ttyS0",
        .timebase_frequency = 10000000,
        .isa = "rv64ima_ssaia",
        .isa_base = "rv64i",
        .isa_extensions = extensions,
        .isa_extension_count = sizeof(extensions) / sizeof(extensions[0]),
        .mmu_type = "riscv,sv48",
    };
    VartVirtMachineConfig machine_config = {
        .ram_size = GUEST_RAM_SIZE,
        .vcpu_count = 1,
        .enable_test_device = true,
        .uart_output = capture_uart,
    };
    char initrd_path[] = "/tmp/vart-initrd-XXXXXX";
    VartRiscvBootInfo boot = { .entry = UINT64_MAX };
    VartVirtMachine machine;
    TestContext context = { .machine = &machine };
    VartKvm kvm;
    int initrd_fd = -1;
    int ret;

    if (argc != 2) {
        return EXIT_FAILURE;
    }
    alarm(10);
    if ((ret = vart_kvm_open(&kvm)) < 0) {
        goto fail_kvm;
    }
    machine_config.uart_output_opaque = &context;
    ret = vart_virt_machine_create(&machine, &kvm, &machine_config);
    if (ret < 0) {
        goto fail_machine;
    }
    initrd_fd = mkstemp(initrd_path);
    if (initrd_fd < 0) {
        ret = -errno;
        goto fail_run;
    }

    files.kernel_path = "/tmp/vart-kernel-does-not-exist";
    if (vart_virt_machine_load_boot_files(&machine, &files, &boot) !=
            -ENOENT || boot.entry != UINT64_MAX) {
        ret = -EIO;
        goto fail_run;
    }
    files.kernel_path = argv[1];
    ret = vart_virt_machine_load_boot_files(&machine, &files, &boot);
    if (ret < 0 || boot.entry != VART_VIRT_DRAM_BASE) {
        ret = ret < 0 ? ret : -EIO;
        goto fail_run;
    }
    boot.entry = UINT64_MAX;
    files.initrd_path = initrd_path;
    if (vart_virt_machine_load_boot_files(&machine, &files, &boot) !=
            -EINVAL || boot.entry != UINT64_MAX ||
        write(initrd_fd, initrd_data, sizeof(initrd_data)) !=
            (ssize_t)sizeof(initrd_data) || close(initrd_fd) < 0) {
        ret = -EIO;
        goto fail_run;
    }
    initrd_fd = -1;
    ret = vart_virt_machine_load_boot_files(&machine, &files, &boot);
    if (ret < 0 || boot.entry != VART_VIRT_DRAM_BASE ||
        memcmp((char *)machine.ram.host_addr + UINT64_C(0x02000000),
               initrd_data, sizeof(initrd_data)) != 0) {
        ret = ret < 0 ? ret : -EIO;
        goto fail_run;
    }
    ret = vart_virt_machine_start(&machine, handle_exit, &context);
    if (ret == 0) {
        ret = vart_virt_machine_join(&machine);
    }
    if (ret < 0 || machine.test_device.status != VART_TEST_STATUS_PASS ||
        strcmp(context.output, "Hello from VART UART\n") != 0) {
        ret = ret < 0 ? ret : -EIO;
        goto fail_run;
    }

    unlink(initrd_path);
    vart_virt_machine_destroy(&machine);
    vart_kvm_close(&kvm);
    alarm(0);
    puts("ok - load machine boot files and run the guest");
    return EXIT_SUCCESS;

fail_run:
    if (initrd_fd >= 0) {
        close(initrd_fd);
    }
    unlink(initrd_path);
    vart_virt_machine_destroy(&machine);
fail_machine:
    vart_kvm_close(&kvm);
fail_kvm:
    fprintf(stderr, "not ok - machine boot file loading: %d\n", ret);
    return EXIT_FAILURE;
}
