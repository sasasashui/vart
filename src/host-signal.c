#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "vart/host-signal.h"
#include "vart/vcpu.h"

int vart_host_signals_init(VartHostSignals *signals)
{
    sigset_t block_mask;
    int ret;

    if (signals == NULL) {
        return -EINVAL;
    }
    memset(signals, 0, sizeof(*signals));
    signals->fd = -1;
    sigemptyset(&signals->control_mask);
    sigaddset(&signals->control_mask, SIGINT);
    sigaddset(&signals->control_mask, SIGTERM);
    sigaddset(&signals->control_mask, SIGHUP);
    block_mask = signals->control_mask;
    sigaddset(&block_mask, VART_VCPU_KICK_SIGNAL);
    ret = pthread_sigmask(SIG_BLOCK, &block_mask, &signals->original_mask);
    if (ret != 0) {
        return -ret;
    }
    signals->fd = signalfd(-1, &signals->control_mask,
                           SFD_NONBLOCK | SFD_CLOEXEC);
    if (signals->fd < 0) {
        ret = -errno;
        pthread_sigmask(SIG_SETMASK, &signals->original_mask, NULL);
        return ret;
    }
    signals->active = true;
    return 0;
}

int vart_host_signals_read(VartHostSignals *signals, int *signum)
{
    struct signalfd_siginfo info;
    ssize_t count;

    if (signals == NULL || signum == NULL || !signals->active) {
        return -EINVAL;
    }
    do {
        count = read(signals->fd, &info, sizeof(info));
    } while (count < 0 && errno == EINTR);
    if (count < 0) {
        return -errno;
    }
    if (count != sizeof(info) ||
        sigismember(&signals->control_mask, info.ssi_signo) != 1) {
        return -EIO;
    }
    *signum = info.ssi_signo;
    return 0;
}

int vart_host_signals_restore(VartHostSignals *signals)
{
    int result = 0;
    int ret;

    if (signals == NULL) {
        return -EINVAL;
    }
    if (!signals->active) {
        return 0;
    }
    if (close(signals->fd) < 0) {
        result = -errno;
    }
    ret = pthread_sigmask(SIG_SETMASK, &signals->original_mask, NULL);
    if (ret != 0 && result == 0) {
        result = -ret;
    }
    signals->fd = -1;
    signals->active = false;
    return result;
}
