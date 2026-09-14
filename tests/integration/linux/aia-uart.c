#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vart/console.h"
#include "vart/kvm.h"
#include "vart/machine/virt-machine-loader.h"
#include "vart/machine/virt.h"
#include "vart/riscv-kvm.h"

#define GUEST_RAM_SIZE (512 * 1024 * 1024UL)
#define DEFAULT_RUNS 1
#define DEFAULT_VCPUS 1

typedef struct LinuxContext {
    VartVirtMachine *machine;
    char output[64 * 1024];
    size_t output_size;
    char input[1024];
    size_t input_offset;
    VartConsole console;
    bool console_ready;
    bool system_event;
} LinuxContext;

static void capture_uart(void *opaque, unsigned char value)
{
    LinuxContext *context = opaque;

    if (context->output_size < sizeof(context->output) - 1) {
        context->output[context->output_size++] = value;
        context->output[context->output_size] = '\0';
        if (strstr(context->output, "~ # ")) {
            context->console_ready = true;
        }
    }
}

static int feed_uart(LinuxContext *context)
{
    ssize_t queued;
    int ret;

    if (!context->console_ready) {
        return 0;
    }
    if (context->input[context->input_offset] != 0) {
        queued = vart_console_queue_input(
            &context->console, &context->input[context->input_offset],
            strlen(&context->input[context->input_offset]));
        if (queued < 0 && queued != -EAGAIN) {
            return (int)queued;
        }
        if (queued > 0) {
            context->input_offset += (size_t)queued;
        }
    }
    ret = vart_console_drain_input(&context->console);
    return ret < 0 ? ret : 0;
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

static int count_open_fds(void)
{
    struct dirent *entry;
    DIR *directory;
    int count = 0;

    directory = opendir("/proc/self/fd");
    if (directory == NULL) {
        return -errno;
    }
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0) {
            count++;
        }
    }
    closedir(directory);
    return count;
}

static int run_linux(VartKvm *kvm, const char *kernel, const char *initrd,
                     size_t vcpu_count, const char *version)
{
    VartVirtMachineConfig config = {
        .ram_size = GUEST_RAM_SIZE,
        .vcpu_count = vcpu_count,
        .uart_output = capture_uart,
    };
    LinuxContext context = { 0 };
    char smp_marker[64];
    VartVirtMachine machine;
    int fds_before;
    int fds_after;
    int input_length;
    int ret;

    fds_before = count_open_fds();
    if (fds_before < 0) {
        return fds_before;
    }
    input_length = snprintf(
        context.input, sizeof(context.input),
        "printf 'VART_%%s_OK\\n' UART_RX; "
        "test \"$$\" -eq 1 && printf 'VART_%%s_OK\\n' INIT_PID1; "
        "grep -q 'proc /proc proc' /proc/mounts && "
        "printf 'VART_%%s_OK\\n' PROC_MOUNT; "
        "grep -q 'sysfs /sys sysfs' /proc/mounts && "
        "printf 'VART_%%s_OK\\n' SYS_MOUNT; "
        "grep -q 'devtmpfs /dev devtmpfs' /proc/mounts && "
        "printf 'VART_%%s_OK\\n' DEV_MOUNT; "
        "test \"$(grep -c '^processor' /proc/cpuinfo)\" -eq %zu "
        "&& printf 'VART_%%s_OK\\n' CPU_COUNT; "
        "printf 'VART_%%s_OK\\n' SHELL; poweroff -f\n",
        vcpu_count);
    if (input_length < 0 || input_length >= (int)sizeof(context.input) ||
        snprintf(smp_marker, sizeof(smp_marker),
                 "smp: Brought up 1 node, %zu CPUs", vcpu_count) >=
            (int)sizeof(smp_marker)) {
        return -E2BIG;
    }
    context.machine = &machine;
    config.uart_output_opaque = &context;
    ret = vart_virt_machine_create(&machine, kvm, &config);
    if (ret == 0) {
        ret = vart_console_init(&context.console, &machine.uart);
    }
    if (ret == 0) {
        ret = prepare_boot(&machine, kernel, initrd);
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
         vart_console_pending_input(&context.console) != 0 ||
         strstr(context.output, "VART_UART_RX_OK") == NULL ||
         strstr(context.output, "VART_INIT_PID1_OK") == NULL ||
         strstr(context.output, "VART_PROC_MOUNT_OK") == NULL ||
         strstr(context.output, "VART_SYS_MOUNT_OK") == NULL ||
         strstr(context.output, "VART_DEV_MOUNT_OK") == NULL ||
         strstr(context.output, "VART_CPU_COUNT_OK") == NULL ||
         strstr(context.output, "VART_SHELL_OK") == NULL ||
         strstr(context.output, "~ # ") == NULL ||
         strstr(context.output, "riscv-aplic") == NULL ||
         strstr(context.output, "10000000.serial: ttyS0") == NULL ||
         (version != NULL && strstr(context.output, version) == NULL) ||
         (vcpu_count > 1 &&
          strstr(context.output, smp_marker) == NULL))) {
        ret = -EIO;
    }
    if (ret < 0) {
        fwrite(context.output, context.output_size, 1, stderr);
    }
    vart_virt_machine_destroy(&machine);
    fds_after = count_open_fds();
    if (ret >= 0 && (fds_after < 0 || fds_after != fds_before ||
                     machine.initialized || machine.vcpus != NULL ||
                     machine.vm.fd != -1 || machine.aia.device.fd != -1)) {
        ret = fds_after < 0 ? fds_after : -EIO;
    }
    return ret;
}

