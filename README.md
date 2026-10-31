<div align="center">

# H3C-NX30-Uboot

**给 H3C NX30 / RT3000 / RC3000 / RW3000 装上带网页救砖和双系统的 U-Boot**

不拆机、不接 TTL：浏览器里生成桥接包，在原厂后台上传，跟着向导就能装好。装完原厂系统照常使用，随时可以回原厂。

[![U-Boot](https://img.shields.io/badge/U--Boot-v0.1.0--pre.6-2a9d8f)](https://github.com/FlyFish-go/H3C-NX30-Uboot/releases/tag/uboot-v0.1.0-pre.6)
[![BootBridge](https://img.shields.io/badge/BootBridge-v0.2.0--pre.3-2a9d8f)](https://github.com/FlyFish-go/H3C-NX30-Uboot/releases/tag/bootbridge-v0.2.0-pre.3)
[![QSDK 固件](https://img.shields.io/badge/QSDK_11.5_固件-r6-e76f51)](https://github.com/FlyFish-go/H3C-RT3000-Product-R1)
[![License](https://img.shields.io/badge/license-GPL--2.0-blue)](LICENSE)
[![QQ群](https://img.shields.io/badge/QQ群-1131957442-12b7f5)](#交流与反馈)
[![Stars](https://img.shields.io/github/stars/FlyFish-go/H3C-NX30-Uboot?style=social)](https://github.com/FlyFish-go/H3C-NX30-Uboot/stargazers)

### [🚀 打开 BootBridge 网页版，开始安装](https://flyfish-go.github.io/H3C-NX30-Uboot/)

[下载 U-Boot](https://github.com/FlyFish-go/H3C-NX30-Uboot/releases/tag/uboot-v0.1.0-pre.6) · [配套 QSDK 11.5 固件](https://github.com/FlyFish-go/H3C-RT3000-Product-R1) · [支持的设备](#支持的设备) · [QQ 群 1131957442](#交流与反馈) · [English](README_EN.md)

</div>

<table>
<tr>
<td width="50%"><img src="docs/images/bootbridge-web.png" alt="BootBridge 网页版：选择机型"></td>
<td width="50%"><img src="docs/images/rescue-overview.png" alt="U-Boot 救援页：系统信息与双系统启动"></td>
</tr>
<tr>
<td align="center">BootBridge 网页版：选机型，自动生成桥接包</td>
<td align="center">U-Boot 救援页：两个系统一键启动、设为默认</td>
</tr>
</table>

## 能做什么

- **🛟 网页救砖**：断电后按住 Reset 再通电，浏览器打开 `192.168.10.1`。刷固件、刷 U-Boot、备份整片闪存、写回 ART / CDT / MIBIB、加载 initramfs，都在网页上完成，不用 TTL。
- **🔀 双系统共存**：原厂系统和第三方系统各占一个槽，在网页上选择启动哪一个、默认启动哪一个。不需要原厂系统时，可以把两个槽合并成一个 80 MiB 大分区。
- **🧰 一键安装**：[BootBridge 网页版](https://flyfish-go.github.io/H3C-NX30-Uboot/)用官方固件生成桥接包，在原厂后台「固件升级」上传即可。固件只在你自己的浏览器里处理，不会上传。
- **↩️ 能回原厂**：安装前先完整备份；桥接系统里随时可以返回原厂，长按 Mesh/WPS 键也行。
- **🔍 自动识别硬件**：识别主板属于哪一类（HW-A/B/C/D）以及 NAND 型号和 ECC，只安装匹配的那一份。救援页也会拒绝写入不匹配的 U-Boot。
- **🔓 开源，可复现**：源码、构建脚本、每个候选的 SHA-256 都公开，可以自己重建核对。

## 支持的设备

| 型号 | 需要的原厂固件版本 | 硬件分类 | 实机测试 |
|---|---|---|---|
| **NX30** / RX3000 | `NX30V100R012` | HW-D | ✅ NX30：全流程 |
| **RT3000** | `RT3000V100R015` | HW-B 或 HW-C | ✅ HW-B、HW-C 各一台：全流程 |
| **RC3000** | `RC3000V100R013L01` | HW-A | ✅ RC3000（512 MB 内存版）：BootBridge 安装与回原厂 |
| **RW3000** / RW3010 | `RW3000V100R012` | HW-A | ✅ RW3000：U-Boot 与 QSDK 固件；BootBridge 识别待更多反馈 |

同一型号可能用不同的主板（例如 RT3000 有 HW-B 和 HW-C 两种），**不用自己判断**，BootBridge 会读取主板信息后自动选择。HW-A/B/C/D 是本项目的内部分类，不是 H3C 官方版型，依据见[硬件说明](docs/HARDWARE.md)。

## 三步安装

**设备还在原厂系统：**

1. 打开 [BootBridge 网页版](https://flyfish-go.github.io/H3C-NX30-Uboot/)，选择机型，按提示准备对应版本的官方固件，生成桥接包。
2. 登录路由器原厂后台，在「固件升级」上传桥接包。重启后电脑用网线接 **LAN** 口，打开 `http://192.168.10.1`。
3. 在安装向导里依次「识别 → 备份 → 安装」。**备份文件请保存在自己电脑上**，里面有本机的校准数据和序列号。完成后路由器重启，照常进入原厂系统。

之后想进救援页：断电，按住 Reset 再通电，打开 `http://192.168.10.1`。

**已经装过本项目旧版 U-Boot：** 在救援页「U-Boot 更新」上传新版即可，见 [pre.6 升级说明](releases/uboot-v0.1.0-pre.6.md#从-pre3pre4-或-pre5-升级)（HW-D 的 ECC8 机器要用 RAM 救援版）。比 pre.3 更旧的版本按 [pre.3 说明](releases/uboot-v0.1.0-pre.3.md#从旧版升级)操作，升级后需要再刷一次固件。

## 装好之后：刷 QSDK 11.5 固件

配套项目 [**H3C-RT3000-Product-R1**](https://github.com/FlyFish-go/H3C-RT3000-Product-R1) 为四类主板提供基于高通 QSDK 11.5 / OpenWrt 21.02 的固件：NSS 硬件加速，有线 NAT 约 940 Mbps，Wi-Fi 6 双频，无线校准数据开机时从本机读取。在救援页把固件写进非原厂系统的那个槽，设为默认启动即可，原厂系统保留不动。

## 测试情况

四类主板各用一台样机，完整跑通了从原厂到第三方系统的全流程：

| 主板 | BootBridge 安装与回原厂 | U-Boot pre.5 | 大分区 | QSDK 11.5 r6 |
|---|:---:|:---:|:---:|:---:|
| HW-A（BootBridge 用 RC3000，其余用 RW3000） | ✅ | ✅ | ✅ | ✅ |
| HW-B（RT3000） | ✅ | ✅ | ✅ | ✅ |
| HW-C（RT3000 RTL8367S） | ✅ | ✅ | ✅ | ✅ |
| HW-D（NX30） | ✅ | ✅ | ✅ | ✅ |

U-Boot 共 84 个候选（4 类主板 × 21 种 NAND 配置），全部离线构建通过；实机测试的是上表 4 个，其余候选还没有在实物上跑过。pre.6 修复的是救援页「固件更新」，实机测试了 HW-A、HW-B、HW-D，HW-C 还没有在 pre.6 上测试，见 [pre.6 说明](releases/uboot-v0.1.0-pre.6.md#验证状态)。每类主板目前只有一台样机，详细记录见[验证记录](docs/VALIDATION.md)。

## 使用前请注意

> [!WARNING]
> 这仍是**测试版**。改写启动程序有变砖风险，如果你没有 TTL 串口等独立恢复手段，可以先观望一段时间。

- 只接受上表列出的官方固件版本，BootBridge 会核对文件大小和 SHA-256，不匹配时不会生成桥接包。
- U-Boot 分区文件（`appsbl.mbn`）**不能**直接上传到原厂升级页，首次安装请用 BootBridge。
- 用 pre.3 以前的救援页「固件更新」刷过固件的设备，系统分区会逐渐出现 ECC 错误，请按 [pre.3 说明](releases/uboot-v0.1.0-pre.3.md#从旧版升级)升级 U-Boot 后重刷固件。
- 用 BootBridge v0.1.0-pre.6 刷入桥接后卡在桥接系统的设备，请不要再尝试其他刷机方式，加群联系维护者。

## 交流与反馈

- **QQ 群：1131957442**（交流、提问、反馈问题都可以）
- [GitHub Issues](https://github.com/FlyFish-go/H3C-NX30-Uboot/issues)：反馈问题时请用[测试记录模板](docs/TEST-REPORT-TEMPLATE.md)，附上版本、镜像 SHA-256 和去掉序列号的日志。
- 手里有其他批次的 NX30 / RT3000 / RC3000 / RW3000，愿意帮忙测试的，欢迎在群里联系。

**如果这个项目帮你省下了一根 TTL 线，欢迎点一个 ⭐ Star，让更多同型号的用户看到它。**

## 文档

| 文档 | 内容 |
|---|---|
| [安装说明](docs/INSTALLATION.md) | 安装流程与注意事项 |
| [硬件说明](docs/HARDWARE.md) | HW-A/B/C/D 的划分依据与 OEM 证据 |
| [NAND 与 ECC](docs/NAND-ECC.md) | 21 种 NAND 配置与 ECC 选择 |
| [验证记录](docs/VALIDATION.md) | 每个版本的离线与实机验证范围 |
| [构建说明](docs/BUILDING.md) | 工具链与构建步骤 |
| [BootBridge](tools/bootbridge/README.md) | 网页版与桥接程序源码、识别规则 |
| [目录说明](docs/REPOSITORY-STRUCTURE.md) | 仓库结构 |
| [发布说明](releases/) | 历次 U-Boot 版本说明 |

## 从源码构建

```sh
git clone https://github.com/FlyFish-go/H3C-NX30-Uboot.git
cd H3C-NX30-Uboot
```

需要兼容的 ARM 工具链，步骤见[构建说明](docs/BUILDING.md)。自己重建的镜像请按自己的文件摘要重新验证，不能直接沿用已发布候选的测试结论。

## 致谢

- [chenxin527/uboot-qsdk12.5-build](https://github.com/chenxin527/uboot-qsdk12.5-build)（uBootKit）及其上游贡献者：本项目的 U-Boot 与救援页基于它开发。
- [ayu1808](https://github.com/ayu1808)（恩山同名）：提供了适配思路，并参与了大量测试。

## 许可

U-Boot 和第三方组件保留原有许可证与版权；新增的构建和封装代码遵循各自声明。详见 [LICENSE](LICENSE) 和[许可说明](LICENSE-NOTICE.md)。

本项目与 H3C 官方无关。

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=FlyFish-go/H3C-NX30-Uboot,FlyFish-go/H3C-RT3000-Product-R1&type=Date)](https://star-history.com/#FlyFish-go/H3C-NX30-Uboot&FlyFish-go/H3C-RT3000-Product-R1&Date)
