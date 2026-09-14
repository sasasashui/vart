#ifndef VART_HOST_SIGNAL_H
#define VART_HOST_SIGNAL_H

#include <stdbool.h>
#include <signal.h>

typedef struct VartHostSignals {
    sigset_t original_mask;
    sigset_t control_mask;
    int fd;
    bool active;
} VartHostSignals;

int vart_host_signals_init(VartHostSignals *signals);
int vart_host_signals_read(VartHostSignals *signals, int *signum);
int vart_host_signals_restore(VartHostSignals *signals);

#endif