static int check_load_failure_cleanup(VartKvm *kvm, const char *kernel)
{
    VartVirtMachineConfig config = {
        .ram_size = GUEST_RAM_SIZE,
        .vcpu_count = 1,
    };
    VartVirtMachine machine;
    int fds_before;
    int fds_after;
    int ret;

    fds_before = count_open_fds();
    if (fds_before < 0) {
        return fds_before;
    }
    ret = vart_virt_machine_create(&machine, kvm, &config);
    if (ret == 0) {
        ret = prepare_boot(&machine, kernel,
                           "/proc/self/vart-missing-initramfs");
    }
    vart_virt_machine_destroy(&machine);
    fds_after = count_open_fds();
    if (ret != -ENOENT || fds_after < 0 || fds_after != fds_before ||
        machine.initialized || machine.vcpus != NULL ||
        machine.vm.fd != -1 || machine.aia.device.fd != -1) {
        return fds_after < 0 ? fds_after : -EIO;
    }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned long runs = DEFAULT_RUNS;
    unsigned long vcpus = DEFAULT_VCPUS;
    const char *version = NULL;
    VartKvm kvm;
    char *end = NULL;
    unsigned long i;
    int ret;

    if (argc < 3 || argc > 6) {
        return EXIT_FAILURE;
    }
    if (argc >= 5) {
        errno = 0;
        vcpus = strtoul(argv[4], &end, 10);
        if (errno != 0 || end == argv[4] || *end != '\0' ||
            vcpus == 0 || vcpus > VART_VIRT_MAX_CPUS) {
            return EXIT_FAILURE;
        }
    }
    if (argc == 6) {
        version = argv[5];
    }
    if (argc >= 4) {
        errno = 0;
        runs = strtoul(argv[3], &end, 10);
        if (errno != 0 || end == argv[3] || *end != '\0' ||
            runs == 0 || runs > 100) {
            return EXIT_FAILURE;
        }
    }
    alarm(20 * runs);
    ret = vart_kvm_open(&kvm);
    if (ret == 0) {
        ret = check_load_failure_cleanup(&kvm, argv[1]);
    }
    for (i = 0; ret == 0 && i < runs; i++) {
        ret = run_linux(&kvm, argv[1], argv[2], vcpus, version);
        if (ret < 0) {
            break;
        }
    }
    vart_kvm_close(&kvm);
    alarm(0);
    if (ret < 0) {
        fprintf(stderr, "not ok - Linux boot iteration %lu: %s\n",
                i + 1, strerror(-ret));
        return EXIT_FAILURE;
    }
    printf("ok - boot Linux with %lu vCPU%s and clean up %lu time%s\n",
           vcpus, vcpus == 1 ? "" : "s", runs, runs == 1 ? "" : "s");
    return EXIT_SUCCESS;
}
