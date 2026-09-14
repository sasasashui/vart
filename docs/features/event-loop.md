# Event loop

VART exposes a project-owned file-descriptor event API in
`include/vart/event-loop.h`. Devices and I/O backends register read or write
interests without depending on the host polling mechanism. Error and hangup
conditions are always reported when supplied by the backend.

Sources are initialized with `vart_event_source_init()` before registration.

The initial implementation builds a stable snapshot and waits with `poll()`.
A callback may modify or remove a source, including another source that was
ready in the same snapshot. Generation checks prevent stale readiness from
being dispatched after such a change. Source storage must remain valid until
the current `vart_event_loop_run_once()` call returns.

Callbacks return zero on success or a negative errno-style result. A callback
failure stops the current dispatch and is returned to the event-loop owner.
Timeouts use milliseconds, with `-1` meaning no timeout. An empty loop follows
the same timeout rules.

Stage 9.1 is intentionally single-threaded. Cross-thread wakeup, ownership
rules, and locking are added in Stage 9.2 without exposing `poll()` to devices.
