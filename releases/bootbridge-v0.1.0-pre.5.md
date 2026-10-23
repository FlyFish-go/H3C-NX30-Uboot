# BootBridge v0.1.0-pre.5（测试版）

> [!WARNING]
> 本版正在由测试人员上机测试，尚未取得整机验收结论。改写启动程序有变砖风险；没有独立恢复手段和自己样机写前备份的用户，请等待测试结论。

## 概要

| 项目 | 内容 |
|---|---|
| 工具版本 | `bootbridge-v0.1.0-pre.5` |
| 规则发布号 | `H3C-BOOTBRIDGE-NAND-OEM-20261023-R7` |
| 内含 U-Boot | 与 pre.4 相同：源码快照 `3095c9527234`（[`uboot-v0.1.0-pre.2`](uboot-v0.1.0-pre.2.md) 加上 DHCP 备用地址池修复），四类硬件 × 21 个 NAND 配置，共 84 个候选 |
| 支持机型 | NX30 / RX3000、RT3000、RC3000、RW3000 / RW3010 |
| 网页版 | <https://flyfish-go.github.io/H3C-NX30-Uboot/> |

## 本版变化

- **修复：HW-A、HW-B 原厂设备一律被拒（`env-machid`）。** 原厂 U-Boot 在所有板型上都不把 `machid` 存进 ENV，而是启动时临时注入。以前只有 HW-D 和 RC3000 的 HW-C 有替代证明，HW-A、HW-B 设备在维护页上会因为"ENV 缺少 machid"关闭安装。现在改为核对原厂内核实际使用的设备树路由（HW-A `mp03.1`，HW-B / HW-D `mp02.1`，HW-C `mp03.1-c2`），每种路由只能证明自己的板型。ENV 里如果存了 `machid` 但数值不对，仍然拒绝。
- **修复：HW-C 的这项证明不再只限 RC3000。** RT3000、RX3000 等同样是 HW-C 板的设备也适用。
- **修复：NAND 上有已纠正的位翻转时拒绝安装。** 以前两份 APPSBL 只要自开机以来出现过一次已被 ECC 纠正的位翻转，就会关闭安装，而且每次开机都一样。现在已纠正的位翻转不再拒绝，维护页会注明次数；无法纠正的错误、坏块、无法读取 ECC 计数仍然拒绝。
- **改进：一次列出全部问题。** 维护页的检查不再在第一个失败项处停止，所有独立检查都会执行，失败项全部列出，第一项作为主要原因显示。是否允许安装的判断没有放宽：任何一项失败仍然关闭安装。身份检查失败时会附上中文说明。
- 规则更新为 `H3C-BOOTBRIDGE-NAND-OEM-20261023-R7`，四个机型的桥接包全部重新生成；同一份官方固件生成的桥接包与 pre.4 不同。
- 网页版与 Windows 离线版（x64 / x86）使用同一份规则，生成结果逐字节相同。

**用 pre.4 或更早版本生成桥接包、维护页提示无法安装的用户，请用本版重新生成桥接包再试；如果仍无法安装，请在维护页下载诊断文件，按[测试记录模板](../docs/TEST-REPORT-TEMPLATE.md)反馈全部失败项。**

## 附件

| 文件 | SHA-256 |
|---|---|
| `BootBridge-Windows-x64-R7.zip`（EXE + `BootBridge.rules.dat`） | `61f6b9fa69035a4af5fada49572e4d45e83dd77b6202def8c650dd3e07af8ede` |
| `BootBridge-Windows-x86-R7.zip`（EXE + `BootBridge.rules.dat`） | `3846118446ebdc0c2764117b815c92bc54189df2b467c38eaa20298a0eb91ad9` |
| `BootBridge-Web-0.1.0.zip`（离线网页包） | `d09278a23d5a22eb7cd0e82af769bae3d7def64f840b63a1c18a150eb3ff781c` |
| `BootBridge-Offline-x64.exe` | `230332f3b7a00c76d6e8ecde67a0c245fe544eb8a68a1cd0cf71c033acfdaa32` |
| `BootBridge-Offline-x86.exe` | `90780425061e187a9afdb0dcff3552e318a3c6addc14b71d585b99ea007dd037` |
| `BootBridge.rules.dat` | `de9e2c7b70fd5228387d2b10b6cf434995635213853ffe83272bf5b90dfcb5ed` |

完整清单见附件 `SHA256SUMS`。

## 使用前请注意

- 只接受上表机型对应的官方固件版本，按大小与 SHA-256 核对；不匹配时不会生成。
- 桥接包要通过原厂升级页上传；不要把 U-Boot 分区文件直接上传到原厂升级页。
- 任何文件都在你的电脑上处理，不会上传到本项目。
- 测试反馈请使用[测试记录模板](../docs/TEST-REPORT-TEMPLATE.md)，备份包请勿公开上传。

## 测试重点

1. HW-A、HW-B 原厂设备：维护页是否不再出现 `env-machid`，能否完成安装。
2. 无法安装时，维护页是否列出全部失败项（而不只是第一项）。
3. 安装后串口启动日志中的 U-Boot 版本为 `3095c9527234`，`192.168.10.1` 恢复页可以打开，电脑自动获得 `192.168.10.x` 地址。
4. 原厂 ENV 分区安装前后是否逐字节一致。

## 已知限制

- 覆盖安装（当前 U-Boot 不是原厂或已登记版本）后，BootBridge 的"返回原厂"不能恢复被覆盖的 U-Boot。
- 出厂 U-Boot 不在版本登记表里的设备，会走覆盖安装路径，同样不能用 BootBridge 返回原厂。
- NX30 / RX3000 需要在电脑上运行一次网页生成的命令来下载官方固件；命令输出目前只有中文。
- 网页版仅在 Chromium 内核浏览器实测，Firefox、Safari 尚未实测。
- 生产 U-Boot 自更新、ECC8、NOR 不在本版范围。
