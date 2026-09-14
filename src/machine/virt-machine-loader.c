#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vart/machine/virt-loader.h"
#include "vart/machine/virt-machine-loader.h"

static int read_boot_file(const char *path, size_t limit,
                          void **data, size_t *size)
{
    struct stat statbuf;
    unsigned char *buffer;
    size_t done = 0;
    size_t length;
    int fd;
    int ret;

    if (path == NULL || data == NULL || size == NULL || limit == 0) {
        return -EINVAL;
    }
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }
    if (fstat(fd, &statbuf) < 0) {
        ret = -errno;
        goto out_fd;
    }
    if (!S_ISREG(statbuf.st_mode) || statbuf.st_size <= 0) {
        ret = -EINVAL;
        goto out_fd;
    }
    if ((uintmax_t)statbuf.st_size > SIZE_MAX ||
        (uintmax_t)statbuf.st_size > limit) {
        ret = -EFBIG;
        goto out_fd;
    }
    length = (size_t)statbuf.st_size;
    buffer = malloc(length);
    if (buffer == NULL) {
        ret = -ENOMEM;
        goto out_fd;
    }
    while (done < length) {
        ssize_t count = read(fd, buffer + done, length - done);

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            ret = -errno;
            goto out_buffer;
        }
        if (count == 0) {
            ret = -EIO;
            goto out_buffer;
        }
        done += (size_t)count;
    }
    close(fd);
    *data = buffer;
    *size = length;
    return 0;

out_buffer:
    free(buffer);
out_fd:
    close(fd);
    return ret;
}

int vart_virt_machine_load_boot_files(
    VartVirtMachine *machine, const VartVirtMachineBootFiles *files,
    VartRiscvBootInfo *boot)
{
    VartVirtBootConfig config;
    VartVirtFdtCpu *cpus = NULL;
    void *initrd = NULL;
    void *kernel = NULL;
    size_t initrd_size = 0;
    size_t kernel_size = 0;
    size_t i;
    int ret;

    if (machine == NULL || !machine->initialized || files == NULL ||
        boot == NULL || files->kernel_path == NULL ||
        files->timebase_frequency == 0 || files->isa == NULL ||
        files->isa_base == NULL || files->isa_extensions == NULL ||
        files->isa_extension_count == 0 || files->mmu_type == NULL) {
        return -EINVAL;
    }
    ret = read_boot_file(files->kernel_path, machine->ram.size,
                         &kernel, &kernel_size);
    if (ret < 0) {
        return ret;
    }
    if (files->initrd_path != NULL) {
        ret = read_boot_file(files->initrd_path, machine->ram.size,
                             &initrd, &initrd_size);
        if (ret < 0) {
            goto out;
        }
    }
    cpus = calloc(machine->vcpu_count, sizeof(*cpus));
    if (cpus == NULL) {
        ret = -ENOMEM;
        goto out;
    }
    for (i = 0; i < machine->vcpu_count; i++) {
        cpus[i] = (VartVirtFdtCpu) {
            .hartid = machine->vcpus[i].hart_id,
            .isa = files->isa,
            .isa_base = files->isa_base,
            .isa_extensions = files->isa_extensions,
            .isa_extension_count = files->isa_extension_count,
            .mmu_type = files->mmu_type,
        };
    }
    config = (VartVirtBootConfig) {
        .kernel = kernel,
        .kernel_size = kernel_size,
        .initrd = initrd,
        .initrd_size = initrd_size,
        .cpus = cpus,
        .cpu_count = machine->vcpu_count,
        .timebase_frequency = files->timebase_frequency,
        .bootargs = files->bootargs,
    };
    ret = vart_virt_boot_load(&machine->ram, &config, boot);
    if (ret == 0) {
        ret = vart_virt_machine_init_boot(machine, boot);
    }
out:
    free(cpus);
    free(initrd);
    free(kernel);
    return ret;
}
