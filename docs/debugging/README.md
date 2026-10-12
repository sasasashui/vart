# Debugging records

This directory preserves investigations of difficult, surprising, or
time-consuming failures. The records support later review and help prevent the
same class of bug from being rediscovered.

Every record must state the ownership classification near the beginning:

* `VART` for a defect in VART code or its tests;
* `Linux/KVM` for a host-kernel or guest-kernel defect;
* `QEMU upstream` for a defect in the QEMU reference implementation; or
* `test infrastructure` when the failure is caused by the reproducer itself.

If the result is not yet conclusive, mark it as `investigation` and list the
evidence needed to classify it. Do not describe a workaround as an upstream
fix. Each record should also state whether an upstream issue or patch exists,
and link the relevant branch, commit, or submission when available.

Create a record after the root cause is understood. Use a descriptive filename
such as `aia-imsic-interrupt-not-delivered.md`; do not number files unless an
external issue number is useful.

Record only durable diagnostic knowledge. Routine build failures, spelling
mistakes, and short-lived notes do not belong here.
