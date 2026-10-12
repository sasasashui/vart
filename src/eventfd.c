#define _GNU_SOURCE

#include <errno.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "vart/eventfd.h"

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
