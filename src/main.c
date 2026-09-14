#include <errno.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vart/cli.h"
#include "vart/console.h"
#include "vart/event-loop.h"
#include "vart/kvm.h"
#include "vart/machine/virt-machine-loader.h"
#include "vart/riscv-kvm.h"
#include "vart/terminal.h"

typedef struct VartRunContext {
    VartVirtMachine *machine;
    VartConsole console;
    VartEventLoop event_loop;
    VartEventSource input_source;
    VartTerminal terminal;
    VartVcpuExit last_exit;
    unsigned long exit_hart;
    int console_error;
    int exit_error;
    bool system_event;
    bool input_closed;
} VartRunContext;

static const char *feature_state(const VartRiscvKvmFeature *feature)
{
    if (!feature->available) {
        return "unavailable";
    }
    return feature->enabled ? "enabled" : "disabled";
}

static int probe_kvm(void)
{
    VartKvm kvm;
    VartRiscvKvmCaps caps;
    VartVcpu vcpu;
    VartVm vm;
    unsigned int i;
    int ret;

    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        fprintf(stderr, "vart: cannot initialize KVM: %s\n",
                strerror(-ret));
        return EXIT_FAILURE;
    }
    printf("KVM API version: %d\n", kvm.api_version);
    printf("vCPU mmap size: %d bytes\n", kvm.vcpu_mmap_size);
    printf("recommended vCPUs: %d\n", kvm.recommended_vcpus);
    printf("maximum vCPUs: %d\n", kvm.max_vcpus);
    printf("user memory: %s\n", kvm.user_memory ? "yes" : "no");
    printf("one-reg API: %s\n", kvm.one_reg ? "yes" : "no");
    printf("irqfd: %s\n", kvm.irqfd ? "yes" : "no");
    printf("ioeventfd: %s\n", kvm.ioeventfd ? "yes" : "no");
    printf("immediate exit: %s\n", kvm.immediate_exit ? "yes" : "no");
    printf("MP state: %s\n", kvm.mp_state ? "yes" : "no");
    printf("RISC-V reset MP state: %s\n",
           kvm.riscv_mp_state_reset ? "yes" : "no");

    ret = vart_vm_create(&vm, &kvm);
    if (ret < 0) {
        goto out_kvm;
    }
    ret = vart_vm_check_device(&vm, KVM_DEV_TYPE_RISCV_AIA);
    printf("RISC-V AIA device: %s\n", ret == 1 ? "yes" : "no");
    if (ret < 0) {
        goto out_vm;
    }
    ret = vart_vcpu_create(&vcpu, &vm, 0);
    if (ret < 0) {
        goto out_vm;
    }
    ret = vart_riscv_kvm_probe_vcpu(&vcpu, &caps);
    if (ret < 0) {
        goto out_vcpu;
    }
    printf("RISC-V register list: %s\n", caps.reg_list ? "yes" : "no");
    printf("RISC-V core mode register: %s\n",
           caps.core_mode ? "yes" : "no");
    printf("RISC-V timer frequency register: %s\n",
           caps.timer_frequency ? "yes" : "no");
    for (i = 0; i < VART_RISCV_KVM_ISA_COUNT; i++) {
        printf("RISC-V ISA %s: %s\n", vart_riscv_kvm_isa_name(i),
               feature_state(&caps.isa[i]));
    }
    for (i = 0; i < VART_RISCV_KVM_SBI_COUNT; i++) {
        printf("RISC-V SBI %s: %s\n", vart_riscv_kvm_sbi_name(i),
               feature_state(&caps.sbi[i]));
    }
out_vcpu:
    vart_vcpu_destroy(&vcpu);
out_vm:
    vart_vm_destroy(&vm);
out_kvm:
    vart_kvm_close(&kvm);
    if (ret < 0) {
        fprintf(stderr, "vart: KVM probe failed: %s\n", strerror(-ret));
    }
    return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

