# H3C-NX30-Uboot v0.1.0-pre.7（预发布）

> [!WARNING]
> 本版仍是测试版。改写启动程序有变砖风险；没有串口等独立恢复手段的用户请谨慎操作。

## 概要

| 项目 | 内容 |
|---|---|
| 源码提交 | `6d377e7d` |
| 候选 | HW-A / HW-B / HW-C / HW-D × 21 个 NAND 配置，共 84 个；每个候选附带一个 RAM 救援版 |
| 附件 | `uboot-v0.1.0-pre.7-HW-A.zip` … `HW-D.zip`，内含 `HW-<组>/<NAND 型号>-ECC<位数>/appsbl.mbn` 和 `ram-rescue.elf` |
| APPSBL 分区 | 每个 `0x140000`（1,310,720 字节） |
| 工具链 | OpenWrt GCC 8.4.0 `r16847-f8282da11e` + binutils 2.34 |

全部摘要见 [`CANDIDATES.json`](https://github.com/FlyFish-go/H3C-NX30-Uboot/blob/main/release-metadata/uboot-v0.1.0-pre.7/CANDIDATES.json) 与 [`SHA256SUMS.txt`](https://github.com/FlyFish-go/H3C-NX30-Uboot/blob/main/release-metadata/uboot-v0.1.0-pre.7/SHA256SUMS.txt)。

## 相对 pre.6 的变化

- **可以直接启动 OpenWrt 官方（上游）固件**：上游 OpenWrt 的 qualcommax 镜像用 SMEM 分区名定位系统（`qcom,smem-part`），以前 U-Boot 传给内核的 `ubi.mtd` 与之对不上，需要手动启动。现在 U-Boot 识别这类内核，按实际启动的槽传入正确的分区名，并把本机 NAND 的 ECC 强度写进设备树。QSDK 和原厂固件的启动参数不变。
- **救援页接受上游 OpenWrt 的 factory.ubi**：卷名为 `rootfs`（没有 `ubi_rootfs`）的 UBI 镜像现在可以校验和写入。
- **ARM64 内核不再提示「可能无法启动」**：IPQ50xx 是 Cortex-A53，本 U-Boot 能启动 AArch64 内核（上游 OpenWrt 镜像就是 ARM64）。
- **HW-D**：启动链的 NAND 读取按加载 ECC 进行（混合 ECC 的机器）；没有 SPI-NOR 节点的通用 QSDK 内核也能启动。

双系统分区表、大分区、原厂包白名单、U-Boot 更新等其他行为与 pre.6 相同。

## 从 pre.3–pre.6 升级

与 [pre.6 说明](uboot-v0.1.0-pre.6.md#从-pre3pre4-或-pre5-升级) 相同：救援页「U-Boot 更新」上传同一硬件组、同一 NAND 配置和 ECC 的 `appsbl.mbn`，两份 APPSBL 一起写入，断电重启确认。HW-D 的 ECC8 机器若仍在 pre.5 或更早，先用 RAM 救援版。

## 验证状态

- **离线**：84/84 个候选与 84/84 个 RAM 救援版构建通过（`dirty=false`，嵌入提交 `6d377e7d`）；新增主机测试：真实的上游 OpenWrt factory.ubi 被接受，截断、缺内核、EC CRC 损坏的副本被拒绝。
- **实机（RAM 救援版，未写 flash）**：

  | 样机 | 候选 | 测试内容 |
  |---|---|---|
  | HW-A | W25N01GWZEIG ECC4 | 默认槽启动上游 OpenWrt（`ubi.mtd=rootfs`，设备树 ECC 4/512）；另一槽 QSDK r9 启动参数与之前逐字节一致；救援页校验上游 factory.ubi（arm64，无平台提醒）和 QSDK 镜像通过，截断镜像被拒 |
  | HW-B | GD5F1GQ5REYIG ECC8 | 默认槽启动上游 OpenWrt（ECC 8/512）；另一槽原厂 RT3000 R015 正常启动；救援页校验同上 |
  | HW-C | W25N01GWZEIG ECC4 | 默认槽启动上游 OpenWrt；另一槽原厂 RT3000 R015 正常启动；救援页校验同上 |
  | HW-D（NX30） | GD5F1GQ5REYIG ECC8 | 默认槽直接启动上游 OpenWrt NX30（不再需要手动 bootm；ECC 8/512）；另一槽 QSDK r9 正常启动；救援页校验上游 factory.ubi 和 QSDK 镜像通过，截断镜像被拒 |

- **实机（写入 flash）**：HW-A（W25N01GWZEIG ECC4）从 pre.6 时期的构建（`a67e4f63`）经救援页「U-Boot 更新」升级：校验通过后写入 `0:APPSBL` 和 `0:APPSBL_1`，读回两份都与发布的 `appsbl.partition.bin` 一致；断电冷启动直接引导上游 OpenWrt（`ubi.mtd=rootfs`，ECC 4），随后 10 次冷启动全部正常。另一硬件组的 `appsbl.mbn` 被拒绝。
- **未验证**：HW-B/C/D 未在本版写入 flash（只做了 RAM 救援版测试）；其余候选只有离线构建结论。

## 已知限制

同 pre.6。另：救援页不接受 OpenWrt 的 H3C 网页升级包（`factory-*.bin`，报「unknown H3C OEM package」），请用 UBI 镜像。上游 OpenWrt 的第一个 HW-A/B/C/D 设备支持尚未合入 OpenWrt 主线，官方快照里还没有这些机型的镜像。
