#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "vart/terminal.h"

static int check_pipe(void)
{
    VartTerminal terminal;
    int pipes[2];
    int original_flags;
    char byte;
    int ret = -1;

    if (pipe(pipes) < 0) {
        return -errno;
    }
    original_flags = fcntl(pipes[0], F_GETFL);
    if (original_flags < 0 ||
        vart_terminal_init(&terminal, pipes[0]) < 0 ||
        terminal.is_tty || !terminal.active ||
        !(fcntl(pipes[0], F_GETFL) & O_NONBLOCK) ||
        read(pipes[0], &byte, 1) != -1 || errno != EAGAIN ||
        vart_terminal_restore(&terminal) < 0 || terminal.active ||
        fcntl(pipes[0], F_GETFL) != original_flags ||
        vart_terminal_restore(&terminal) < 0) {
        goto out;
    }
    ret = 0;
out:
    close(pipes[1]);
    close(pipes[0]);
    return ret;
}

static int check_pty(void)
{
    struct termios original;
    struct termios current;
    VartTerminal terminal;
    const char *slave_name;
    int original_flags;
    int master = -1;
    int slave = -1;
    char byte;
    int ret = -1;

    master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master < 0 || grantpt(master) < 0 || unlockpt(master) < 0 ||
        (slave_name = ptsname(master)) == NULL) {
        goto out;
    }
    slave = open(slave_name, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (slave < 0 || tcgetattr(slave, &original) < 0 ||
        (original_flags = fcntl(slave, F_GETFL)) < 0 ||
        vart_terminal_init(&terminal, slave) < 0 || !terminal.is_tty ||
        !terminal.active || tcgetattr(slave, &current) < 0 ||
        (current.c_lflag & (ICANON | ECHO)) != 0 ||
        (current.c_lflag & ISIG) != (original.c_lflag & ISIG) ||
        (current.c_iflag & (IXON | ICRNL)) != 0 ||
        current.c_cc[VMIN] != 1 || current.c_cc[VTIME] != 0 ||
        !(fcntl(slave, F_GETFL) & O_NONBLOCK) ||
        write(master, "x", 1) != 1 || read(slave, &byte, 1) != 1 ||
        byte != 'x' || vart_terminal_restore(&terminal) < 0 ||
        terminal.active || tcgetattr(slave, &current) < 0 ||
        current.c_iflag != original.c_iflag ||
        current.c_oflag != original.c_oflag ||
        current.c_cflag != original.c_cflag ||
        current.c_lflag != original.c_lflag ||
        memcmp(current.c_cc, original.c_cc, NCCS) != 0 ||
        fcntl(slave, F_GETFL) != original_flags) {
        goto out;
    }
    ret = 0;
out:
    if (slave >= 0) {
        close(slave);
    }
    if (master >= 0) {
        close(master);
    }
    return ret;
}

int main(void)
{
    VartTerminal terminal;
    int fd;

    fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return EXIT_FAILURE;
    }
    close(fd);
    if (vart_terminal_init(NULL, STDIN_FILENO) != -EINVAL ||
        vart_terminal_init(&terminal, -1) != -EINVAL ||
        vart_terminal_init(&terminal, fd) != -EBADF ||
        vart_terminal_restore(NULL) != -EINVAL ||
        check_pipe() < 0 || check_pty() < 0) {
        return EXIT_FAILURE;
    }
    puts("ok - configure and restore host terminal state");
    return EXIT_SUCCESS;
}
