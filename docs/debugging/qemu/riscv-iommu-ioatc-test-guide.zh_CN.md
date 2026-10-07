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

## 3. 最初的 IOATC 高 churn 测试

最早观察到明显性能退化的测试不是 fio，而是一个使用 BusyBox 工具的
NVMe 文件复制 workload。测试入口为：

```text
tests/integration/qemu/riscv-iommu-ioatc-tombstone-init.sh
```

测试启动 4 个并发任务。每个任务先从 `/dev/urandom` 创建一个 8 MiB
源文件，然后循环执行 50 次：

```sh
cp /tmp/src$j /mnt/disk/file$j
sync
cmp -s /tmp/src$j /mnt/disk/file$j
echo 3 > /proc/sys/vm/drop_caches
```

每个任务写入 50 次 8 MiB 文件，因此 4 个任务合计写入 1,600 MiB
（约 1.56 GiB），并由 `cmp` 再读取和校验相同的数据量。测试本身不直接
构造 IOTINVAL 命令；`sync`、清理 page cache 以及正常 Linux DMA
map/unmap 路径自然产生大量 selective invalidation。因此它能代表文件
创建、同步和回收频繁的高 churn I/O，又比固定小工作集的深队列 fio 更容易
放大 IOATC tombstone 扫描成本。

### 3.1 制作测试 initramfs

```sh
cd "$LAB"
work_dir=$(mktemp -d /tmp/ioatc-copy.XXXXXX)
cp -a rootfs/. "$work_dir/"
cp vart/tests/integration/qemu/riscv-iommu-ioatc-tombstone-init.sh \
    "$work_dir/init"
chmod 0755 "$work_dir/init"

(
    cd "$work_dir"
    find . -print0 |
        cpio --null -o --format=newc 2>/dev/null |
        gzip -9 > "$LAB/ioatc-tombstone-rootfs.cpio.gz"
)
```

生成成功后可以删除本次创建的临时目录。不要提交生成的 initramfs。

### 3.2 运行补丁版

```sh
start=$(date +%s)
timeout 120 "$BUILD/qemu-system-riscv64" \
    -machine virt,aia=aplic-imsic \
    -accel kvm \
    -cpu host \
    -m 1G \
    -smp 4 \
    -nographic \
    -kernel "$KERNEL" \
    -initrd "$LAB/ioatc-tombstone-rootfs.cpio.gz" \
    -append 'console=ttyS0 rdinit=/init nokaslr iommu.strict=0' \
    -device riscv-iommu-pci \
    -drive file="$LAB/disk.img",if=none,format=raw,id=blk0 \
    -device nvme,drive=blk0,serial=ioatc-test \
    > /tmp/ioatc-copy-patched.log 2>&1
status=$?
end=$(date +%s)
echo "HOST_ELAPSED=$((end - start)) EXIT=$status"
```

成功日志必须包含：

```text
IOATC_TOMBSTONE_BOOT
IOATC_TOMBSTONE_START ...
JOB_1_OK
JOB_2_OK
JOB_3_OK
JOB_4_OK
IOATC_TOMBSTONE_END ...
```

提取结果和错误：

```sh
grep -E 'IOATC_TOMBSTONE_|JOB_|Fault 13|timeout|IOMMU.*[Ee]rror|DMA.*[Ee]rror|nvme.*failed' \
    /tmp/ioatc-copy-patched.log
```

### 3.3 A/B 结果

按照第 8 节的方法切换纯基线和补丁分支，使用完全相同的内核、initramfs、
磁盘和命令。历史结果有两组：

```text
QEMU master 实验分支重测：
  optimized: 32 seconds, all four jobs passed
  baseline:  did not finish within 120 seconds

QEMU v11.1.2：
  optimized: 35 seconds host time, 35 seconds guest workload
  baseline:  168 seconds host time, 167 seconds guest workload
  speedup:   4.8x
```

因此，32 秒指的是最早在 QEMU master IOATC 优化分支上复测得到的宿主
总耗时。升级到 v11.1.2 后，同一测试为 35 秒；二者编译配置和 QEMU
基线不同，不能直接比较绝对时间。2026-10-07 使用相同 v11.1.2 源码、
build 配置、内核、initramfs 和磁盘完成了两边的完整运行：补丁版 35 秒，
原始版本 168 秒，耗时减少约 79%，加速约 4.8 倍。

这个测试既检查性能，也通过 `cmp` 检查数据一致性。外部 timeout 返回
124 只表示达到测试上限；需要根据日志确认 workload 是否已经开始，不能
把 NVMe 初始化失败或等待 `/dev/nvme0n1` 的时间当成性能结果。

## 4. 准备发行版 fio

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

## 5. 制作 fio initramfs

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

## 6. 短时 fio 吞吐测试

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

