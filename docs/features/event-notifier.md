# Event notifier

`VartEventNotifier` is the project-owned wrapper around a non-blocking,
close-on-exec Linux `eventfd`. It provides a small notification primitive for
future IOThread, ioeventfd, and irqfd users without exposing the host fd
creation details to devices.

Notifications accumulate in the eventfd counter. `notify()` adds a non-zero
value and `read()` consumes the current counter value. Reading an empty
notifier returns `-EAGAIN`; callers can use `vart_event_notifier_get_fd()` with
the event loop or pass the fd to a KVM ioctl.

The notifier owns its fd from successful initialization until destruction.
Initialization, notification, and reads are not implicitly synchronized; a
caller must provide the required locking when sharing a notifier across
threads. The initial implementation only provides the primitive and does not
change the existing userspace MSI-X injector.

`tests/unit/event/notifier.c` validates counter accumulation, polling,
non-blocking reads, invalid arguments, repeated initialization, and teardown.
