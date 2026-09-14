#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "vart/event-loop.h"

#define VART_EVENT_INTERESTS (VART_EVENT_READ | VART_EVENT_WRITE)

typedef struct VartPollEntry {
    VartEventSource *source;
    uint64_t generation;
} VartPollEntry;

static short events_to_poll(uint32_t events)
{
    short result = 0;

    if (events & VART_EVENT_READ) {
        result |= POLLIN;
    }
    if (events & VART_EVENT_WRITE) {
        result |= POLLOUT;
    }
    return result;
}

static uint32_t events_from_poll(short events)
{
    uint32_t result = 0;

    if (events & POLLIN) {
        result |= VART_EVENT_READ;
    }
    if (events & POLLOUT) {
        result |= VART_EVENT_WRITE;
    }
    if (events & (POLLERR | POLLNVAL)) {
        result |= VART_EVENT_ERROR;
    }
    if (events & POLLHUP) {
        result |= VART_EVENT_HANGUP;
    }
    return result;
}

int vart_event_loop_init(VartEventLoop *loop)
{
    if (loop == NULL) {
        return -EINVAL;
    }
    memset(loop, 0, sizeof(*loop));
    loop->wake_fd = -1;
    loop->wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (loop->wake_fd < 0) {
        return -errno;
    }
    loop->initialized = true;
    return 0;
}

void vart_event_loop_destroy(VartEventLoop *loop)
{
    size_t i;

    if (loop == NULL) {
        return;
    }
    for (i = 0; loop->initialized && i < loop->count; i++) {
        VartEventSource *source = loop->sources[i];

        source->loop = NULL;
        source->registered = false;
        source->generation++;
    }
    free(loop->sources);
    if (loop->initialized) {
        close(loop->wake_fd);
    }
    memset(loop, 0, sizeof(*loop));
    loop->wake_fd = -1;
}

void vart_event_source_init(VartEventSource *source)
{
    if (source != NULL) {
        memset(source, 0, sizeof(*source));
        source->fd = -1;
    }
}

int vart_event_add(VartEventLoop *loop, VartEventSource *source, int fd,
                   uint32_t events, VartEventCallback callback, void *opaque)
{
    VartEventSource **sources;
    size_t capacity;

    if (loop == NULL || !loop->initialized || source == NULL || fd < 0 ||
        callback == NULL ||
        (events & ~VART_EVENT_INTERESTS) != 0) {
        return -EINVAL;
    }
    if (source->registered) {
        return -EEXIST;
    }
    if (loop->count == loop->capacity) {
        capacity = loop->capacity == 0 ? 8 : loop->capacity * 2;
        if (capacity < loop->capacity ||
            capacity > SIZE_MAX / sizeof(*sources)) {
            return -EOVERFLOW;
        }
        sources = realloc(loop->sources, capacity * sizeof(*sources));
        if (sources == NULL) {
            return -ENOMEM;
        }
        loop->sources = sources;
        loop->capacity = capacity;
    }
    source->loop = loop;
    source->callback = callback;
    source->opaque = opaque;
    source->fd = fd;
    source->events = events;
    source->generation++;
    source->registered = true;
    loop->sources[loop->count++] = source;
    return 0;
}

int vart_event_modify(VartEventSource *source, uint32_t events)
{
    if (source == NULL || !source->registered ||
        (events & ~VART_EVENT_INTERESTS) != 0) {
        return -EINVAL;
    }
    source->events = events;
    source->generation++;
    return 0;
}

int vart_event_remove(VartEventSource *source)
{
    VartEventLoop *loop;
    size_t i;

    if (source == NULL || !source->registered || source->loop == NULL) {
        return -EINVAL;
    }
    loop = source->loop;
    for (i = 0; i < loop->count; i++) {
        if (loop->sources[i] == source) {
            memmove(&loop->sources[i], &loop->sources[i + 1],
                    (loop->count - i - 1) * sizeof(*loop->sources));
            loop->count--;
            source->loop = NULL;
            source->registered = false;
            source->generation++;
            return 0;
        }
    }
    return -ENOENT;
}

int vart_event_loop_wake(VartEventLoop *loop)
{
    uint64_t value = 1;
    ssize_t count;

    if (loop == NULL || !loop->initialized) {
        return -EINVAL;
    }
    do {
        count = write(loop->wake_fd, &value, sizeof(value));
    } while (count < 0 && errno == EINTR);
    if (count == (ssize_t)sizeof(value) ||
        (count < 0 && errno == EAGAIN)) {
        return 0;
    }
    return count < 0 ? -errno : -EIO;
}

static int event_loop_drain_wake(VartEventLoop *loop)
{
    uint64_t value;
    ssize_t count;

    do {
        count = read(loop->wake_fd, &value, sizeof(value));
    } while (count < 0 && errno == EINTR);
    if (count == (ssize_t)sizeof(value) ||
        (count < 0 && errno == EAGAIN)) {
        return 0;
    }
    return count < 0 ? -errno : -EIO;
}

int vart_event_loop_run_once(VartEventLoop *loop, int timeout_ms)
{
    VartPollEntry *entries;
    struct pollfd *pollfds;
    size_t count;
    size_t i;
    int callbacks = 0;
    int ret;

    if (loop == NULL || !loop->initialized || timeout_ms < -1) {
        return -EINVAL;
    }
    count = loop->count;
    pollfds = calloc(count + 1, sizeof(*pollfds));
    entries = count == 0 ? NULL : calloc(count, sizeof(*entries));
    if (pollfds == NULL || (count != 0 && entries == NULL)) {
        free(entries);
        free(pollfds);
        return -ENOMEM;
    }
    pollfds[0].fd = loop->wake_fd;
    pollfds[0].events = POLLIN;
    for (i = 0; i < count; i++) {
        VartEventSource *source = loop->sources[i];

        pollfds[i + 1].fd = source->fd;
        pollfds[i + 1].events = events_to_poll(source->events);
        entries[i].source = source;
        entries[i].generation = source->generation;
    }
    ret = poll(pollfds, count + 1, timeout_ms);
    if (ret < 0) {
        ret = -errno;
        goto out;
    }
    if (pollfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        ret = -EIO;
        goto out;
    }
    if (pollfds[0].revents & POLLIN) {
        ret = event_loop_drain_wake(loop);
        if (ret < 0) {
            goto out;
        }
    }
    for (i = 0; i < count; i++) {
        VartEventSource *source = entries[i].source;
        uint32_t events;

        if (pollfds[i + 1].revents == 0) {
            continue;
        }
        if (!source->registered || source->loop != loop ||
            source->generation != entries[i].generation) {
            continue;
        }
        events = events_from_poll(pollfds[i + 1].revents);
        if (events == 0) {
            continue;
        }
        ret = source->callback(source, events, source->opaque);
        if (ret < 0) {
            goto out;
        }
        callbacks++;
    }
    ret = callbacks;
out:
    free(entries);
    free(pollfds);
    return ret;
}
