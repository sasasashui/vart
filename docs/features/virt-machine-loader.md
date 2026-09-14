# Virt machine boot-file loader

The machine loader turns kernel and optional initramfs paths into the existing
in-memory direct-boot configuration. It accepts a common CPU ISA description,
creates one FDT CPU entry for every machine vCPU, builds the final DTB, loads
all resources into RAM, and initializes the direct S-mode boot state.

Input files must be non-empty regular files. Their sizes are checked against
guest RAM before allocation, reads retry after `EINTR`, and an unexpected EOF
is an error. Both files are completely read before guest RAM or vCPU state is
changed, so a missing or invalid initramfs cannot leave a partially loaded
kernel.

The loader does not choose command-line defaults or inspect host filesystems
beyond the requested paths. CLI policy, automatic ISA selection, and friendly
diagnostics belong to the following runtime-entry increment.

The KVM integration test covers a missing kernel, an empty initramfs, loading a
real tiny guest with an initramfs and generated FDT, direct-boot initialization,
UART output, and clean machine teardown.
