#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>

#include "vart/eventfd.h"

int main(void)
{
    VartEventNotifier notifier = { .fd = -1 };
    struct pollfd pollfd;
    uint64_t value;

    if (vart_event_notifier_init(NULL) != -EINVAL ||
        vart_event_notifier_get_fd(NULL) != -1 ||
        vart_event_notifier_read(NULL, &value) != -EINVAL ||
        vart_event_notifier_notify(NULL, 1) != -EINVAL) {
        return EXIT_FAILURE;
    }
    if (vart_event_notifier_init(&notifier) < 0 ||
        vart_event_notifier_get_fd(&notifier) < 0 ||
        vart_event_notifier_init(&notifier) != -EBUSY ||
        vart_event_notifier_read(&notifier, &value) != -EAGAIN ||
        vart_event_notifier_notify(&notifier, 0) != -EINVAL) {
        vart_event_notifier_destroy(&notifier);
        return EXIT_FAILURE;
    }
    if (vart_event_notifier_notify(&notifier, 3) < 0 ||
        vart_event_notifier_notify(&notifier, 4) < 0) {
        vart_event_notifier_destroy(&notifier);
        return EXIT_FAILURE;
    }
    pollfd.fd = vart_event_notifier_get_fd(&notifier);
    pollfd.events = POLLIN;
    pollfd.revents = 0;
    if (poll(&pollfd, 1, 0) != 1 || !(pollfd.revents & POLLIN) ||
        vart_event_notifier_read(&notifier, &value) < 0 || value != 7 ||
        vart_event_notifier_read(&notifier, &value) != -EAGAIN) {
        vart_event_notifier_destroy(&notifier);
        return EXIT_FAILURE;
    }
    vart_event_notifier_destroy(&notifier);
    if (vart_event_notifier_get_fd(&notifier) != -1 ||
        vart_event_notifier_read(&notifier, &value) != -EINVAL ||
        vart_event_notifier_notify(&notifier, 1) != -EINVAL) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
