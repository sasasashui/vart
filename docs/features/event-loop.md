# Event loop

VART exposes a project-owned file-descriptor event API in
`include/vart/event-loop.h`. Devices and I/O backends register read or write
interests without depending on the host polling mechanism. Error and hangup
conditions are always reported when supplied by the backend.

Sources are initialized with `vart_event_source_init()` before registration.
Setting a source's interests to zero fully disables it, including error and
hangup notification, until a later modification re-enables an interest.

The initial implementation builds a stable snapshot and waits with `poll()`.
A callback may modify or remove a source, including another source that was
ready in the same snapshot. Generation checks prevent stale readiness from
being dispatched after such a change. Source storage must remain valid until
the current `vart_event_loop_run_once()` call returns.

Callbacks return zero on success or a negative errno-style result. A callback
failure stops the current dispatch and is returned to the event-loop owner.
Timeouts use milliseconds, with `-1` meaning no timeout. An empty loop follows
the same timeout rules.

Event-source dispatch remains intentionally single-threaded. Cross-thread
callers use the wakeup API without exposing `poll()` to devices.

The loop owns a nonblocking close-on-exec `eventfd`. Any thread may call
`vart_event_loop_wake()` to interrupt `poll()`, including an infinite wait.
Multiple wakeups are coalesced and drained before device callbacks are
dispatched. A wakeup is a control notification, so it does not increment the
callback count returned by `vart_event_loop_run_once()`.

Event-source registration, modification, removal, and dispatch remain owned
by the event-loop thread. Callers must ensure the loop outlives threads that
can wake it; destruction closes the internal descriptor after those callers
have stopped. Stage 9.2 does not introduce a second lock or alter the VM big
lock order.