短测脚本必须让 4 个 job 使用不同文件，例如：

```sh
'--filename_format=fio.$jobnum'
```

必须使用单引号，避免 guest shell 提前展开 `$jobnum`。多个 writer 共用同一
文件并开启 verify 时，fio 会因相互覆盖而收到 SIGABRT；这属于测试脚本
错误，不能作为 QEMU 回归。短测不启用 verify，数据一致性由第 3 节的
`cp + sync + cmp` 单独验证。

仓库中的短测入口为：

```text
tests/integration/qemu/riscv-iommu-ioatc-fio-short-init.sh
```

制作短测 initramfs 时，将第 5 节复制的入口替换成这个脚本，并将输出命名
为 `fio-ioatc-short-rootfs.cpio.gz`；无需重新下载或编译 fio。

使用 8 GiB ext4 测试盘 `fio-disk-8g.img`，避免先前被 timeout 中断的长测
文件填满 1 GiB 的 `disk.img`。短测使用单独封装的
`fio-ioatc-short-rootfs.cpio.gz`。QEMU 命令如下：

```sh
timeout 150 "$BUILD/qemu-system-riscv64" \
    -machine virt,aia=aplic-imsic \
    -accel kvm \
    -cpu host \
    -m 1G \
    -smp 4 \
    -nographic \
    -kernel "$KERNEL" \
    -initrd "$LAB/fio-ioatc-short-rootfs.cpio.gz" \
    -append 'console=ttyS0 rdinit=/init nokaslr iommu.strict=0' \
    -device riscv-iommu-pci \
    -drive file="$LAB/fio-disk-8g.img",if=none,format=raw,id=blk0 \
    -device nvme,drive=blk0,serial=fio-ioatc \
    > /tmp/fio-v11.1.2-patched.log 2>&1
```

2026-10-07 每个版本连续运行 3 次，读写带宽相加后结果为：

| 运行 | 原始 v11.1.2 | 补丁版 |
|---|---:|---:|
| 1 | 104.2 MiB/s | 104.8 MiB/s |
| 2 | 103.1 MiB/s | 102.8 MiB/s |
| 3 | 105.7 MiB/s | 105.5 MiB/s |
| 中位数 | 104.2 MiB/s | 104.8 MiB/s |

中位数差约 +0.6%，小于样本自身约 1--2% 的波动，说明常规深队列、小
工作集负载没有可测的大规模回退。更严格的统计仍应交替 A/B 顺序并增加
样本数量。

## 7. 长时大工作集 fio 测试

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
timeout 700 "$BUILD/qemu-system-riscv64" \
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

2026-10-07 使用完全相同命令完成两边的 300 秒稳态测试：

| 指标 | 原始 v11.1.2 | 补丁版 |
|---|---:|---:|
| 读 IOPS | 5,261 | 5,759 |
| 写 IOPS | 约 12.3k | 约 13.4k |
| 读带宽 | 20.6 MiB/s | 22.5 MiB/s |
| 写带宽 | 48.0 MiB/s | 52.5 MiB/s |
| 总带宽 | 68.6 MiB/s | 75.0 MiB/s |
| 宿主总耗时 | 355 秒 | 345 秒 |
| fio 状态 | 0 | 0 |

补丁版稳态总带宽提升约 9.3%，宿主总耗时缩短约 2.8%。该测试没有直接
注入 invalidation，只通过常见的 4 KiB `randrw`、`libaio`、direct I/O
和 Linux DMA API 产生正常的映射及回收流量。收益小于第 3 节的高 churn
测试，符合 tombstone 扫描成本随 invalidation churn 增长的预期。

## 8. A/B 切换流程

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

## 9. 结果检查

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

## 10. 已知注意事项

- 当前 build 使用 `-O0`、`debug_mutex=true` 和
  `debug_graph_lock=true`。A/B 对比有效，但绝对性能不能与 `-O2` build
  直接比较。
- `fio 3.41` 在本环境中使用 `time_based + verify_backlog +` 深队列时触发
  `inflight_issued` 断言和用户态 double-free。吞吐测试没有启用 verify；
  数据一致性由独立的 `cp + sync + cmp` workload 验证。
- 深队列、128 MiB 小工作集主要受 NVMe 模型饱和限制，三轮中位数差仅
  +0.6%。4 GiB 工作集、低队列深度的完整 A/B 提升约 9.3%；频繁同步、
  回收 DMA 映射的高 churn 测试提升约 4.8 倍。
- 尚未完成 `iommu.strict=1` 长时 A/B。它预计会产生更密集的
  invalidation，但应作为独立结果记录，不能代替默认 lazy 模式测试。
- 未实现 generation retry 的第一版仍保留上游已有的 page walk 与
  invalidation 排序窗口；性能测试通过不代表该并发语义已经修复。
