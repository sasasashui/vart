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
