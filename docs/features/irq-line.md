# Interrupt lines

## Purpose

`VartIrq` decouples device models from the interrupt-controller backend. A
device can raise, lower, or pulse its output without depending on KVM, APLIC,
or a particular event loop. The controller callback receives only its opaque
state, line number, and requested level.

## Semantics

The line tracks its successfully applied level and suppresses duplicate level
changes. State changes are committed only after the backend accepts them. A
pulse requires an initially low line and performs a high transition followed
by a low transition. This keeps level-sensitive devices able to retain an
assertion while giving edge-triggered devices a small convenience operation.

The KVM AIA adapter binds a generic line to one valid APLIC source and forwards
transitions through `KVM_IRQ_LINE`. Device models retain the `VartIrq` pointer;
the machine owns both the line and controller and must keep them alive longer
than the device.
Line state is not internally locked; callers serialize device-visible changes
with the VM big lock under the current threading model.

## Validation

The unit test covers duplicate suppression, pulse ordering, error propagation,
and state rollback. The VART test device exposes a test-only pulse register;
its integration guest programs APLIC source 1, writes that register, and then
claims IMSIC identity 1. This verifies device MMIO through the generic line,
KVM APLIC, and KVM IMSIC without a direct AIA call in the device model.
