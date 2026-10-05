# QEMU RISC-V IOMMU IOATC 测试复现手册

本文记录 RISC-V IOMMU IOATC 回收补丁使用的 qtest 和 fio 测试流程。
命令默认在 RISC-V KVM 服务器的
`/home/tc/riscv-linux-lab` 目录执行。

## 1. 测试对象

测试使用下面两个版本做 A/B 对比：

```text
baseline: v11.1.2
patched:  riscv-iommu-ioatc-v11.1.2
commit:   402e9a005c hw/riscv: Reclaim invalidated IOMMU IOATC entries
```

不要使用 `v11.1.0-rc3` 复现性能数据。该版本缺少下面两个上游
RISC-V IOMMU page-walk 修复，实验内核会报告 Fault 13，并在 NVMe
Identify Controller 阶段超时：

```text
c75952b065 hw/riscv/riscv-iommu: fix U-bit check to apply only to leaf S/VS-stage PTEs
292af62ed0 hw/riscv/riscv-iommu: preserve requested perm in spa_fetch()
```

公共路径：

```sh
LAB=/home/tc/riscv-linux-lab
QEMU=$LAB/qemu
BUILD=$QEMU/build-v11.1.2
KERNEL=$LAB/linux-build-iommu-v6/arch/riscv/boot/Image
```

确认当前补丁版源码和二进制：

```sh
git -C "$QEMU" switch riscv-iommu-ioatc-v11.1.2
"$BUILD/qemu-system-riscv64" --version
```

期望版本包含：

```text
v11.1.2-1-g402e9a005c
```

## 2. qtest

只增量构建 RISC-V system emulator 和 IOMMU qtest：

```sh
ninja -C "$BUILD" \
    qemu-system-riscv64 \
    tests/qtest/iommu-riscv-test
```

运行定向测试：

```sh
"$BUILD/pyvenv/bin/meson" test \
    -C "$BUILD" \
    --print-errorlogs \
    'qtest-riscv64/iommu-riscv-test'
```

本轮 v11.1.2 测试结果为 4 个子测试全部通过。上游 v11.1.2 的测试
尚未包含 selective invalidation 回收测试，因此该结果只能证明基础功能
没有回归，不能单独证明失效 entry 已被删除。

如使用包含扩展 qtest 的 IOATC 开发分支，应额外运行：

```text
/riscv64/iommu-testdev/cache/selective-inval-reclaims-entry
```

## 3. 准备发行版 fio

服务器软件源提供原生 RISC-V `fio 3.41`。下面的命令只下载和解包，
不会向宿主系统安装软件包：

```sh
cd "$LAB"
mkdir -p fio-guest-package
cd fio-guest-package

apt download fio
apt download \
    libaio1t64 libgfapi0 libnbd0 libnfs14 \
    librdmacm1t64 librados2 librbd1
apt download libgfrpc0 libgfxdr0 libglusterfs0

mkdir -p root
for deb in ./*.deb; do
    dpkg-deb -x "$deb" root
done
```

复制宿主已有的动态库依赖：

```sh
LD_LIBRARY_PATH=./root/usr/lib/riscv64-linux-gnu:\
./root/usr/lib/riscv64-linux-gnu/ceph \
ldd ./root/usr/bin/fio |
awk '/=> \/(usr\/)?lib/ { print $3 }' |
sort -u |
xargs -r cp -L --parents -t root

mkdir -p root/lib
cp -L /lib/ld-linux-riscv64-lp64d.so.1 \
    root/lib/ld-linux-riscv64-lp64d.so.1
```

再次检查时不应出现 `not found`：

```sh
LD_LIBRARY_PATH=./root/usr/lib/riscv64-linux-gnu:\
./root/usr/lib/riscv64-linux-gnu/ceph \
ldd ./root/usr/bin/fio
```

## 4. 制作 fio initramfs

测试入口脚本位于：

```text
tests/integration/qemu/riscv-iommu-ioatc-fio-init.sh
```

以现有 BusyBox rootfs 为基础制作 initramfs：

```sh
cd "$LAB"
mkdir -p fio-guest-rootfs
cp -a rootfs/. fio-guest-rootfs/
cp -a fio-guest-package/root/. fio-guest-rootfs/
cp vart/tests/integration/qemu/riscv-iommu-ioatc-fio-init.sh \
    fio-guest-rootfs/init
chmod 0755 fio-guest-rootfs/init fio-guest-rootfs/usr/bin/fio

cd fio-guest-rootfs
find . -print0 |
    cpio --null -o --format=newc 2>/dev/null |
    gzip -1 > ../fio-ioatc-rootfs.cpio.gz
```

生成的 initramfs 约为 24 MiB。不要提交下载的软件包、动态库或生成的
initramfs。

## 5. 短时 fio 吞吐测试

短时测试参数：

```text
file size: 128 MiB/job
jobs:      4
block:     4 KiB
rw:        30% read / 70% write
ioengine:  libaio
direct:    1
iodepth:   64/job
runtime:   60 seconds
```

使用 1 GiB ext4 测试盘 `disk.img`。QEMU 命令如下：

