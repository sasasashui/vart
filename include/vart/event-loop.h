#ifndef VART_EVENT_LOOP_H
#define VART_EVENT_LOOP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct VartEventLoop VartEventLoop;
typedef struct VartEventSource VartEventSource;

enum VartEvent {
    VART_EVENT_READ = 1U << 0,
    VART_EVENT_WRITE = 1U << 1,
    VART_EVENT_ERROR = 1U << 2,
    VART_EVENT_HANGUP = 1U << 3,
};

typedef int (*VartEventCallback)(VartEventSource *source, uint32_t events,
                                 void *opaque);

struct VartEventSource {
    VartEventLoop *loop;
    VartEventCallback callback;
    void *opaque;
    uint64_t generation;
    int fd;
    uint32_t events;
    bool registered;
};

struct VartEventLoop {
    VartEventSource **sources;
    size_t count;
    size_t capacity;
    int wake_fd;
    bool initialized;
};

int vart_event_loop_init(VartEventLoop *loop);
void vart_event_loop_destroy(VartEventLoop *loop);
void vart_event_source_init(VartEventSource *source);
int vart_event_add(VartEventLoop *loop, VartEventSource *source, int fd,
                   uint32_t events, VartEventCallback callback, void *opaque);
int vart_event_modify(VartEventSource *source, uint32_t events);
int vart_event_remove(VartEventSource *source);
int vart_event_loop_wake(VartEventLoop *loop);
int vart_event_loop_run_once(VartEventLoop *loop, int timeout_ms);

#endif
