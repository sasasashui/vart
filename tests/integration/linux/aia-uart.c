#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vart/kvm.h"
#include "vart/machine/virt-machine-loader.h"
#include "vart/riscv-kvm.h"

#define GUEST_RAM_SIZE (512 * 1024 * 1024UL)

typedef struct LinuxContext {
    VartVirtMachine *machine;
    char output[64 * 1024];
    size_t output_size;
    const char *input;
    size_t input_offset;
    bool console_ready;
    bool system_event;
} LinuxContext;

static void capture_uart(void *opaque, unsigned char value)
{
    LinuxContext *context = opaque;

    if (context->output_size < sizeof(context->output) - 1) {
        context->output[context->output_size++] = value;
        context->output[context->output_size] = '\0';
        if (strstr(context->output, "RISC-V Linux debug environment")) {
            context->console_ready = true;
        }
    }
}

static int feed_uart(LinuxContext *context)
{
    VartUart16550 *uart = &context->machine->uart;

    if (!context->console_ready || context->input[context->input_offset] == 0 ||
        !(uart->ier & 1) || (uart->lsr & VART_UART16550_LSR_DR)) {
        return 0;
    }
    return vart_uart16550_receive(
        uart, (unsigned char)context->input[context->input_offset++]);
}

static int stop_machine(LinuxContext *context, int result)
{
    int ret = vart_vm_request_shutdown_locked(&context->machine->vm);

    return result < 0 ? result : ret < 0 ? ret : 1;
}

static int handle_exit(VartVcpu *vcpu, const VartVcpuExit *exit,
                       void *opaque)
{
    LinuxContext *context = opaque;
    int ret;

    vart_mutex_assert_held(&vcpu->vm->big_lock);
    switch (exit->type) {
    case VART_VCPU_EXIT_MMIO:
        ret = vart_execution_handle_exit(&context->machine->execution,
                                         vcpu, exit);
        if (ret == 0) {
            ret = feed_uart(context);
        }
        return ret < 0 ? stop_machine(context, ret) : 0;
    case VART_VCPU_EXIT_INTERRUPTED:
        return 0;
    case VART_VCPU_EXIT_SYSTEM_EVENT:
        context->system_event = true;
        return stop_machine(context, 1);
    case VART_VCPU_EXIT_SHUTDOWN:
        return stop_machine(context, 1);
    case VART_VCPU_EXIT_RISCV_SBI:
    case VART_VCPU_EXIT_UNKNOWN:
        return stop_machine(context, -ENOTSUP);
    }
    return stop_machine(context, -ENOTSUP);
}

static int prepare_boot(VartVirtMachine *machine, const char *kernel,
                        const char *initrd)
{
    static const char *const extensions[] = {
        "i", "m", "a", "f", "d", "c", "zicsr", "zifencei",
        "sstc", "ssaia",
    };
    VartVirtMachineBootFiles files = {
        .kernel_path = kernel,
        .initrd_path = initrd,
        .bootargs = "earlycon=uart8250,mmio,0x10000000 console=ttyS0",
        .isa = "rv64imafdc_zicsr_zifencei_sstc_ssaia",
        .isa_base = "rv64i",
        .isa_extensions = extensions,
        .isa_extension_count = sizeof(extensions) / sizeof(extensions[0]),
        .mmu_type = "riscv,sv48",
    };
    VartRiscvBootContractReport report;
    VartRiscvKvmCaps caps;
    VartRiscvBootInfo boot;
    int ret;

    ret = vart_riscv_kvm_probe_vcpu(&machine->vcpus[0], &caps);
    if (ret == 0) {
        ret = vart_riscv_kvm_validate_boot(machine->vm.kvm, &caps, &report);
    }
    if (ret == 0) {
        ret = vart_riscv_vcpu_get_registers(&machine->vcpus[0],
                                            VART_RISCV_REG_TIMER);
    }
    if (ret == 0) {
        files.timebase_frequency =
            machine->vcpus[0].cpu_state.timer.frequency;
        if (files.timebase_frequency == 0) {
            ret = -EIO;
        }
    }
    if (ret == 0) {
        ret = vart_virt_machine_load_boot_files(machine, &files, &boot);
    }
    return ret;
}

int main(int argc, char **argv)
{
    VartVirtMachineConfig config = {
        .ram_size = GUEST_RAM_SIZE,
        .vcpu_count = 1,
        .uart_output = capture_uart,
    };
    LinuxContext context = {
        .input = "echo VART_UART_RX_OK; poweroff -f\n",
    };
    VartVirtMachine machine;
    VartKvm kvm;
    int ret;

    if (argc != 3) {
        return EXIT_FAILURE;
    }
    alarm(20);
    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        return EXIT_FAILURE;
    }
    context.machine = &machine;
    config.uart_output_opaque = &context;
    ret = vart_virt_machine_create(&machine, &kvm, &config);
    if (ret == 0) {
        ret = prepare_boot(&machine, argv[1], argv[2]);
    }
    if (ret == 0) {
        ret = vart_virt_machine_start(&machine, handle_exit, &context);
    }
    if (ret == 0) {
        ret = vart_virt_machine_join(&machine);
    }
    if (ret == 0 &&
        (!context.console_ready || !context.system_event ||
         context.input[context.input_offset] != 0 ||
         strstr(context.output, "VART_UART_RX_OK") == NULL ||
         strstr(context.output, "riscv-aplic") == NULL ||
         strstr(context.output, "ttyS0 at MMIO 0x10000000") == NULL)) {
        ret = -EIO;
    }
    if (ret < 0) {
        fwrite(context.output, context.output_size, 1, stderr);
    }
    vart_virt_machine_destroy(&machine);
    vart_kvm_close(&kvm);
    alarm(0);
    if (ret < 0) {
        fprintf(stderr, "not ok - Linux AIA and UART: %s\n",
                strerror(-ret));
        return EXIT_FAILURE;
    }
    puts("ok - deliver Linux UART input through KVM AIA");
    return EXIT_SUCCESS;
}
