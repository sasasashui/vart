#ifndef VART_RUNTIME_H
#define VART_RUNTIME_H

#include <stdbool.h>

#include "vart/event-loop.h"
#include "vart/host-signal.h"
#include "vart/terminal.h"

typedef struct VartRuntime {
    VartEventLoop event_loop;
    VartHostSignals signals;
    VartTerminal terminal;
    bool event_loop_initialized;
    bool signals_initialized;
    bool terminal_initialized;
} VartRuntime;

int vart_runtime_init(VartRuntime *runtime, int input_fd);
int vart_runtime_destroy(VartRuntime *runtime);
int vart_runtime_prepare_standard_fds(void);

#endif
