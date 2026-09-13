# Integration tests

Integration tests exercise interactions among rvmm-lab subsystems. Tests that
require `/dev/kvm` must detect its availability and report a clear skip or
failure according to the test target being run.

