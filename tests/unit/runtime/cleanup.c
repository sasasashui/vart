#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "vart/runtime.h"

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
        if (entry->d_name[0] != '.') {
            count++;
        }
    }
    closedir(directory);
    return count;
}

static int same_runtime_mask(const sigset_t *first, const sigset_t *second)
{
    static const int signals[] = { SIGINT, SIGTERM, SIGHUP, SIGUSR1 };
    size_t i;

    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        if (sigismember(first, signals[i]) !=
            sigismember(second, signals[i])) {
            return 0;
        }
    }
    return 1;
}

static int unused_event(VartEventSource *source, uint32_t events,
                        void *opaque)
{
    (void)source;
    (void)events;
    (void)opaque;
    return 0;
}

static int check_success_cleanup(void)
{
    VartEventSource source;
    VartRuntime runtime;
    sigset_t mask_before;
    sigset_t mask_after;
    int flags_before;
    int fds_before;
    int fds_after;
    int pipes[2];
    int ret = -1;

    if (pipe(pipes) < 0) {
        return -errno;
    }
    flags_before = fcntl(pipes[0], F_GETFL);
    fds_before = count_open_fds();
    if (flags_before < 0 || fds_before < 0 ||
        pthread_sigmask(SIG_SETMASK, NULL, &mask_before) != 0 ||
        vart_runtime_init(&runtime, pipes[0]) < 0) {
        goto out;
    }
    vart_event_source_init(&source);
    if (!(fcntl(pipes[0], F_GETFL) & O_NONBLOCK) ||
        vart_event_add(&runtime.event_loop, &source, pipes[0],
                       VART_EVENT_READ, unused_event, NULL) < 0 ||
        vart_runtime_destroy(&runtime) < 0 || source.registered ||
        source.loop != NULL ||
        vart_runtime_destroy(&runtime) < 0 ||
        fcntl(pipes[0], F_GETFL) != flags_before ||
        pthread_sigmask(SIG_SETMASK, NULL, &mask_after) != 0 ||
        !same_runtime_mask(&mask_before, &mask_after)) {
        goto out;
    }
    fds_after = count_open_fds();
    if (fds_after != fds_before) {
        goto out;
    }
    ret = 0;
out:
    close(pipes[1]);
    close(pipes[0]);
    return ret;
}

static int check_partial_init_cleanup(void)
{
    VartRuntime runtime;
    sigset_t mask_before;
    sigset_t mask_after;
    int fds_before;

    fds_before = count_open_fds();
    if (fds_before < 0 ||
        pthread_sigmask(SIG_SETMASK, NULL, &mask_before) != 0 ||
        vart_runtime_init(&runtime, INT_MAX) != -EBADF ||
        runtime.event_loop_initialized || runtime.signals_initialized ||
        runtime.terminal_initialized ||
        count_open_fds() != fds_before ||
        pthread_sigmask(SIG_SETMASK, NULL, &mask_after) != 0 ||
        !same_runtime_mask(&mask_before, &mask_after)) {
        return -1;
    }
    return 0;
}

static int check_closed_standard_input(void)
{
    char target[64];
    int status;
    pid_t pid;
    ssize_t length;

    pid = fork();
    if (pid < 0) {
        return -errno;
    }
    if (pid == 0) {
        close(STDIN_FILENO);
        if (vart_runtime_prepare_standard_fds() < 0) {
            _exit(1);
        }
        length = readlink("/proc/self/fd/0", target, sizeof(target) - 1);
        if (length < 0) {
            _exit(1);
        }
        target[length] = '\0';
        _exit(strcmp(target, "/dev/null") == 0 &&
              (fcntl(STDIN_FILENO, F_GETFL) & O_ACCMODE) == O_RDONLY ?
              0 : 1);
    }
    if (waitpid(pid, &status, 0) != pid) {
        return -errno;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -ECHILD;
}

int main(void)
{
    VartRuntime runtime;
    int i;

    if (vart_runtime_init(NULL, STDIN_FILENO) != -EINVAL ||
        vart_runtime_init(&runtime, -1) != -EINVAL ||
        vart_runtime_destroy(NULL) != -EINVAL ||
        check_partial_init_cleanup() < 0 ||
        check_closed_standard_input() < 0) {
        return EXIT_FAILURE;
    }
    for (i = 0; i < 100; i++) {
        if (check_success_cleanup() < 0) {
            return EXIT_FAILURE;
        }
    }
    puts("ok - roll back and release runtime resources");
    return EXIT_SUCCESS;
}
