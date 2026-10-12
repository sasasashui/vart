#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/event-loop.h"
#include "vart/eventfd.h"

typedef struct NotifierContext {
    unsigned int calls;
    uint64_t value;
} NotifierContext;

static int record_notification(VartEventNotifier *notifier, uint64_t value,
                               void *opaque)
{
    NotifierContext *context = opaque;

    (void)notifier;
    context->calls++;
    context->value = value;
    return 0;
}

int main(void)
{
    VartEventLoop loop;
    VartEventNotifier notifier = { .fd = -1 };
    VartEventNotifierSource source = { 0 };
    NotifierContext context = { 0 };

    if (vart_event_loop_init(&loop) < 0 ||
        vart_event_notifier_init(&notifier) < 0 ||
        vart_event_notifier_source_add(&loop, &source, &notifier,
                                       record_notification, &context) < 0 ||
        vart_event_notifier_notify(&notifier, 5) < 0 ||
        vart_event_loop_run_once(&loop, 0) != 1 ||
        context.calls != 1 || context.value != 5 ||
        vart_event_loop_run_once(&loop, 0) != 0 ||
        vart_event_notifier_source_remove(&source) < 0 ||
        vart_event_notifier_source_remove(&source) >= 0) {
        if (source.event_source.registered) {
            vart_event_notifier_source_remove(&source);
        }
        vart_event_notifier_destroy(&notifier);
        vart_event_loop_destroy(&loop);
        return EXIT_FAILURE;
    }
    vart_event_notifier_destroy(&notifier);
    vart_event_loop_destroy(&loop);
    puts("ok - dispatch eventfd notifications through the event loop");
    return EXIT_SUCCESS;
}
