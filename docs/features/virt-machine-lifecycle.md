# Virt machine lifecycle

`VartVirtMachine` is the ownership boundary for one QEMU-compatible RISC-V
`virt` machine. The caller owns the open `VartKvm` handle; the machine owns its
KVM VM, RAM mapping and slot, vCPUs, system address space, in-kernel AIA,
UART interrupt line, UART, and optional test device.

Creation follows the dependency order VM, RAM, vCPUs, AIA, IRQ routes, and
devices. Destruction first stops and joins any vCPU workers, then releases
devices and interrupt state, vCPUs, RAM, and the VM in reverse dependency
order. Partial construction uses the same destructor, and destruction is
idempotent so every caller has one cleanup path.

`vart_virt_machine_init_boot()` applies the direct S-mode boot contract to all
vCPUs. Hart 0 becomes runnable and secondary harts remain stopped for SBI HSM.
The start and join operations cover every configured vCPU, while exit policy
stays with the caller through the existing callback interface.

The initial system address space contains the UART and, for tiny regression
guests only, the optional VART test device. RAM remains a KVM memory slot;
future RAM address-region publication and platform/PCIe buses can be added
without changing device MMIO or IRQ interfaces.

The KVM integration test constructs the complete machine, checks its topology,
loads the existing MMIO round-trip guest, initializes boot state, captures UART
output, observes test-device completion, and exercises repeated destruction.
