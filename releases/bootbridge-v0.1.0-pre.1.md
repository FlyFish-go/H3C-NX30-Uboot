# BootBridge v0.1.0-pre.1（测试版）

> [!WARNING]
> 本版正在由测试人员上机测试，尚未取得整机验收结论。改写启动程序有变砖风险；没有独立恢复手段和自己样机写前备份的用户，请等待测试结论。

## 概要

| 项目 | 内容 |
|---|---|
| 工具版本 | `bootbridge-v0.1.0-pre.1` |
| 规则发布号 | `H3C-BOOTBRIDGE-NAND-OEM-20261020-R4` |
| 内含 U-Boot | [`uboot-v0.1.0-pre.2`](uboot-v0.1.0-pre.2.md)，四类硬件 × 21 个 NAND 配置，共 84 个候选 |
| 支持机型 | NX30 / RX3000、RT3000、RC3000、RW3000 / RW3010 |
| 网页版 | <https://flyfish-go.github.io/H3C-NX30-Uboot/> |

## 本版变化

- 首次公开 BootBridge 网页版与 Windows 离线版（x64 / x86），两者使用同一份规则，生成结果逐字节相同。
- 恢复页地址统一为 `192.168.10.1`（四类硬件一致），电脑自动获取 `192.168.10.100`–`192.168.10.200` 的地址。
- 网页第 6 步增加进入恢复页的说明：启动失败自动进入，或断电后按住 Reset 再通电。
- 修复：EXE 内显示的规则发布号此前是占位值 `development-unreleased`，现在与规则数据一致。

## 附件

| 文件 | SHA-256 |
|---|---|
| `BootBridge-Windows-x64-R4.zip`（EXE + `BootBridge.rules.dat`） | `<填写>` |
| `BootBridge-Windows-x86-R4.zip`（EXE + `BootBridge.rules.dat`） | `<填写>` |
| `BootBridge-Web-0.1.0.zip`（离线网页包） | `09ee94c05d03927c26abbadd57106d5a8be632c351b5175c3c0be1d469d6a62f` |
| `BootBridge-Offline-x64.exe` | `ede23b880af03a4432b9aa6188dfe3a59dacce8d2ec126caad66bd2e477afce4` |
| `BootBridge-Offline-x86.exe` | `2cf1df30401cd30aeea3772289d4865347f567195da4d718b29f0cb3c64d6c33` |
| `BootBridge.rules.dat` | `c29a9aac5bd32076c4f1fccf549562c038a15ae26e6428e3e039b640b7f6c342` |

完整清单见附件 `SHA256SUMS`。

## 使用前请注意

- 只接受上表机型对应的官方固件版本，按大小与 SHA-256 核对；不匹配时不会生成。
- 桥接包要通过原厂升级页上传；不要把 U-Boot 分区文件直接上传到原厂升级页。
- 任何文件都在你的电脑上处理，不会上传到本项目。
- 测试反馈请使用[测试记录模板](../docs/TEST-REPORT-TEMPLATE.md)，备份包请勿公开上传。

## 测试重点

1. `192.168.10.1` 恢复页能否打开（自动进入、按 Reset 进入两种方式）。
2. 电脑是否自动获得 `192.168.10.x` 地址。
3. 原厂 ENV 分区安装前后是否逐字节一致。
4. 按住 Reset 实际需要的秒数。

## 已知限制

- NX30 / RX3000 需要在电脑上运行一次网页生成的命令来下载官方固件；命令输出目前只有中文。
- 网页版仅在 Chromium 内核浏览器实测，Firefox、Safari 尚未实测。
- 生产 U-Boot 自更新、ECC8、NOR 不在本版范围。
