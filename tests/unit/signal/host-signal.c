#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "vart/host-signal.h"
#include "vart/vcpu.h"

static int mask_matches(const sigset_t *first, const sigset_t *second,
                        int signum)
{
    return sigismember(first, signum) == sigismember(second, signum);
}

static int check_signal(VartHostSignals *signals, int expected)
{
    int signum;
    int ret;

    if (kill(getpid(), expected) < 0) {
        return -errno;
    }
    ret = vart_host_signals_read(signals, &signum);
    return ret < 0 ? ret : signum == expected ? 0 : -EINVAL;
}

int main(void)
{
    static const int control_signals[] = { SIGINT, SIGTERM, SIGHUP };
    VartHostSignals signals;
    sigset_t before;
    sigset_t blocked;
    sigset_t after;
    size_t i;
    int signum;

    if (pthread_sigmask(SIG_SETMASK, NULL, &before) != 0 ||
        vart_host_signals_init(NULL) != -EINVAL ||
        vart_host_signals_init(&signals) < 0 || !signals.active ||
        signals.fd < 0 ||
        pthread_sigmask(SIG_SETMASK, NULL, &blocked) != 0 ||
        sigismember(&blocked, VART_VCPU_KICK_SIGNAL) != 1) {
        return EXIT_FAILURE;
    }
    for (i = 0; i < sizeof(control_signals) / sizeof(control_signals[0]);
         i++) {
        if (sigismember(&blocked, control_signals[i]) != 1 ||
            check_signal(&signals, control_signals[i]) < 0) {
            return EXIT_FAILURE;
        }
    }
    if (vart_host_signals_read(&signals, &signum) != -EAGAIN ||
        vart_host_signals_read(NULL, &signum) != -EINVAL ||
        vart_host_signals_read(&signals, NULL) != -EINVAL ||
        vart_host_signals_restore(&signals) < 0 || signals.active ||
        signals.fd != -1 || vart_host_signals_restore(&signals) < 0 ||
        pthread_sigmask(SIG_SETMASK, NULL, &after) != 0 ||
        !mask_matches(&before, &after, SIGINT) ||
        !mask_matches(&before, &after, SIGTERM) ||
        !mask_matches(&before, &after, SIGHUP) ||
        !mask_matches(&before, &after, VART_VCPU_KICK_SIGNAL) ||
        vart_host_signals_restore(NULL) != -EINVAL) {
        return EXIT_FAILURE;
    }
    puts("ok - route blocked host signals through signalfd");
    return EXIT_SUCCESS;
}
