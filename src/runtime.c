#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "vart/runtime.h"

int vart_runtime_prepare_standard_fds(void)
{
    int flags;
    int nullfd;
    int fd;

    for (fd = STDIN_FILENO; fd <= STDERR_FILENO; fd++) {
        if (fcntl(fd, F_GETFD) >= 0) {
            continue;
        }
        if (errno != EBADF) {
            return -errno;
        }
        flags = fd == STDIN_FILENO ? O_RDONLY : O_WRONLY;
        nullfd = open("/dev/null", flags);
        if (nullfd < 0) {
            return -errno;
        }
        if (nullfd != fd) {
            if (dup2(nullfd, fd) < 0) {
                int ret = -errno;

                close(nullfd);
                return ret;
            }
            close(nullfd);
        }
    }
    return 0;
}

int vart_runtime_init(VartRuntime *runtime, int input_fd)
{
    int ret;

    if (runtime == NULL || input_fd < 0) {
        return -EINVAL;
    }
    memset(runtime, 0, sizeof(*runtime));
    ret = vart_event_loop_init(&runtime->event_loop);
    if (ret < 0) {
        return ret;
    }
    runtime->event_loop_initialized = true;
    ret = vart_host_signals_init(&runtime->signals);
    if (ret < 0) {
        goto fail;
    }
    runtime->signals_initialized = true;
    ret = vart_terminal_init(&runtime->terminal, input_fd);
    if (ret < 0) {
        goto fail;
    }
    runtime->terminal_initialized = true;
    return 0;

fail:
    vart_runtime_destroy(runtime);
    return ret;
}

int vart_runtime_destroy(VartRuntime *runtime)
{
    int result = 0;
    int ret;

    if (runtime == NULL) {
        return -EINVAL;
    }
    if (runtime->terminal_initialized) {
        ret = vart_terminal_restore(&runtime->terminal);
        if (ret < 0) {
            result = ret;
        } else {
            runtime->terminal_initialized = false;
        }
    }
    if (runtime->event_loop_initialized) {
        vart_event_loop_destroy(&runtime->event_loop);
        runtime->event_loop_initialized = false;
    }
    if (runtime->signals_initialized) {
        ret = vart_host_signals_restore(&runtime->signals);
        if (ret < 0 && result == 0) {
            result = ret;
        }
        runtime->signals_initialized = false;
    }
    return result;
}
