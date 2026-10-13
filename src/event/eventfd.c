#define _GNU_SOURCE

#include <errno.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "vart/eventfd.h"

static int vart_event_notifier_dispatch(VartEventSource *event_source,
                                         uint32_t events, void *opaque)
{
    VartEventNotifierSource *source = opaque;
    uint64_t value;
    int ret;

    (void)event_source;
    if (source == NULL || source->notifier == NULL ||
        source->callback == NULL) {
        return -EINVAL;
    }
    if (!(events & VART_EVENT_READ)) {
        return -EIO;
    }
    ret = vart_event_notifier_read(source->notifier, &value);
    if (ret == -EAGAIN) {
        return 0;
    }
    if (ret < 0) {
        return ret;
    }
    return source->callback(source->notifier, value, source->opaque);
}

int vart_event_notifier_init(VartEventNotifier *notifier)
{
    int fd;

    if (notifier == NULL) {
        return -EINVAL;
    }
    if (notifier->initialized) {
        return -EBUSY;
    }
    fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (fd < 0) {
        return -errno;
    }
    notifier->fd = fd;
    notifier->initialized = true;
    return 0;
}

void vart_event_notifier_destroy(VartEventNotifier *notifier)
{
    if (notifier == NULL) {
        return;
    }
    if (notifier->initialized) {
        close(notifier->fd);
    }
    notifier->fd = -1;
    notifier->initialized = false;
}

int vart_event_notifier_notify(VartEventNotifier *notifier, uint64_t value)
{
    ssize_t count;

    if (notifier == NULL || !notifier->initialized || value == 0) {
        return -EINVAL;
    }
    do {
        count = write(notifier->fd, &value, sizeof(value));
    } while (count < 0 && errno == EINTR);
    if (count == (ssize_t)sizeof(value)) {
        return 0;
    }
    return count < 0 ? -errno : -EIO;
}

int vart_event_notifier_read(VartEventNotifier *notifier, uint64_t *value)
{
    ssize_t count;

    if (notifier == NULL || !notifier->initialized || value == NULL) {
        return -EINVAL;
    }
    do {
        count = read(notifier->fd, value, sizeof(*value));
    } while (count < 0 && errno == EINTR);
    if (count == (ssize_t)sizeof(*value)) {
        return 0;
    }
    return count < 0 ? -errno : -EIO;
}

int vart_event_notifier_get_fd(const VartEventNotifier *notifier)
{
    if (notifier == NULL || !notifier->initialized) {
        return -1;
    }
    return notifier->fd;
}

int vart_event_notifier_source_add(VartEventLoop *loop,
                                   VartEventNotifierSource *source,
                                   VartEventNotifier *notifier,
                                   VartEventNotifierCallback callback,
                                   void *opaque)
{
    int fd;

    if (loop == NULL || source == NULL || notifier == NULL ||
        callback == NULL || source->event_source.registered) {
        return -EINVAL;
    }
    fd = vart_event_notifier_get_fd(notifier);
    if (fd < 0) {
        return -EINVAL;
    }
    source->notifier = notifier;
    source->callback = callback;
    source->opaque = opaque;
    vart_event_source_init(&source->event_source);
    return vart_event_add(loop, &source->event_source, fd, VART_EVENT_READ,
                          vart_event_notifier_dispatch, source);
}

int vart_event_notifier_source_remove(VartEventNotifierSource *source)
{
    int ret;

    if (source == NULL || !source->event_source.registered) {
        return -EINVAL;
    }
    ret = vart_event_remove(&source->event_source);
    if (ret < 0) {
        return ret;
    }
    source->notifier = NULL;
    source->callback = NULL;
    source->opaque = NULL;
    return 0;
}
