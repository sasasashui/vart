#ifndef VART_EVENTFD_H
#define VART_EVENTFD_H

#include <stdbool.h>
#include <stdint.h>

#include "vart/event-loop.h"

typedef struct VartEventNotifier {
    int fd;
    bool initialized;
} VartEventNotifier;

typedef int (*VartEventNotifierCallback)(VartEventNotifier *notifier,
                                         uint64_t value, void *opaque);

typedef struct VartEventNotifierSource {
    VartEventSource event_source;
    VartEventNotifier *notifier;
    VartEventNotifierCallback callback;
    void *opaque;
} VartEventNotifierSource;

int vart_event_notifier_init(VartEventNotifier *notifier);
void vart_event_notifier_destroy(VartEventNotifier *notifier);
int vart_event_notifier_notify(VartEventNotifier *notifier, uint64_t value);
int vart_event_notifier_read(VartEventNotifier *notifier, uint64_t *value);
int vart_event_notifier_get_fd(const VartEventNotifier *notifier);
int vart_event_notifier_source_add(VartEventLoop *loop,
                                   VartEventNotifierSource *source,
                                   VartEventNotifier *notifier,
                                   VartEventNotifierCallback callback,
                                   void *opaque);
int vart_event_notifier_source_remove(VartEventNotifierSource *source);

#endif