static void uart_output(void *opaque, unsigned char value)
{
    VartRunContext *context = opaque;
    ssize_t count;

    do {
        count = write(STDOUT_FILENO, &value, 1);
    } while (count < 0 && errno == EINTR);
    if (count != 1 && context->console_error == 0) {
        context->console_error = count < 0 ? -errno : -EIO;
    }
}

static int stop_from_exit(VartRunContext *context, int result)
{
    int ret = vart_vm_request_shutdown_locked(&context->machine->vm);
    int wake_ret = vart_event_loop_wake(&context->event_loop);

    if (result < 0) {
        return result;
    }
    if (ret < 0) {
        return ret;
    }
    return wake_ret < 0 ? wake_ret : 1;
}

static int handle_exit(VartVcpu *vcpu, const VartVcpuExit *exit,
                       void *opaque)
{
    VartRunContext *context = opaque;
    int ret;

    vart_mutex_assert_held(&vcpu->vm->big_lock);
    context->last_exit = *exit;
    context->exit_hart = vcpu->hart_id;
    switch (exit->type) {
    case VART_VCPU_EXIT_MMIO:
    {
        bool was_full = vart_console_input_space(&context->console) == 0;
        bool receive_disabled =
            !(context->machine->uart.ier & VART_UART16550_IER_RDI);

        ret = vart_execution_handle_exit(&context->machine->execution,
                                         vcpu, exit);
        if (ret < 0) {
            context->exit_error = ret;
            return stop_from_exit(context, ret);
        }
        ret = vart_console_drain_input(&context->console);
        if (ret < 0) {
            context->console_error = ret;
            return stop_from_exit(context, ret);
        }
        if ((was_full && vart_console_input_space(&context->console) != 0) ||
            (receive_disabled &&
             (context->machine->uart.ier & VART_UART16550_IER_RDI))) {
            ret = vart_event_loop_wake(&context->event_loop);
            if (ret < 0) {
                context->console_error = ret;
                return stop_from_exit(context, ret);
            }
        }
        if (context->machine->test_device_initialized &&
            context->machine->test_device.status != VART_TEST_STATUS_NONE) {
            return stop_from_exit(context, 1);
        }
        return 0;
    }
    case VART_VCPU_EXIT_INTERRUPTED:
        return 0;
    case VART_VCPU_EXIT_SYSTEM_EVENT:
        context->system_event = true;
        return stop_from_exit(context, 1);
    case VART_VCPU_EXIT_SHUTDOWN:
        return stop_from_exit(context, 1);
    case VART_VCPU_EXIT_RISCV_SBI:
    case VART_VCPU_EXIT_UNKNOWN:
        context->exit_error = -ENOTSUP;
        return stop_from_exit(context, -ENOTSUP);
    }
    context->exit_error = -ENOTSUP;
    return stop_from_exit(context, -ENOTSUP);
}

static int console_input_event(VartEventSource *source, uint32_t events,
                               void *opaque)
{
    VartRunContext *context = opaque;
    unsigned char buffer[512];
    size_t space;
    ssize_t count;
    int ret = 0;

    if (events & VART_EVENT_ERROR) {
        return -EIO;
    }
    vart_mutex_lock(&context->machine->vm.big_lock);
    space = vart_console_input_space(&context->console);
    vart_mutex_unlock(&context->machine->vm.big_lock);
    if (space > sizeof(buffer)) {
        space = sizeof(buffer);
    }
    if (space != 0 && (events & VART_EVENT_READ)) {
        do {
            count = read(source->fd, buffer, space);
        } while (count < 0 && errno == EINTR);
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            return -errno;
        }
        if (count > 0) {
            vart_mutex_lock(&context->machine->vm.big_lock);
            count = vart_console_queue_input(&context->console, buffer,
                                              (size_t)count);
            if (count >= 0) {
                ret = vart_console_drain_input(&context->console);
            }
            vart_mutex_unlock(&context->machine->vm.big_lock);
            if (count < 0 || ret < 0) {
                return count < 0 ? (int)count : ret;
            }
        } else if (count == 0) {
            context->input_closed = true;
        }
    } else if (events & VART_EVENT_HANGUP) {
        context->input_closed = true;
    }
    if (context->input_closed) {
        return vart_event_remove(source);
    }
    return 0;
}

