# H3C-NX30-Uboot v0.1.0-pre.8.rc1（测试版）

> [!WARNING]
> 这是给测试者用的候选版，**还没有经过实机写入验证**。改写启动程序有变砖风险；没有串口等独立恢复手段的用户请等正式的 pre.8。

## 概要

| 项目 | 内容 |
|---|---|
| 源码提交 | `e1c2c60e` |
| 候选 | HW-A / HW-B / HW-C / HW-D × 21 个 NAND 配置，共 84 个；每个候选附带一个 RAM 救援版 |
| 附件 | `uboot-v0.1.0-pre.8.rc1-HW-A.zip` … `HW-D.zip`，内含 `HW-<组>/<NAND 型号>-ECC<位数>/appsbl.mbn` 和 `ram-rescue.elf` |
| 工具链 | OpenWrt GCC 8.4.0 `r16847-f8282da11e` + binutils 2.34 |

全部摘要见 [`CANDIDATES.json`](https://github.com/FlyFish-go/H3C-NX30-Uboot/blob/uboot-v0.1.0-pre.8.rc1/release-metadata/uboot-v0.1.0-pre.8.rc1/CANDIDATES.json) 与 [`SHA256SUMS.txt`](https://github.com/FlyFish-go/H3C-NX30-Uboot/blob/uboot-v0.1.0-pre.8.rc1/release-metadata/uboot-v0.1.0-pre.8.rc1/SHA256SUMS.txt)。

## 相对 pre.7 的变化

### 按 RESET 键切换系统

断电，按住 RESET 再通电，红灯每秒短闪一下：

| 松手时机 | 结果 |
|---|---|
| 第一闪之前 | 正常启动默认系统（同以前） |
| 闪 **1** 下后 | 启动**物理一区**，并把它设为默认 |
| 闪 **2** 下后 | 启动**物理二区**，并把它设为默认 |
| 按住到第 **3** 闪 | 进入网页救援页 `192.168.10.1`（同以前） |

- 松手后绿灯快闪 1 下或 2 下，表示选中了哪个区。
- "一区/二区"按 NAND 上的物理位置算（一区在低地址），和分区名 `rootfs`/`rootfs_1` 无关。
- 设默认用的是救援页「默认启动」同一套写入流程：先写 BOOTCONFIG 备份份，再写主份，两份都完整读回核对。写入前就被拒绝（例如自定义了 `bootcmd`）时，这次照样启动所选的区，只是默认不变；写到一半出错会进救援页。
- **大分区（单系统 MIBIB）**：闪 1 下启动唯一的系统；闪 2 下或 3 下进救援页（没有二区）。分区表无法识别时一律进救援页。
- **HW-D（NX30）行为变化**：以前按住 RESET 上电会直接进救援页，现在和 HW-A/B/C 一样要按到第 3 闪。D 的闪灯要等 U-Boot 3 秒串口等待结束后才开始，所以比 A/B/C 晚 3 秒左右，按住不放即可。

### 版本号

- 启动信息和救援页「系统信息」里的版本现在带发布标签和真实日期，例如 `2016.01-h3c-common4-v0.1.0-pre.8.rc1-e1c2c60e9f37 (2026-11-04 …)`。以前每个版本都显示 `2026-10-17`，只能靠提交号区分。
- 用 RAM 救援版（`bootelf`）启动时，「系统信息」的启动模式会注明「RAM 救援版（未写入闪存）」。
- 网页终端执行 `bootelf` / `bootm` / `bootipq` / `go` / `reset` 时会提示：页面连接会断开，出现 "Failed to fetch" 属正常现象。

其余行为与 pre.7 相同。

## 从 pre.3–pre.7 升级

与 [pre.7 说明](uboot-v0.1.0-pre.7.md#从-pre3pre6-升级) 相同：救援页「U-Boot 更新」上传同一硬件组、同一 NAND 配置和 ECC 的 `appsbl.mbn`，两份 APPSBL 一起写入，断电重启确认。HW-D 的 ECC8 机器若仍在 pre.5 或更早，先用 RAM 救援版。

## 请测试者反馈

1. 硬件组、NAND 型号/ECC、原来的 U-Boot 版本。
2. 升级后启动信息里的版本串。
3. 闪 0 / 1 / 2 / 3 下分别的结果：启动了哪个系统、重启后默认是否跟着变、灯的表现。
4. 大分区用户：闪 1 下和闪 2 下的结果。
5. 有串口的请附上 `H3C RESET:` 开头的几行日志。

## 验证状态

- **离线**：84/84 个候选与 84/84 个 RAM 救援版构建通过，版本串含标签和提交号 `e1c2c60e9f37`；主机测试：闪灯次数与动作的对应（双分区 / 大分区 / 无法识别）、四个硬件组的启动策略（含 HW-D 松手于 0.5 / 1.5 / 2.5 秒和按住不放）、救援页默认槽位写入事务（B/C/D 配置）全部通过。
- **实机**：**尚未进行**。RESET 闪灯选择需要人手按键，等测试者反馈。

## 重建

```
SOURCE_REVISION=e1c2c60e9f37b5ff0b6f432beb16da006a6b02a1 SOURCE_DATE_EPOCH=1793797546 \
H3C_RELEASE_LABEL=v0.1.0-pre.8.rc1 ./build-release.sh <A|B|C|D> <输出目录>
```

在 git 检出里构建时，`SOURCE_REVISION` 和 `SOURCE_DATE_EPOCH` 默认取当前提交及其提交时间；用源码包构建时必须显式传入。
