#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define OUTPUT_CAPACITY (2U * 1024U * 1024U)

typedef struct PtyGuest {
    struct termios original_termios;
    char *output;
    size_t output_length;
    pid_t pid;
    int master_fd;
    int slave_fd;
    int original_flags;
} PtyGuest;

static long long monotonic_ms(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        return -1;
    }
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int append_output(PtyGuest *guest)
{
    ssize_t count;

    if (guest->output_length == OUTPUT_CAPACITY - 1) {
        return -ENOSPC;
    }
    do {
        count = read(guest->master_fd,
                     guest->output + guest->output_length,
                     OUTPUT_CAPACITY - guest->output_length - 1);
    } while (count < 0 && errno == EINTR);
    if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        return errno == EIO ? 0 : -errno;
    }
    if (count > 0) {
        guest->output_length += count;
        guest->output[guest->output_length] = '\0';
    }
    return 0;
}

static int wait_for_output(PtyGuest *guest, const char *marker, int timeout_ms)
{
    struct pollfd pollfd = {
        .fd = guest->master_fd,
        .events = POLLIN,
    };
    long long deadline = monotonic_ms() + timeout_ms;
    long long now;
    int ret;

    while (strstr(guest->output, marker) == NULL) {
        now = monotonic_ms();
        if (now < 0 || now >= deadline) {
            return -ETIMEDOUT;
        }
        ret = poll(&pollfd, 1, (int)(deadline - now));
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        if (ret == 0) {
            return -ETIMEDOUT;
        }
        ret = append_output(guest);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

static int spawn_guest(PtyGuest *guest, const char *vart,
                       const char *kernel, const char *initrd)
{
    pid_t pid;

    memset(guest, 0, sizeof(*guest));
    guest->master_fd = -1;
    guest->slave_fd = -1;
    guest->output = calloc(OUTPUT_CAPACITY, 1);
    if (guest->output == NULL) {
        return -ENOMEM;
    }
    if (openpty(&guest->master_fd, &guest->slave_fd, NULL, NULL, NULL) < 0 ||
        tcgetattr(guest->slave_fd, &guest->original_termios) < 0) {
        return -errno;
    }
    guest->original_flags = fcntl(guest->slave_fd, F_GETFL);
    if (guest->original_flags < 0 ||
        fcntl(guest->master_fd, F_SETFL, O_NONBLOCK) < 0) {
        return -errno;
    }
    pid = fork();
    if (pid < 0) {
        return -errno;
    }
    if (pid == 0) {
        close(guest->master_fd);
        /* VINTR targets VART only after the slave becomes its controlling TTY. */
        if (setsid() < 0 ||
            ioctl(guest->slave_fd, TIOCSCTTY, 0) < 0 ||
            dup2(guest->slave_fd, STDIN_FILENO) < 0 ||
            dup2(guest->slave_fd, STDOUT_FILENO) < 0 ||
            dup2(guest->slave_fd, STDERR_FILENO) < 0) {
            _exit(127);
        }
        if (guest->slave_fd > STDERR_FILENO) {
            close(guest->slave_fd);
        }
        execl(vart, vart, "--kernel", kernel, "--initrd", initrd,
              "--memory", "512M", "--cpus", "1", "--append",
              "earlycon=uart8250,mmio,0x10000000 console=ttyS0",
              (char *)NULL);
        _exit(127);
    }
    guest->pid = pid;
    return 0;
}

static int terminal_is_raw(PtyGuest *guest)
{
    struct termios current;

    /* The parent and fd 0 share the slave's file description across fork. */
    if (tcgetattr(guest->slave_fd, &current) < 0) {
        return 0;
    }
    return !(current.c_lflag & (ICANON | ECHO)) &&
           (current.c_lflag & ISIG) ==
           (guest->original_termios.c_lflag & ISIG) &&
           (fcntl(guest->slave_fd, F_GETFL) & O_NONBLOCK);
}

static int terminal_is_restored(PtyGuest *guest)
{
    struct termios current;

    return tcgetattr(guest->slave_fd, &current) == 0 &&
           current.c_iflag == guest->original_termios.c_iflag &&
           current.c_oflag == guest->original_termios.c_oflag &&
           current.c_cflag == guest->original_termios.c_cflag &&
           current.c_lflag == guest->original_termios.c_lflag &&
           memcmp(current.c_cc, guest->original_termios.c_cc, NCCS) == 0 &&
           fcntl(guest->slave_fd, F_GETFL) == guest->original_flags;
}

static int wait_for_child(PtyGuest *guest, int expected_status)
{
    long long deadline = monotonic_ms() + 5000;
    int status;
    pid_t ret;

    for (;;) {
        ret = waitpid(guest->pid, &status, WNOHANG);
        if (ret == guest->pid) {
            guest->pid = -1;
            append_output(guest);
            return WIFEXITED(status) &&
                   WEXITSTATUS(status) == expected_status ? 0 : -ECHILD;
        }
        if (ret < 0) {
            return -errno;
        }
        if (monotonic_ms() >= deadline) {
            return -ETIMEDOUT;
        }
        append_output(guest);
        usleep(10000);
    }
}

static void destroy_guest(PtyGuest *guest)
{
    if (guest->pid > 0) {
        kill(guest->pid, SIGKILL);
        waitpid(guest->pid, NULL, 0);
    }
    if (guest->slave_fd >= 0) {
        close(guest->slave_fd);
    }
    if (guest->master_fd >= 0) {
        close(guest->master_fd);
    }
    free(guest->output);
}

static int check_shell(const char *vart, const char *kernel,
                       const char *initrd)
{
    static const char command[] =
        "printf 'VART_PTY_OK\\n'; poweroff -f\n";
    PtyGuest guest;
    int ret = -1;

    if (spawn_guest(&guest, vart, kernel, initrd) < 0 ||
        wait_for_output(&guest, "~ # ", 10000) < 0 ||
        !terminal_is_raw(&guest) ||
        write(guest.master_fd, command, sizeof(command) - 1) !=
        (ssize_t)sizeof(command) - 1 ||
        wait_for_output(&guest, "VART_PTY_OK", 5000) < 0 ||
        wait_for_output(&guest, "reboot: Power down", 5000) < 0 ||
        wait_for_child(&guest, 0) < 0 || !terminal_is_restored(&guest)) {
        goto out;
    }
    ret = 0;
out:
    if (ret < 0 && guest.output != NULL) {
        fputs(guest.output, stderr);
    }
    destroy_guest(&guest);
    return ret;
}

static int check_ctrl_c(const char *vart, const char *kernel,
                        const char *initrd)
{
    PtyGuest guest;
    char interrupt = 3;
    int ret = -1;

    if (spawn_guest(&guest, vart, kernel, initrd) < 0 ||
        wait_for_output(&guest, "~ # ", 10000) < 0 ||
        !terminal_is_raw(&guest) ||
        write(guest.master_fd, &interrupt, 1) != 1 ||
        wait_for_child(&guest, 130) < 0 ||
        !terminal_is_restored(&guest) ||
        strstr(guest.output, "vart: terminated by signal 2") == NULL) {
        goto out;
    }
    ret = 0;
out:
    if (ret < 0 && guest.output != NULL) {
        fputs(guest.output, stderr);
    }
    destroy_guest(&guest);
    return ret;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s VART IMAGE INITRAMFS\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (check_shell(argv[1], argv[2], argv[3]) < 0 ||
        check_ctrl_c(argv[1], argv[2], argv[3]) < 0) {
        return EXIT_FAILURE;
    }
    puts("ok - run Linux interactively through a pseudo-terminal");
    return EXIT_SUCCESS;
}