static int update_input_interest(VartRunContext *context)
{
    uint32_t events;

    if (context->input_closed) {
        return 0;
    }
    vart_mutex_lock(&context->machine->vm.big_lock);
    events = vart_console_input_space(&context->console) != 0 &&
             (context->machine->uart.ier & VART_UART16550_IER_RDI) ?
             VART_EVENT_READ : 0;
    vart_mutex_unlock(&context->machine->vm.big_lock);
    if (context->input_source.events == events) {
        return 0;
    }
    return vart_event_modify(&context->input_source, events);
}

static bool guest_has_stopped(VartRunContext *context)
{
    VartVirtMachine *machine = context->machine;
    bool stopped;
    size_t i;

    vart_mutex_lock(&machine->vm.big_lock);
    stopped = machine->vm.shutdown_requested;
    for (i = 0; !stopped && i < machine->vcpu_count; i++) {
        stopped = machine->vcpus[i].thread_state ==
                  VART_VCPU_THREAD_STOPPED;
    }
    if (stopped && !machine->vm.shutdown_requested) {
        vart_vm_request_shutdown_locked(&machine->vm);
    }
    vart_mutex_unlock(&machine->vm.big_lock);
    return stopped;
}

static int run_event_loop(VartRunContext *context)
{
    int ret;

    while (!guest_has_stopped(context)) {
        ret = update_input_interest(context);
        if (ret < 0) {
            return ret;
        }
        ret = vart_event_loop_run_once(&context->event_loop, 100);
        if (ret < 0 && ret != -EINTR) {
            return ret;
        }
    }
    return 0;
}

static int validate_boot_contract(VartVirtMachine *machine,
                                  uint32_t *timebase_frequency)
{
    VartRiscvBootContractReport report;
    VartRiscvKvmCaps caps;
    int ret;

    ret = vart_riscv_kvm_probe_vcpu(&machine->vcpus[0], &caps);
    if (ret == 0) {
        ret = vart_riscv_kvm_validate_boot(machine->vm.kvm, &caps, &report);
    }
    if (ret == 0) {
        ret = vart_riscv_vcpu_get_registers(&machine->vcpus[0],
                                            VART_RISCV_REG_TIMER);
    }
    if (ret == 0 && machine->vcpus[0].cpu_state.timer.frequency == 0) {
        return -EIO;
    }
    if (ret == 0) {
        *timebase_frequency =
            machine->vcpus[0].cpu_state.timer.frequency;
    }
    return ret;
}

