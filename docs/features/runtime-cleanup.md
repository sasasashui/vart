# Runtime cleanup

`VartRuntime` owns the host resources used while a guest is running: the event
loop, blocked host-signal state and `signalfd`, and terminal configuration. Its
initializer acquires them in that order. A partial initialization failure uses
the same reverse-order destruction path as normal shutdown.

Destruction first restores the terminal, then destroys the event loop and
invalidates all registered event sources, and finally closes the signal file
descriptor and restores the controller thread's original signal mask. Cleanup
continues after an individual operation fails and returns the first error. A
successful cleanup is idempotent. The machine and KVM descriptors remain owned
by the machine and entry-point layers respectively.

Before parsing the command line or opening KVM, the process verifies file
descriptors 0, 1, and 2. Any closed standard descriptor is opened on
`/dev/null`. This prevents later KVM, VM, or device opens from reusing a
standard descriptor that the console layer would otherwise misidentify.

The runtime unit test verifies successful and partial initialization paths. It
checks input file flags, the four runtime-controlled signal mask bits, open
file-descriptor counts, event-source invalidation, and repeated destruction.
The integration test covers a missing boot file after runtime setup, a closed
console output descriptor after vCPU execution, and recovery when standard
input was closed before process startup.
