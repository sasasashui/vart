# VART RISC-V IOMMU DMA 调试记录

## 现象

使用 Linux 7.3-rc5（linux-iommu/next，启用 RISC-V IOMMU DMA 和 MSI remapping）启动
`--riscv-iommu --pcie-switch` 时，PCIe 和 virtio-blk 可以枚举，但第一次 virtio 队列 DMA
访问失败。相同内核在 QEMU `virt,aia=aplic-imsic` 上可以完成块设备访问。

## 根因和修复

1. VART 只声明了 Sv39x4 capability，而 Linux 使用的是标准 Sv39（FSC mode 8）。增加
   `CAP_SV39`，并实现标准 Sv39 的三级页表遍历。
2. FSC 的 PPN 使用 ATP PPN 字段 bits 43:0，不是 DDTP/DTE 的 bits 53:10。修正掩码并
   保持 root PPN 的页移位只做一次。
3. RISC-V IOMMU 的 DDT、命令队列和 fault queue walker 是设备内部访问，应直接访问系统
   address space，不能使用 IOMMU PCI endpoint 的 DMA 接口（后者受 PCI Bus Master 位约束）。
4. FDT 的 `iommu-map` 改为 QEMU virt 使用的单段 `<0 iommu 0 0x10000>`，覆盖全部 PCI
   requester ID。原来的零长度首段会使仅有 virtio-blk 的启动路径无法正确建立 DMA 域。
5. 启用 IOMMU 时，根总线的 function 0 由 IOMMU endpoint 占用；直接挂根总线的 virtio-blk
   改用 function 1，避免设备地址冲突。

## 验证

以下测试均在远端 RISC-V KVM 主机上执行，使用增量编译：

```text
./build/tests/iommu-context
./build/tests/iommu-device
make check-linux-iommu-v6-pcie-switch-net
make check-linux-6.18.3-pcie-switch-net
make check-linux-7.3-rc2-pcie-switch-net
```

另外增加了 `iommu-pcie-switch-net-pressure` 集成模式，对 `/dev/vda` 连续执行 64 轮
4 KiB 块写入（每轮 64 个块），用于验证 translated DMA 在实际 virtio-blk 请求下可持续工作。
测试结果为 0，输出 `VART_IOMMU_DMA_PRESSURE_OK`。

使用相同压力测试分别运行 4 vCPU 两次，均通过并输出同一标记：

```text
make check-linux-iommu-v6-pcie-switch-net-pressure-4vcpu
run 1: ok - boot Linux with 4 vCPUs and clean up 1 time
run 2: ok - boot Linux with 4 vCPUs and clean up 1 time
```

这验证了当前共享 IOMMU context、PCI DMA 路径和 virtio 中断路径在多 vCPU
启动及并发运行环境下没有立即出现错误；它还不是长时间、多设备并发压力基准。

这些问题均为 VART 当前实现的问题，不是 Linux 或 QEMU 上游错误。