static int run_guest(const VartCliOptions *options)
{
    static const char *const extensions[] = {
        "i", "m", "a", "f", "d", "c", "zicsr", "zifencei",
        "sstc", "ssaia",
    };
    VartVirtMachineBootFiles files = {
        .kernel_path = options->kernel_path,
        .initrd_path = options->initrd_path,
        .bootargs = options->bootargs,
        .isa = "rv64imafdc_zicsr_zifencei_sstc_ssaia",
        .isa_base = "rv64i",
        .isa_extensions = extensions,
        .isa_extension_count = sizeof(extensions) / sizeof(extensions[0]),
        .mmu_type = "riscv,sv48",
    };
    VartVirtMachineConfig config = {
        .ram_size = options->memory_size,
        .vcpu_count = options->vcpu_count,
        .enable_test_device = options->enable_test_device,
        .uart_output = uart_output,
    };
    VartRunContext context = { 0 };
    VartRiscvBootInfo boot;
    VartVirtMachine machine;
    VartKvm kvm;
    bool event_loop_initialized = false;
    bool terminal_initialized = false;
    int loop_ret;
    int restore_ret;
    int ret;

    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        fprintf(stderr, "vart: KVM initialization failed: %s\n",
                strerror(-ret));
        return EXIT_FAILURE;
    }
    context.machine = &machine;
    config.uart_output_opaque = &context;
    ret = vart_virt_machine_create(&machine, &kvm, &config);
    if (ret < 0) {
        fprintf(stderr, "vart: machine creation failed: %s\n",
                strerror(-ret));
        goto out_kvm;
    }
    ret = vart_console_init(&context.console, &machine.uart);
    if (ret < 0) {
        fprintf(stderr, "vart: console initialization failed: %s\n",
                strerror(-ret));
        goto out_terminal;
    }
    ret = vart_event_loop_init(&context.event_loop);
    if (ret < 0) {
        fprintf(stderr, "vart: event loop initialization failed: %s\n",
                strerror(-ret));
        goto out_terminal;
    }
    event_loop_initialized = true;
    ret = vart_terminal_init(&context.terminal, STDIN_FILENO);
    if (ret < 0) {
        fprintf(stderr, "vart: terminal initialization failed: %s\n",
                strerror(-ret));
        goto out_event_loop;
    }
    terminal_initialized = true;
    vart_event_source_init(&context.input_source);
    ret = vart_event_add(&context.event_loop, &context.input_source,
                         STDIN_FILENO, VART_EVENT_READ,
                         console_input_event, &context);
    if (ret < 0) {
        fprintf(stderr, "vart: console input registration failed: %s\n",
                strerror(-ret));
        goto out_terminal;
    }
    ret = validate_boot_contract(&machine, &files.timebase_frequency);
    if (ret < 0) {
        fprintf(stderr, "vart: unsupported KVM boot contract: %s\n",
                strerror(-ret));
        goto out_terminal;
    }
    ret = vart_virt_machine_load_boot_files(&machine, &files, &boot);
    if (ret < 0) {
        fprintf(stderr, "vart: boot resource loading failed: %s\n",
                strerror(-ret));
        goto out_terminal;
    }
    ret = vart_virt_machine_start(&machine, handle_exit, &context);
    if (ret < 0) {
        fprintf(stderr, "vart: vCPU startup failed: %s\n", strerror(-ret));
        goto out_terminal;
    }
    loop_ret = run_event_loop(&context);
    if (loop_ret < 0) {
        vart_vm_request_shutdown(&machine.vm);
    }
    ret = vart_virt_machine_join(&machine);
    if (ret == 0 && loop_ret < 0) {
        ret = loop_ret;
    }
    if (ret < 0) {
        fprintf(stderr, "vart: hart %lu exit %u failed: %s\n",
                context.exit_hart, context.last_exit.kvm_reason,
                strerror(-ret));
    } else if (context.console_error < 0) {
        ret = context.console_error;
        fprintf(stderr, "vart: console output failed: %s\n",
                strerror(-ret));
    } else if (machine.test_device_initialized &&
               machine.test_device.status == VART_TEST_STATUS_FAIL) {
        ret = -EIO;
        fprintf(stderr, "vart: test guest reported failure\n");
    } else if (context.system_event) {
        fprintf(stderr, "vart: guest system event %u reason %llu\n",
                context.last_exit.system_event.type,
                context.last_exit.system_event.ndata == 0 ? 0ULL :
                (unsigned long long)context.last_exit.system_event.data[0]);
    }
out_terminal:
    restore_ret = terminal_initialized ?
                  vart_terminal_restore(&context.terminal) : 0;
    if (restore_ret < 0 && ret >= 0) {
        ret = restore_ret;
        fprintf(stderr, "vart: terminal restore failed: %s\n",
                strerror(-ret));
    }
out_event_loop:
    if (event_loop_initialized) {
        vart_event_loop_destroy(&context.event_loop);
    }
    vart_virt_machine_destroy(&machine);
out_kvm:
    vart_kvm_close(&kvm);
    return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    VartCliOptions options;
    int ret;

    ret = vart_cli_parse(argc, argv, &options);
    if (ret < 0) {
        fprintf(stderr, "vart: invalid command line\n");
        vart_cli_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (options.mode == VART_CLI_HELP) {
        vart_cli_usage(argv[0]);
        return EXIT_SUCCESS;
    }
    return options.mode == VART_CLI_PROBE ? probe_kvm() :
           run_guest(&options);
}
