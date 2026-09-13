# Stale objects after public structure changes

## Symptom

The normal build passed, while an incremental `CONFIG_DEBUG_LOCKS=y` build
crashed in the vCPU kick test. A clean debug rebuild then completed 500 runs
without failure.

## Cause

The Makefile tracked C source prerequisites but not included headers. Changing
the public `VartVcpu` layout rebuilt `vcpu.c` and its direct test while leaving
other objects compiled against the old layout. Linking those ABI-incompatible
objects produced a misleading runtime crash that resembled a signal race.

## Resolution

Host compilations use `-MMD -MP` and include the generated dependency files.
Each object or test executable is therefore rebuilt when one of the headers it
actually includes changes. Test link rules filter their expanded prerequisites
to C sources and objects because included dependency files also add headers to
Make's `$^` list.

When a concurrency change fails in only one existing build directory, compare
a clean rebuild before treating the failure as a runtime race.
