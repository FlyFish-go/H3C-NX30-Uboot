# BootBridge v0.1.0-pre.4（测试版）

> [!WARNING]
> 本版正在由测试人员上机测试，尚未取得整机验收结论。改写启动程序有变砖风险；没有独立恢复手段和自己样机写前备份的用户，请等待测试结论。

## 概要

| 项目 | 内容 |
|---|---|
| 工具版本 | `bootbridge-v0.1.0-pre.4` |
| 规则发布号 | `H3C-BOOTBRIDGE-NAND-OEM-20261022-R6` |
| 内含 U-Boot | 源码快照 `3095c9527234`：[`uboot-v0.1.0-pre.2`](uboot-v0.1.0-pre.2.md) 加上 DHCP 备用地址池修复，四类硬件 × 21 个 NAND 配置，共 84 个候选 |
| 支持机型 | NX30 / RX3000、RT3000、RC3000、RW3000 / RW3010 |
| 网页版 | <https://flyfish-go.github.io/H3C-NX30-Uboot/> |

## 本版变化

- **覆盖安装**：设备上已经装过其他 U-Boot（例如 pre.1 / pre.2 / pre.3 装上的旧版本，或第三方 U-Boot）时，以前桥接程序会因为"未命中可信版本注册表"拒绝安装，设备继续运行旧 U-Boot，恢复页仍是 `192.168.1.1`。现在会直接覆盖安装本版携带的 U-Boot。
  - 只有当前 U-Boot 不是原厂版本、也不在已登记版本里时才走这条路；原厂设备仍按首次安装处理。
  - 本版携带的 U-Boot 必须是已登记的候选，否则照样拒绝。
  - 写前检查不变：SBL、BC 选择器、ECC 校验任何一项异常都会拒绝；从判定到写入之间当前 U-Boot 内容有变化也会拒绝。
  - 维护页会提示"覆盖安装，不保留原U-Boot"。被覆盖的 U-Boot 不会被保留，之后不能通过 BootBridge 返回到它。
- **DHCP 修复**：U-Boot 恢复模式的备用 DHCP 地址池固定在本机恢复网段，电脑会拿到 `192.168.10.x` 地址。
- 规则更新为 `H3C-BOOTBRIDGE-NAND-OEM-20261022-R6`，四个机型的桥接包全部重新生成；同一份官方固件生成的桥接包与 pre.3 不同。
- 网页版与 Windows 离线版（x64 / x86）使用同一份规则，生成结果逐字节相同。

**用 pre.3 安装后串口日志里仍是旧 U-Boot、恢复页仍是 `192.168.1.1` 的用户，请用本版重新生成桥接包后再安装。**

## 附件

| 文件 | SHA-256 |
|---|---|
| `BootBridge-Windows-x64-R6.zip`（EXE + `BootBridge.rules.dat`） | `ffec0449185373f15fe2ad23c52f7ba42881ca25d328b5ebef7bb521ea178f46` |
| `BootBridge-Windows-x86-R6.zip`（EXE + `BootBridge.rules.dat`） | `fc0de489151b012936b5014756f44190338ae41e3af72a87a897768873c409cc` |
| `BootBridge-Web-0.1.0.zip`（离线网页包） | `638276f0a812c31dbcc713d4ac68b5adf0a18e6fffd5179a48ef9b56ddb10cd3` |
| `BootBridge-Offline-x64.exe` | `a1ab8635235c31a664f4ee3deaa91500542bece3d98705e5bfe8b05ae8715fc1` |
| `BootBridge-Offline-x86.exe` | `6cc428229edfc2e6ac7d9f64c872331bcebf806580223e55851003f7fad63046` |
| `BootBridge.rules.dat` | `032e7830aa14cd6a7b3fde58bb37f91e3a1926f58c2be77f6ba31421556cd3fe` |

完整清单见附件 `SHA256SUMS`。

## 使用前请注意

- 只接受上表机型对应的官方固件版本，按大小与 SHA-256 核对；不匹配时不会生成。
- 桥接包要通过原厂升级页上传；不要把 U-Boot 分区文件直接上传到原厂升级页。
- 任何文件都在你的电脑上处理，不会上传到本项目。
- 测试反馈请使用[测试记录模板](../docs/TEST-REPORT-TEMPLATE.md)，备份包请勿公开上传。

## 测试重点

1. 已装过旧 U-Boot 的设备：维护页是否显示"覆盖安装"，安装后串口启动日志中的 U-Boot 版本是否为 `3095c9527234`。
2. `192.168.10.1` 恢复页能否打开（自动进入、按 Reset 进入两种方式）。
3. 电脑是否自动获得 `192.168.10.x` 地址。
4. 原厂 ENV 分区安装前后是否逐字节一致。

## 已知限制

- 覆盖安装后，BootBridge 的"返回原厂"不能恢复被覆盖的 U-Boot。
- NX30 / RX3000 需要在电脑上运行一次网页生成的命令来下载官方固件；命令输出目前只有中文。
- 网页版仅在 Chromium 内核浏览器实测，Firefox、Safari 尚未实测。
- 生产 U-Boot 自更新、ECC8、NOR 不在本版范围。
