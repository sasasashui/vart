#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "vart/terminal.h"

int vart_terminal_init(VartTerminal *terminal, int fd)
{
    struct termios raw;
    int flags;
    int ret;

    if (terminal == NULL || fd < 0) {
        return -EINVAL;
    }
    memset(terminal, 0, sizeof(*terminal));
    terminal->fd = -1;
    flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        return -errno;
    }
    terminal->fd = fd;
    terminal->original_flags = flags;
    terminal->is_tty = isatty(fd) == 1;
    if (terminal->is_tty) {
        if (tcgetattr(fd, &terminal->original_termios) < 0) {
            ret = -errno;
            goto fail;
        }
        raw = terminal->original_termios;
        cfmakeraw(&raw);
        raw.c_lflag |= terminal->original_termios.c_lflag & ISIG;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        ret = -errno;
        goto fail;
    }
    if (terminal->is_tty && tcsetattr(fd, TCSANOW, &raw) < 0) {
        ret = -errno;
        fcntl(fd, F_SETFL, flags);
        goto fail;
    }
    terminal->active = true;
    return 0;

fail:
    memset(terminal, 0, sizeof(*terminal));
    terminal->fd = -1;
    return ret;
}

int vart_terminal_restore(VartTerminal *terminal)
{
    int result = 0;

    if (terminal == NULL) {
        return -EINVAL;
    }
    if (!terminal->active) {
        return 0;
    }
    if (terminal->is_tty &&
        tcsetattr(terminal->fd, TCSANOW,
                  &terminal->original_termios) < 0) {
        result = -errno;
    }
    if (fcntl(terminal->fd, F_SETFL, terminal->original_flags) < 0 &&
        result == 0) {
        result = -errno;
    }
    if (result == 0) {
        terminal->active = false;
    }
    return result;
}