```sh
timeout 150 "$BUILD/qemu-system-riscv64" \
    -machine virt,aia=aplic-imsic \
    -accel kvm \
    -cpu host \
    -m 1G \
    -smp 4 \
    -nographic \
    -kernel "$KERNEL" \
    -initrd "$LAB/fio-ioatc-rootfs.cpio.gz" \
    -append 'console=ttyS0 rdinit=/init nokaslr iommu.strict=0' \
    -device riscv-iommu-pci \
    -drive file="$LAB/disk.img",if=none,format=raw,id=blk0 \
    -device nvme,drive=blk0,serial=fio-ioatc \
    > /tmp/fio-v11.1.2-patched.log 2>&1
```

本轮单次结果：

| 指标 | 原始 v11.1.2 | 补丁版 |
|---|---:|---:|
| 读 IOPS | 7,713 | 8,007 |
| 写 IOPS | 约 18.0k | 约 18.7k |
| 总带宽 | 100.6 MiB/s | 104.5 MiB/s |

单轮提升约 4%。正式统计应正反顺序各运行至少 3 次，计算均值和标准差。

## 6. 长时大工作集 fio 测试

创建独立的 8 GiB 稀疏 ext4 测试盘，不要覆盖 `disk.img`：

```sh
cd "$LAB"
truncate -s 8G fio-disk-8g.img
mkfs.ext4 -F -q fio-disk-8g.img
```

长时参数：

```text
file size:      1 GiB/job
jobs:           4
total working:  4 GiB
block:          4 KiB
rw:             30% read / 70% write
ioengine:       libaio
direct:         1
iodepth:        4/job
runtime:        300 seconds
status interval: 10 seconds
```

QEMU 命令：

```sh
timeout 380 "$BUILD/qemu-system-riscv64" \
    -machine virt,aia=aplic-imsic \
    -accel kvm \
    -cpu host \
    -m 1G \
    -smp 4 \
    -nographic \
    -kernel "$KERNEL" \
    -initrd "$LAB/fio-ioatc-rootfs.cpio.gz" \
    -append 'console=ttyS0 rdinit=/init nokaslr iommu.strict=0' \
    -device riscv-iommu-pci \
    -drive file="$LAB/fio-disk-8g.img",if=none,format=raw,id=blk0 \
    -device nvme,drive=blk0,serial=fio-long \
    > /tmp/fio-long-strict0-patched.log 2>&1
```

补丁版结果：

```text
file layout: about 50 seconds
fio runtime: 300.072 seconds
read:         5,804 IOPS, 22.7 MiB/s
write:        about 13.5k IOPS, 52.9 MiB/s
fio status:   0
host total:   351 seconds
```

原始 v11.1.2 在 380 秒上限内只完成约 140 秒正式 randrw：

```text
file layout: about 230 seconds
read at 140s: 4,702 IOPS
write at 140s: about 11.0k IOPS
total BW:     about 61.3 MiB/s
exit:         timeout status 124
```

在可比的 140 秒位置，补丁版约为 19.4k IOPS、75.9 MiB/s，吞吐提升
约 24%；文件布局阶段约快 4.6 倍。

## 7. A/B 切换流程

测试原始 v11.1.2：

```sh
git -C "$QEMU" switch --detach v11.1.2
ninja -C "$BUILD" qemu-system-riscv64
"$BUILD/qemu-system-riscv64" --version
```

期望输出为纯 `v11.1.2`。完成 baseline 后必须恢复补丁分支和二进制：

```sh
git -C "$QEMU" switch riscv-iommu-ioatc-v11.1.2
ninja -C "$BUILD" qemu-system-riscv64
"$BUILD/qemu-system-riscv64" --version
```

期望恢复为：

```text
v11.1.2-1-g402e9a005c
```

切换时只需增量编译 RISC-V IOMMU 相关对象并重新链接，不需要新建工作树
或完整重编译。

## 8. 结果检查

提取 fio 状态和性能：

```sh
grep -E \
    'FIO_IOATC_|read: IOPS|write: IOPS|READ:|WRITE:|err=' \
    /tmp/fio-*.log
```

检查 guest/QEMU 错误：

```sh
grep -Ei \
    'Fault 13|timeout|IOMMU.*error|DMA.*error|nvme.*failed|BUG:|WARNING:' \
    /tmp/fio-*.log
```

成功运行至少应包含：

```text
FIO_IOATC_BOOT
FIO_IOATC_START ...
fio ... err=0
FIO_IOATC_STATUS 0
FIO_IOATC_END ...
```

外部 `timeout` 返回 124 时，需要检查 fio 是否已进入正式测试。不能把 NVMe
初始化失败或 initramfs 等待设备的时间当成性能结果。

## 9. 已知注意事项

- 当前 build 使用 `-O0`、`debug_mutex=true` 和
  `debug_graph_lock=true`。A/B 对比有效，但绝对性能不能与 `-O2` build
  直接比较。
- `fio 3.41` 在本环境中使用 `time_based + verify_backlog +` 深队列时触发
  `inflight_issued` 断言和用户态 double-free。吞吐测试没有启用 verify；
  数据一致性由独立的 `cp + sync + cmp` workload 验证。
- 深队列、128 MiB 小工作集主要受 NVMe 模型饱和限制，单轮只提升约 4%。
  4 GiB 工作集、低队列深度能更清楚地暴露 IOATC tombstone 扫描成本。
- 尚未完成 `iommu.strict=1` 长时 A/B。它预计会产生更密集的
  invalidation，但应作为独立结果记录，不能代替默认 lazy 模式测试。
- 未实现 generation retry 的第一版仍保留上游已有的 page walk 与
  invalidation 排序窗口；性能测试通过不代表该并发语义已经修复。
