# H3C-NX30-Uboot

**H3C NX30 家族双系统 U-Boot**

基于 [chenxin527/uBootKit](https://github.com/chenxin527/uboot-qsdk12.5-build)，为 NX30 家族相关硬件维护双系统启动、槽位管理和网页恢复功能。非 H3C 官方项目。

目前主要维护 NX30、RT3000、RX3000、RC3000、RW3000 的相关硬件变体。适配按具体硬件、NAND/ECC 和启动链核验，详见[硬件范围](docs/HARDWARE.md)。

**首轮先提供 U-Boot 源码与测试候选；BootBridge 配套工具后续在本仓库维护和发布。**

[首个预发布说明](releases/uboot-v0.1.0-pre.1.md) · [适配范围](docs/HARDWARE.md) · [验证记录](docs/VALIDATION.md) · [构建说明](docs/BUILDING.md) · [工具进度](tools/bootbridge/README.md)

## 项目功能

- **双系统管理**：查看两个系统槽及已识别的固件信息，提供启动与默认槽管理入口。
- **网页维护与恢复**：在 U-Boot 中查看系统、槽位和存储信息，执行当前硬件路径允许的维护操作。
- **按硬件分别适配**：共用网页与核心代码，保留 HW-A/B/C/D 各自的配置、镜像与兼容检查。

功能入口以对应硬件路径的实际条件为准；本轮执行过的操作见[验证记录](docs/VALIDATION.md)。

## 当前预发布状态

| 对象 | 本轮状态 |
|---|---|
| HW-D 精确候选 | 在一台 NX30 测试样机上完成有限完整流程验证：Winbond EF:BA:21、spare64/ECC4、R012 SBL |
| HW-A / HW-B / HW-C 候选 | 已完成离线构建与包核对，本轮实机验收待测试者完成 |
| BootBridge 配套工具 | 本次不提供下载；官方固件获取、OTA 和界面改进后续在同仓推进 |

> **安装前请先阅读：** 本次 APPSBL 分区 BIN 不能直接上传原厂升级页。本轮不提供普通用户的首次安装工具；普通用户请等待后续工具和对应指南。测试者仅按覆盖自己精确样机的已有安装、备份与恢复流程操作。

HW-A/B/C/D 是项目内部分类，用于源码和候选追溯。它不是 H3C 官方版型，也不是供用户凭型号或外观猜选镜像的菜单。[安装说明](docs/INSTALLATION.md) · [NAND 与 ECC](docs/NAND-ECC.md)

## 获取源码与参与测试

首个[预发布说明](releases/uboot-v0.1.0-pre.1.md)列出候选内容、精确摘要和实测范围。配套安装工具后续在本仓库发布。

构建者从[构建说明](docs/BUILDING.md)开始，准备兼容 ARM 工具链。自行重建的镜像需按自己的文件摘要重新验证，不能直接继承已发布候选的硬件结论。

测试反馈请使用[测试记录模板](docs/TEST-REPORT-TEMPLATE.md)，附版本、镜像 SHA-256 和去敏结果。

## 仓库组织

| 目录或文件 | 用途 |
|---|---|
| `u-boot-2016/` | U-Boot 主源码；保留上游目录布局 |
| `build-release.sh`、`release-tools/` | H3C 四类候选的构建入口和独立封装工具 |
| `docs/` | 硬件范围、构建、验证、安装与测试文档 |
| `releases/`、`release-metadata/` | 发布说明、候选摘要和去敏验证记录 |
| `tools/bootbridge/` | 配套工具入口；本轮只有进度说明 |

源码、文档与配套工具的组织方式见[完整目录说明](docs/REPOSITORY-STRUCTURE.md)。

## 上游与许可

感谢 [chenxin527/uboot-qsdk12.5-build](https://github.com/chenxin527/uboot-qsdk12.5-build) 及其上游贡献者。H3C 家族的适配、双系统管理与相关增强在本项目中维护。

U-Boot 和第三方组件保留原有许可证与版权；新增构建和封装代码遵循各自声明。详见 [LICENSE](LICENSE) 和[许可说明](LICENSE-NOTICE.md)。

## 致谢

感谢 [ayu1808](https://github.com/ayu1808)（恩山同名）为本项目提供适配思路，并参与了大量测试。
