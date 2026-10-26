# H3C-NX30-Uboot v0.1.0-pre.4（预发布）

> [!WARNING]
> 本版仍是测试版。改写启动程序有变砖风险；没有串口等独立恢复手段的用户请谨慎操作。

## 概要

| 项目 | 内容 |
|---|---|
| 源码提交 | `2f03aeaa` |
| 候选 | HW-A / HW-B / HW-C / HW-D × 21 个 NAND 配置，共 84 个；每个候选附带一个 RAM 救援版 |
| 附件 | `uboot-v0.1.0-pre.4-HW-A.zip` … `HW-D.zip`，内含 `HW-<组>/<NAND 型号>-ECC<位数>/appsbl.mbn` 和 `ram-rescue.elf` |
| APPSBL 分区 | 每个 `0x140000`（1,310,720 字节） |
| 工具链 | OpenWrt GCC 8.4.0 `r16847-f8282da11e` + binutils 2.34 |

全部摘要见 [`CANDIDATES.json`](../release-metadata/uboot-v0.1.0-pre.4/CANDIDATES.json) 与 [`SHA256SUMS.txt`](../release-metadata/uboot-v0.1.0-pre.4/SHA256SUMS.txt)。

## 相对 pre.3 的变化

- 启动系统时，除了 `h3c.boot_slot=<偏移>`，还会把所启动系统分区的大小以 `h3c.boot_size=<大小>` 传给 Linux。双系统分区表下是 `0x2800000`（40 MiB），单一大分区表下是 `0x5000000`（80 MiB）。两个参数要么一起追加，要么都不追加。
- 用途：大分区和“双系统、默认槽在第 1 区”这两种情况，`h3c.boot_slot` 都是 `0x900000`，Linux 单凭它分不清。之后的 QSDK 固件会据此在两种分区表下都能使用；**大分区需要 U-Boot pre.4 或更新**。
- 原厂系统不读这两个参数，启动不受影响。
- 其余代码与 pre.3 相同。

## 从 pre.3 升级

1. 进入救援页（断电后按住 Reset 再通电），电脑用网线接 **LAN** 口，打开 `http://192.168.10.1`。
2. 在「U-Boot 更新」上传与本机对应的 `appsbl.mbn`（硬件组与 NAND 型号、ECC 与 pre.3 时选的相同），确认写入。两份 APPSBL 会一起更新。
3. 断电重启，确认能正常进入系统。

pre.3 救援页只接受与当前运行版本同一硬件组、同一 NAND 配置和 ECC 的构建，选错会被拒绝。不需要重刷系统固件。

**比 pre.3 更旧的版本**（pre.1、pre.2，BootBridge pre.1–pre.6 安装的 U-Boot）：按 [pre.3 说明](uboot-v0.1.0-pre.3.md#从旧版升级)的步骤升级，只是把文件换成本版的 `ram-rescue.elf` 和 `appsbl.mbn`；升级后同样要再刷一次固件。

**还是原厂系统的设备**：用 [BootBridge](https://flyfish-go.github.io/H3C-NX30-Uboot/) 安装。BootBridge v0.2.0-pre.2（规则 BB2-R2）起安装的就是本版；用 v0.2.0-pre.1 装上 pre.3 的设备，按上面的步骤升级即可。

## 验证状态

- **离线**：84/84 个候选与 84/84 个 RAM 救援版构建通过；同一候选的两个版本身份一致；二进制中无构建机路径，嵌入的源码提交为 `2f03aeaa`。启动参数主机测试（ASan/UBSan）覆盖双系统、大分区和空间不足三种情况。
- **实机**：四台样机都从 pre.3 经救援页「U-Boot 更新」升级到本版，各一个 NAND 配置：

  | 样机 | 候选 | 内核收到的参数 | 结果 |
  |---|---|---|---|
  | HW-A | W25N01GWZEIG ECC4 | `h3c.boot_slot=0x900000 h3c.boot_size=0x2800000`（QSDK） | 通过 |
  | HW-B | GD5F1GQ5REYIG ECC8 | 未读取（原厂系统） | 通过 |
  | HW-C | W25N01GWZEIG ECC4 | `h3c.boot_slot=0x900000 h3c.boot_size=0x2800000`（原厂系统） | 通过 |
  | HW-D（NX30） | W25N01GWZEIG ECC4 | `h3c.boot_slot=0x3100000 h3c.boot_size=0x2800000`（原厂系统） | 通过 |

  每台：救援页验证并写入两份 APPSBL，读回两份都与发布摘要一致；冷启动显示新版本号，无 panic、无反复重启，原厂系统和 QSDK 都正常进入。
- **未验证**：大分区下的启动参数（`0x5000000`）只有主机测试；其余 80 个候选只有离线构建结论。

## 已知限制

- 其余候选只有离线构建结论。ECC8 的 NX30（GD5F1GQ5REYIG）仍未实机验证。
- 本版只增加启动参数；大分区下 U-Boot 传出的 `h3c.boot_size=0x5000000` 只有离线测试，需等支持大分区的 QSDK 一起实机验证。
- 原厂 SBL 在异常复位后可能改用另一份 APPSBL，所以升级时两份必须一起写。

## 构建

见[构建说明](../docs/BUILDING.md)。重建时传入 `SOURCE_REVISION=2f03aeaa3054fa4dec234844cea026efde93bcba`。RAM 救援版用 `H3C_RAM_RESCUE=1 ./build-release.sh …` 构建。
