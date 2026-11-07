# H3C-NX30-Uboot v0.1.0

第一个正式版。功能与 pre.7 相同，另外增加按键切换系统，救援页也能直接告诉你该选哪个文件。

## 概要

| 项目 | 内容 |
|---|---|
| 源码提交 | `ddad68d1` |
| 候选 | HW-A / HW-B / HW-C / HW-D × 21 个 NAND 配置，共 84 个；每个候选附带一个 RAM 救援版 |
| 附件 | `uboot-v0.1.0-HW-A.zip` … `HW-D.zip`，内含 `HW-<组>/<NAND 型号>-ECC<位数>/appsbl.mbn` 和 `ram-rescue.elf` |
| 工具链 | OpenWrt GCC 8.4.0 `r16847-f8282da11e` + binutils 2.34 |

全部摘要见 [`CANDIDATES.json`](https://github.com/FlyFish-go/H3C-NX30-Uboot/blob/uboot-v0.1.0/release-metadata/uboot-v0.1.0/CANDIDATES.json) 与 [`SHA256SUMS.txt`](https://github.com/FlyFish-go/H3C-NX30-Uboot/blob/uboot-v0.1.0/release-metadata/uboot-v0.1.0/SHA256SUMS.txt)。

## 相对 pre.7 的变化

### 按键切换系统

断电，按住 **RESET 或 Mesh/WPS 键**再通电，红灯每秒短闪一下：

| 松手时机 | 结果 |
|---|---|
| 第一闪之前 | 正常启动默认系统（同以前） |
| 闪 **1** 下后 | 启动**物理一区**，并把它设为默认 |
| 闪 **2** 下后 | 启动**物理二区**，并把它设为默认 |
| 按住到第 **3** 闪 | 进入网页救援页 `192.168.10.1`（同以前） |

- 松手后绿灯快闪 1 下或 2 下，表示选中了哪个区。第 3 闪亮满后才转绿灯进救援页。
- 松手要持续 0.2 秒才算数，按 RESET 小孔时手指打滑不会提前结束。Mesh/WPS 键比 RESET 小孔好按。
- "一区/二区"按 NAND 上的物理位置算（一区在低地址），和分区名 `rootfs`/`rootfs_1` 无关。
- 设默认用的是救援页「默认启动」同一套写入流程：先写 BOOTCONFIG 备份份，再写主份，两份都完整读回核对。写入前就被拒绝（例如自定义了 `bootcmd`）时，这次照样启动所选的区，只是默认不变；写到一半出错会进救援页。
- **大分区（单系统）**：闪 1 下启动唯一的系统；闪 2 下或 3 下进救援页（没有二区）。分区表无法识别时一律进救援页。
- **HW-A 和 HW-D 的行为变化**：以前按住 RESET 上电会直接进救援页、不闪灯，现在四个硬件组都要按到第 3 闪。

### 救援页

- 「U-Boot 更新」页直接写出该选哪个文件，例如「请从 HW-A 的 zip 中选择 `HW-A/W25N01GWZEIG-ECC4/appsbl.mbn`」。只有这个文件夹的文件能通过校验。
- 「系统信息」在 HW-D（NX30）上也显示 NAND 型号了。
- HW-B / HW-D 的「系统信息」显示 5 GHz FEM 识别结果，例如「无 → HW-B」。5 GHz 没有外置 FEM 的 D 型板按规则属于 **HW-B**，系统固件也应选 HW-B。U-Boot 当前和识别结果不是同一组时，「U-Boot 更新」页会提示。本版仍然只接受本组的 U-Boot 文件，不支持在网页上换组。只在打开网页时读一次 ART，不写入，启动流程不读取。
- 用 RAM 救援版启动时，「系统信息」注明「RAM 救援版（未写入闪存）」。
- 网页终端执行 `bootelf` / `bootm` / `bootipq` / `go` / `reset` 时提示页面连接会断开，出现 "Failed to fetch" 属正常现象。

### 版本号

启动信息和「系统信息」里的版本带发布标签和真实日期，例如 `2016.01-h3c-common4-v0.1.0-ddad68d1835b`（HW-D 为 `…-bridge10-v0.1.0-…`）。以前每个版本都显示 `2026-10-17`。

## 升级

已装 pre.3–pre.7：救援页「U-Boot 更新」上传同一硬件组、同一 NAND 配置和 ECC 的 `appsbl.mbn`（页面会写出文件名），两份 APPSBL 一起写入，断电重启确认版本号。HW-D 的 ECC8 机器若仍在 pre.5 或更早，先用 RAM 救援版，见 [pre.7 说明](uboot-v0.1.0-pre.7.md#从-pre3pre6-升级)。比 pre.3 更旧的按 [pre.3 说明](uboot-v0.1.0-pre.3.md#从旧版升级)。

首次安装请用 [BootBridge](https://flyfish-go.github.io/H3C-NX30-Uboot/)，`appsbl.mbn` 不能直接上传到原厂升级页。

## 验证状态

- **离线**：84/84 个候选与 84/84 个 RAM 救援版构建通过，版本串含标签和提交号 `ddad68d1835b`；二进制中无构建机路径。主机测试：闪灯次数与动作的对应、四个硬件组的启动策略（含 Mesh 单独按、松手于不同时刻、按住不放）、救援页默认槽位写入事务、FEM 识别全部通过。
- **实机写入**：四台样机（A/C/D 为 W25N01GWZEIG ECC4，B 为 GD5F1GQ5REYIG ECC8）经救援页「U-Boot 更新」写入本版，两份 APPSBL 读回与发布文件一致；之后各冷启动 5 次，都从默认系统正常启动，无 panic。A/B/C/D 另一槽的 QSDK r10 单次启动各 3 次正常。
- **按键**（在本版之前的候选 rc2/rc3 上人工实测；本版只改了第 3 闪的显示时长和提前松手的判定）：HW-D Mesh 闪 1/2/3 下都正确（启动对应的区并设为默认、进救援页），救援页显示 NAND 型号；HW-A Mesh 闪 3 下进救援页、RESET 闪 1/2/3 下正确；HW-C Mesh 闪 3 下进救援页、不按键正常启动。HW-B 的按键没有实测。
- **未验证**：其余 80 个候选只有离线构建结论。

## 重建

```
SOURCE_REVISION=ddad68d1835b3b9d4b8a2e9fbaf933a32cf3e4fb SOURCE_DATE_EPOCH=1793978073 \
H3C_RELEASE_LABEL=v0.1.0 ./build-release.sh <A|B|C|D> <输出目录>
```

在 git 检出里构建时，`SOURCE_REVISION` 和 `SOURCE_DATE_EPOCH` 默认取当前提交及其提交时间；用源码包构建时必须显式传入。
