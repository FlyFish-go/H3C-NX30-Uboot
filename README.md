# H3C-NX30-Uboot

**H3C NX30 家族双系统 U-Boot**

基于 [chenxin527/uBootKit](https://github.com/chenxin527/uboot-qsdk12.5-build)，为 NX30 家族相关硬件维护双系统启动、槽位管理和网页恢复功能。非 H3C 官方项目。

目前主要维护 NX30、RT3000、RX3000、RC3000、RW3000 的相关硬件变体。适配按具体硬件、NAND/ECC 和启动链核验，详见[硬件范围](docs/HARDWARE.md)。

> [!CAUTION]
> **用以前版本的救援页「固件更新」刷过固件的设备，系统分区会逐渐出现无法纠正的 ECC 错误，严重时开机停在救援页、写入全被拒绝。** 请按 [U-Boot pre.3 说明](releases/uboot-v0.1.0-pre.3.md#从旧版升级)升级 U-Boot，然后再刷一次固件。

**当前测试版：U-Boot v0.1.0-pre.5 与 BootBridge v0.2.0-pre.2（规则 BB2-R2，安装 pre.4），正在由测试人员上机测试，尚未取得整机验收结论。** 已装过本项目 U-Boot 的设备按 pre.5 说明升级；还是原厂系统的设备用 [BootBridge 网页版](https://flyfish-go.github.io/H3C-NX30-Uboot/)安装，再升级到 pre.5。

[U-Boot pre.5 说明](releases/uboot-v0.1.0-pre.5.md) · [U-Boot pre.4 说明](releases/uboot-v0.1.0-pre.4.md) · [U-Boot pre.3 说明](releases/uboot-v0.1.0-pre.3.md) · [BootBridge v0.2.0-pre.2 说明](https://github.com/FlyFish-go/H3C-NX30-Uboot/releases/tag/bootbridge-v0.2.0-pre.2) · [适配范围](docs/HARDWARE.md) · [验证记录](docs/VALIDATION.md) · [构建说明](docs/BUILDING.md) · [BootBridge](tools/bootbridge/README.md)

## 项目功能

- **双系统管理**：查看两个系统槽及已识别的固件信息，提供启动与默认槽管理入口。
- **网页维护与恢复**：在 U-Boot 中查看系统、槽位和存储信息，执行当前硬件路径允许的维护操作。
- **按硬件分别适配**：共用网页与核心代码，保留 HW-A/B/C/D 各自的配置、镜像与兼容检查。

功能入口以对应硬件路径的实际条件为准；本轮执行过的操作见[验证记录](docs/VALIDATION.md)。

## HW-A/B/C/D 分类与当前候选

| HW 工程组 | 主要实物结构 / OEM 代表 machid | U-Boot `v0.1.0-pre.5` | QSDK 11.5 的现有进展 |
|---|---|---|---|
| [**HW-A**](docs/HARDWARE.md#hw-a) | 长板；IPQ5018 / 外置 DDR / QCA8337；`0x08040001` | 84 候选之一；W25N01GWZEIG ECC4 一台样机实测通过 | 当前未建立独立整机验收 |
| [**HW-B**](docs/HARDWARE.md#hw-b) | 短板；IPQ5000 / 集成 DDR / QCA8337；`0x08040000` | 84 候选之一；GD5F1GQ5REYIG ECC8 一台样机实测通过 | RT3000 B 的 Product R1 r5 阶段实测 |
| [**HW-C**](docs/HARDWARE.md#hw-c) | 长板；IPQ5018 / 外置 DDR / RTL8367S；`0x08040101` | 84 候选之一；W25N01GWZEIG ECC4 一台样机实测通过 | RT3000 C 的 Native08 已持久安装、有界验证；仍有开放项 |
| [**HW-D**](docs/HARDWARE.md#hw-d) | NX30 型长板；IPQ5000 / 集成 DDR / QCA8337；`0x08040000` | 84 候选之一；W25N01GWZEIG ECC4 一台 NX30 实测通过，ECC8 未验证 | 独立构建候选，**尚未取得整机验收结论** |

**B、D 可共用 `0x08040000` / MP02.1；machid 不是四组唯一编码。** 这些是工程分组和候选状态，不是通刷资格。如何依据实物照片、CDT/APPSBL 和控制 FDT 判组，见 [详细硬件与 OEM 依据](docs/HARDWARE.md)；对应 U-Boot 实测门见 [验证记录](docs/VALIDATION.md)。

> **安装前请先阅读：** 本次 APPSBL 分区 BIN 不能直接上传原厂升级页。首次安装请使用 BootBridge。当前为测试版，没有独立恢复手段和自己样机写前备份的用户，请等待测试结论。

HW-A/B/C/D 是项目内部分类，用于源码和候选追溯。它不是 H3C 官方版型，也不是供用户凭型号或外观猜选镜像的菜单。[安装说明](docs/INSTALLATION.md) · [NAND 与 ECC](docs/NAND-ECC.md)

## 获取源码与参与测试

[pre.5 说明](releases/uboot-v0.1.0-pre.5.md)列出候选内容、精确摘要、实测范围和升级步骤；更旧版本的升级步骤见 [pre.3 说明](releases/uboot-v0.1.0-pre.3.md#从旧版升级)。安装工具见 [tools/bootbridge](tools/bootbridge/README.md)。

构建者从[构建说明](docs/BUILDING.md)开始，准备兼容 ARM 工具链。自行重建的镜像需按自己的文件摘要重新验证，不能直接继承已发布候选的硬件结论。

测试反馈请使用[测试记录模板](docs/TEST-REPORT-TEMPLATE.md)，附版本、镜像 SHA-256 和去敏结果。

## 仓库组织

| 目录或文件 | 用途 |
|---|---|
| `u-boot-2016/` | U-Boot 主源码；保留上游目录布局 |
| `build-release.sh`、`release-tools/` | H3C 四类候选的构建入口和独立封装工具 |
| `docs/` | 硬件范围、构建、验证、安装与测试文档 |
| `releases/`、`release-metadata/` | 发布说明、候选摘要和去敏验证记录 |
| `tools/bootbridge/` | BootBridge 源码（Windows 版、网页版、桥接程序）与规则 |

源码、文档与配套工具的组织方式见[完整目录说明](docs/REPOSITORY-STRUCTURE.md)。

## 上游与许可

感谢 [chenxin527/uboot-qsdk12.5-build](https://github.com/chenxin527/uboot-qsdk12.5-build) 及其上游贡献者。H3C 家族的适配、双系统管理与相关增强在本项目中维护。

U-Boot 和第三方组件保留原有许可证与版权；新增构建和封装代码遵循各自声明。详见 [LICENSE](LICENSE) 和[许可说明](LICENSE-NOTICE.md)。

## 致谢

感谢 [ayu1808](https://github.com/ayu1808)（恩山同名）为本项目提供适配思路，并参与了大量测试。
