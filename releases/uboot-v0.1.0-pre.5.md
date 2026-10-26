# H3C-NX30-Uboot v0.1.0-pre.5（预发布）

> [!WARNING]
> 本版仍是测试版。改写启动程序有变砖风险；没有串口等独立恢复手段的用户请谨慎操作。

## 概要

| 项目 | 内容 |
|---|---|
| 源码提交 | `01f0ee3d` |
| 候选 | HW-A / HW-B / HW-C / HW-D × 21 个 NAND 配置，共 84 个；每个候选附带一个 RAM 救援版 |
| 附件 | `uboot-v0.1.0-pre.5-HW-A.zip` … `HW-D.zip`，内含 `HW-<组>/<NAND 型号>-ECC<位数>/appsbl.mbn` 和 `ram-rescue.elf` |
| APPSBL 分区 | 每个 `0x140000`（1,310,720 字节） |
| 工具链 | OpenWrt GCC 8.4.0 `r16847-f8282da11e` + binutils 2.34 |

全部摘要见 [`CANDIDATES.json`](../release-metadata/uboot-v0.1.0-pre.5/CANDIDATES.json) 与 [`SHA256SUMS.txt`](../release-metadata/uboot-v0.1.0-pre.5/SHA256SUMS.txt)。

## 相对 pre.4 的变化

修复 HW-C 和 HW-D 在「大分区」（PTABLE 页生成的单一 80 MiB 系统分区）下不能用的两个问题。HW-A、HW-B 的代码没有变化。

- **HW-D（NX30）**：切到大分区后，救援页「固件更新」写入系统分区失败（`write or full readback failed`，errno -30）。原因是 HW-D 的闪存写入范围只登记了两个 40 MiB 系统槽。现在也接受大分区的 `0x900000` + `0x5000000`；写入前仍要求当前分区表确实是大分区。
- **HW-C**：大分区里刷入第三方系统后，开机停在救援页（串口只显示 `bootipq failed`）。原因是 HW-C 遇到带分区表的第三方系统时，只接受 40 MiB 的系统分区。现在当 U-Boot 读到的分区表本身是大分区时，也接受 80 MiB 的 `rootfs`。
- 双系统分区表下的行为与 pre.4 相同。

pre.4 的 HW-C、HW-D 如果已经切到大分区：救援页本身仍可进入。可以在「分区表更新」页写回「原厂双系统」，或者按下面的步骤升级到本版后继续使用大分区。

## 从 pre.3 或 pre.4 升级

1. 进入救援页（断电后按住 Reset 再通电），电脑用网线接 **LAN** 口，打开 `http://192.168.10.1`。
2. 在「U-Boot 更新」上传与本机对应的 `appsbl.mbn`（硬件组与 NAND 型号、ECC 与原来选的相同），确认写入。两份 APPSBL 会一起更新。
3. 断电重启，确认能正常进入系统。

救援页只接受与当前运行版本同一硬件组、同一 NAND 配置和 ECC 的构建，选错会被拒绝。不需要重刷系统固件。

**比 pre.3 更旧的版本**（pre.1、pre.2，BootBridge pre.1–pre.6 安装的 U-Boot）：按 [pre.3 说明](uboot-v0.1.0-pre.3.md#从旧版升级)的步骤升级，只是把文件换成本版的 `ram-rescue.elf` 和 `appsbl.mbn`；升级后同样要再刷一次固件。

**还是原厂系统的设备**：用 [BootBridge](https://flyfish-go.github.io/H3C-NX30-Uboot/) 安装。BootBridge v0.2.0-pre.3（规则 BB2-R3）起安装的就是本版；用 v0.2.0-pre.2 装上 pre.4 的设备，按上面的步骤升级即可。

## 验证状态

- **离线**：84/84 个候选与 84/84 个 RAM 救援版构建通过；同一候选的两个版本身份一致；二进制中无构建机路径，嵌入的源码提交为 `01f0ee3d`。HW-D 写入范围的主机测试新增大分区用例。
- **实机**：四台样机都从 pre.4 经救援页「U-Boot 更新」升级到本版（两份 APPSBL），各一个 NAND 配置，冷启动显示新版本号：

  | 样机 | 候选 | 大分区 |
  |---|---|---|
  | HW-A | W25N01GWZEIG ECC4 | pre.4 下已通过（本版 HW-A 代码未变） |
  | HW-B | GD5F1GQ5REYIG ECC8 | pre.4 下已通过（本版 HW-B 代码未变） |
  | HW-C | W25N01GWZEIG ECC4 | 本版通过 |
  | HW-D（NX30） | W25N01GWZEIG ECC4 | 本版通过 |

  大分区的测试用未公开的 QSDK 测试镜像：救援页把默认槽切到第 1 区，在「分区表更新」页写入大分区，在「固件更新」写入系统；内核收到 `h3c.boot_slot=0x900000 h3c.boot_size=0x5000000`，系统分区为 80 MiB；系统内升级并保留配置；连续冷启动 5 次。之后写回原厂双系统分区表、写回原来两个系统槽的备份，读出与备份逐字节一致。
- **未验证**：其余 80 个候选只有离线构建结论。

## 已知限制

- 其余候选只有离线构建结论。ECC8 的 NX30（GD5F1GQ5REYIG）仍未实机验证。
- 大分区会抹掉原厂系统所在的区，只适合第三方系统；原厂固件在大分区下会被拒绝。用大分区前先在「闪存备份」备份两个系统槽。
- 原厂 SBL 在异常复位后可能改用另一份 APPSBL，所以升级时两份必须一起写。

## 构建

见[构建说明](../docs/BUILDING.md)。重建时传入 `SOURCE_REVISION=01f0ee3dba7171fa4f8240b93e69b67a6f126910`。RAM 救援版用 `H3C_RAM_RESCUE=1 ./build-release.sh …` 构建。
