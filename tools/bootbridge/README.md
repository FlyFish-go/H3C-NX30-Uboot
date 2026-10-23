# BootBridge 配套工具

BootBridge 用于在原厂系统上首次安装本仓库的 U-Boot：用 H3C 官方固件在你自己的电脑上生成"桥接包"，通过原厂升级页上传；桥接程序启动后识别设备、做写前检查，再在维护页安装 U-Boot。普通用户不需要选择 HW-A/B/C/D。

> [!WARNING]
> **测试版 `bootbridge-v0.1.0-pre.6`（规则 `H3C-BOOTBRIDGE-NAND-OEM-20261023-R8`）正在由测试人员上机测试，尚未取得整机验收结论。**
> 改写启动程序有变砖风险。没有串口或编程器等独立恢复手段、也没有自己样机写前备份的用户，请等待测试结论。

## 获取

| 方式 | 地址 | 说明 |
|---|---|---|
| 网页版 | <https://flyfish-go.github.io/H3C-NX30-Uboot/> | 在浏览器内生成，任何文件都不会上传 |
| Windows 版 | [Release `bootbridge-v0.1.0-pre.6`](https://github.com/FlyFish-go/H3C-NX30-Uboot/releases/tag/bootbridge-v0.1.0-pre.6) | x64 / x86 压缩包；EXE 必须和 `BootBridge.rules.dat` 放在同一文件夹 |
| 离线网页包 | 同一 Release 的 `BootBridge-Web-0.1.0.zip` | 解压后双击 `index.html` |

下载后请用 Release 中的 `SHA256SUMS` 核对文件。

## 支持的机型与官方固件

| 机型 | 必须使用的官方固件 | 获取方式 |
|---|---|---|
| NX30 / RX3000 | `NX30V100R012` | 无公开链接；网页版按设备 SN 生成下载命令，在本机运行 |
| RT3000 | `RT3000V100R015` | H3C 官方地址直接下载 |
| RC3000 | `RC3000V100R013L01` | H3C 官方地址直接下载 |
| RW3000 / RW3010 | `RW3000V100R012` | H3C 官方地址直接下载 |

工具按大小和 SHA-256 核对官方固件，不匹配时不会生成桥接包。

## 六个步骤

1. 选择机型。
2. 获取上表中对应的官方固件。
3. 先用原厂升级页把路由器升级到这个官方版本。
4. 在工具里选择这份官方固件，生成桥接包。
5. 在原厂升级页上传桥接包。
6. 打开 `192.168.10.1` 的维护页，按提示备份并安装 U-Boot。

安装后进入恢复页：系统启动失败时会自动进入；也可以断电后按住 Reset 再通电，保持几秒后松开。电脑网卡设为自动获取 IP（会得到 `192.168.10.x`），浏览器打开 `192.168.10.1`。

## 测试反馈

请使用[测试记录模板](../../docs/TEST-REPORT-TEMPLATE.md)，写明机型、官方版本、工具版本和规则号，并重点记录：

- 能否打开 `192.168.10.1` 恢复页（自动进入和按 Reset 进入两种方式）；
- 电脑是否自动获得 `192.168.10.x` 地址；
- 原厂 ENV 分区在安装前后是否逐字节一致；
- 按住 Reset 实际需要几秒。

备份包可能含设备身份、ENV、ART 和认证材料，请勿公开上传。

## 目录

| 路径 | 内容 |
|---|---|
| `desktop/` | Windows 离线版源码（C）、规则目录与构建脚本 |
| `web/` | 网页版源码、站点生成脚本与测试 |
| `design/desktop-offline-ui/` | 桌面版与网页版共用的界面文案 |
| `nx30-oem-bridge/` | 桥接包内运行的安装程序源码与测试 |
| `rules/H3C-BOOTBRIDGE-NAND-OEM-20261023-R8/` | 本版规则的生成头文件、清单与摘要（规则数据 DAT 作为 Release 附件提供） |

构建方法见 [`desktop/README.md`](desktop/README.md) 与 [`web/README.md`](web/README.md)。构建与测试需要你自己下载的官方固件，仓库不包含任何 H3C 官方固件。

桥接包里的 U-Boot 由本仓库根目录的源码构建，对应 [`uboot-v0.1.0-pre.2`](../../releases/uboot-v0.1.0-pre.2.md)。

## 许可

BootBridge 自编代码按 GPL-2.0-or-later 提供（见 [`LICENSE`](LICENSE) 与 [`COPYING`](COPYING)）；第三方与上游文件保留各自的许可。

[返回首页](../../README.md)
