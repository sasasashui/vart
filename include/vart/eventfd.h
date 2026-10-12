#ifndef VART_EVENTFD_H
#define VART_EVENTFD_H

#include <stdbool.h>
#include <stdint.h>

typedef struct VartEventNotifier {
    int fd;
    bool initialized;
} VartEventNotifier;

int vart_event_notifier_init(VartEventNotifier *notifier);
void vart_event_notifier_destroy(VartEventNotifier *notifier);
int vart_event_notifier_notify(VartEventNotifier *notifier, uint64_t value);
int vart_event_notifier_read(VartEventNotifier *notifier, uint64_t *value);
int vart_event_notifier_get_fd(const VartEventNotifier *notifier);

#endif
