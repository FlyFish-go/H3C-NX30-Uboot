# BootBridge v0.1.0-pre.3（测试版）

> [!WARNING]
> 本版正在由测试人员上机测试，尚未取得整机验收结论。改写启动程序有变砖风险；没有独立恢复手段和自己样机写前备份的用户，请等待测试结论。

## 概要

| 项目 | 内容 |
|---|---|
| 工具版本 | `bootbridge-v0.1.0-pre.3` |
| 规则发布号 | `H3C-BOOTBRIDGE-NAND-OEM-20261022-R5` |
| 内含 U-Boot | [`uboot-v0.1.0-pre.2`](uboot-v0.1.0-pre.2.md)，四类硬件 × 21 个 NAND 配置，共 84 个候选 |
| 支持机型 | NX30 / RX3000、RT3000、RC3000、RW3000 / RW3010 |
| 网页版 | <https://flyfish-go.github.io/H3C-NX30-Uboot/> |

## 本版变化

- 修复：原厂 ENV 中没有 `machid` 的设备上，桥接程序会拒绝安装自己携带的 U-Boot（`BII_ENV_MACHID`），设备因此继续运行旧 U-Boot，恢复页仍是 `192.168.1.1`。HW-C（如 RC3000）出厂即是这种情况。现在按设备树路由补证后可以正常安装，恢复页为 `192.168.10.1`。
- 规则更新为 `H3C-BOOTBRIDGE-NAND-OEM-20261022-R5`，四个机型的桥接包全部重新生成；同一份官方固件生成的桥接包与 pre.1 / pre.2 不同（pre.2 只更新了界面，规则数据与 pre.1 相同）。
- 网页版与 Windows 离线版（x64 / x86）使用同一份规则，生成结果逐字节相同。

**用 pre.1 或 pre.2 生成过桥接包、安装后恢复页仍是 `192.168.1.1` 的用户，请用本版重新生成桥接包后再安装。**

## 附件

| 文件 | SHA-256 |
|---|---|
| `BootBridge-Windows-x64-R5.zip`（EXE + `BootBridge.rules.dat`） | `e6ae78b6bfb112fb4e620c3cb694d221b4d46f5cfbeecb93c3b0df249588d2f2` |
| `BootBridge-Windows-x86-R5.zip`（EXE + `BootBridge.rules.dat`） | `cf88d19740268b9f71c05c99f83cfe47e696268fe82e9b09d952cee55f623d9b` |
| `BootBridge-Web-0.1.0.zip`（离线网页包） | `21acbd3191c945bfdb693db50b07c14fbb5753c562b534a4c7eb0273e7e05266` |
| `BootBridge-Offline-x64.exe` | `6682ebf605d0095881289713bf0886079475c67b43bf90b7654ad7712cd0c909` |
| `BootBridge-Offline-x86.exe` | `f7303a419a421a8fe103f290bc18f448b063379099b70b399464172396bc0543` |
| `BootBridge.rules.dat` | `73c0d34e912829e286e0d85c2408c1833b7d3cf4d3d11b532b6966202f20765c` |

完整清单见附件 `SHA256SUMS`。

## 使用前请注意

- 只接受上表机型对应的官方固件版本，按大小与 SHA-256 核对；不匹配时不会生成。
- 桥接包要通过原厂升级页上传；不要把 U-Boot 分区文件直接上传到原厂升级页。
- 任何文件都在你的电脑上处理，不会上传到本项目。
- 测试反馈请使用[测试记录模板](../docs/TEST-REPORT-TEMPLATE.md)，备份包请勿公开上传。

## 测试重点

1. 安装后串口启动日志中的 U-Boot 版本不再是旧版本，恢复页在 `192.168.10.1`。
2. `192.168.10.1` 恢复页能否打开（自动进入、按 Reset 进入两种方式）。
3. 电脑是否自动获得 `192.168.10.x` 地址。
4. 原厂 ENV 分区安装前后是否逐字节一致。

## 已知限制

- NX30 / RX3000 需要在电脑上运行一次网页生成的命令来下载官方固件；命令输出目前只有中文。
- 网页版仅在 Chromium 内核浏览器实测，Firefox、Safari 尚未实测。
- 生产 U-Boot 自更新、ECC8、NOR 不在本版范围。
