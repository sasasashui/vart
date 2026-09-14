# Synchronization

VART owns its synchronization API so VM, vCPU, and device code do not depend
directly on a particular host implementation. Mutexes and condition variables
currently use pthreads. Linux futexes are reserved for low-overhead event and
vCPU kick paths where a simple atomic state plus wait/wake operation is a better
fit than a condition variable.

## VM big lock

Each VM owns a big lock. It protects guest-visible shared state, including MMIO
device callbacks, address-space topology changes, interrupt-controller state,
and machine lifecycle transitions. A vCPU must release the big lock before
entering `KVM_RUN` and acquire it again before handling a VM exit.

Subsystem code should use `VartMutex` and `VartCond` rather than pthread types.
This keeps ownership checks and future synchronization backends centralized.
The project-wide order remains:

```text
VM big lock
    -> address-space or IOMMU lock
        -> bus or device lock
            -> queue or completion lock
```

## Thread signal masks

`vart_thread_create()` temporarily blocks asynchronous signals in the creating
thread so a new project-owned thread inherits a controlled mask, then restores
the creator's original mask. Synchronous fault signals (`SIGSEGV`, `SIGFPE`,
`SIGILL`, and `SIGBUS`) remain unblocked.

Worker types explicitly open only the signals they consume. The vCPU lifecycle
controller blocks the reserved `SIGUSR1` kick signal, while each vCPU thread
unblocks it after installing its thread-local KVM context. Future I/O and event
threads must use the same creation wrapper and must not change signal masks
without documenting ownership of each newly opened signal.

The main runtime additionally blocks `SIGINT`, `SIGTERM`, and `SIGHUP` before
starting vCPU threads. `VartHostSignals` exposes those control signals through
a nonblocking `signalfd`, allowing the event-loop callback to request VM
shutdown and kick every vCPU in ordinary thread context. Cleanup joins all
vCPUs and restores terminal state before closing the signal descriptor and
restoring the controller's exact original mask. No asynchronous signal handler
takes a lock or performs lifecycle operations.

## Debug locks

Build with `CONFIG_DEBUG_LOCKS=y` to enable owner tracking and error-checking
pthread mutexes:

```text
make BUILD_DIR=build-debug CONFIG_DEBUG_LOCKS=y check
```

The debug implementation detects recursive locking, unlocking by a non-owner,
destroying a held mutex, waiting on a condition without holding its mutex,
non-LIFO unlocks, fixed-rank lock-order inversions, and failed lock-context
assertions. A failure reports the lock name, initialization site, and failing
call site before aborting. Ranked locks encode the documented global order;
unclassified locks still receive ownership and nesting checks.

Owner state uses C11 atomics because diagnostic metadata can be inspected by a
thread that does not own the mutex. Debug checks are compiled out when the
configuration is disabled. This is deliberately smaller than Linux lockdep:
the current lock hierarchy is shallow, so VART does not yet maintain a global
lock-class dependency graph. The fixed ranks can be replaced by such a graph if
the hierarchy becomes too dynamic for a total order.
