# U-Boot 构建说明

本页对应 `v0.1.0-pre.2`。仓库根目录的 `build-release.sh` 是 H3C 四类候选的构建入口，MBN 由 `release-tools/` 中独立实现的 Python 封装器生成，不需要高通的 `elftombn.py` / `mbn_tools.py`（未包含）。目录说明见[仓库结构](REPOSITORY-STRUCTURE.md)。

## 准备环境

需要 Bash、GNU make、Python 3、GNU sha256sum、GNU date、主机 C 编译器与链接器、zlib 开发文件，以及旧版 U-Boot 构建所需的主机工具。主机 libc 缺少 strlcpy/strlcat 时，构建入口会探测并使用 libbsd。

ARM 工具链由构建者另行准备，仓库不附带工具链。本版候选使用 **OpenWrt GCC 8.4.0（`r16847-f8282da11e`）+ binutils 2.34** 构建。用这套工具链重建本版源码可以得到与候选逐字节相同的结果。

> [!IMPORTANT]
> 不要用 pre.1 使用过的 GCC 5.2.0 / binutils 2.24 构建本版：结果会比候选大约 7–8 KB，与发布摘要不同。

**本版不提供已核验的工具链下载地址。** 该工具链的版本串 `r16847-f8282da11e` 指向 OpenWrt 21.02 系列的构建产物，但本仓库无法核实与之对应的公开下载地址和摘要，因此不给出链接。请自行从 OpenWrt 官方 SDK（21.02 分支，musl、EABI、arm_cortex-a7+neon-vfpv4）取得 GCC 8.4.0 与 binutils 2.34 的匹配组合；版本串不一致时，重建摘要也会不同。

## 构建示例

在仓库根目录执行，指定工具链前缀、路径映射和一个尚不存在的绝对输出路径：

```sh
CROSS_COMPILE=/opt/toolchains/bin/arm-openwrt-linux- \
KCFLAGS="-ffile-prefix-map=$PWD/=/build/" \
  ./build-release.sh D /tmp/h3c-hw-d-build
```

`KCFLAGS` 把源码根映射为 `/build/`，候选就是这样构建的：二进制里不含构建机的本地路径，换目录构建也能得到相同结果。不加这一项会把你的本地路径编进二进制，摘要也会与发布候选不同。发布候选实际使用的写法是 `-ffile-prefix-map=<源码树绝对路径>/=/build/`，即把包含 `u-boot-2016/` 的那一层目录映射到 `/build/`。在仓库根目录构建时写成 `-ffile-prefix-map=$PWD/=/build/` 即可；映射目标必须是 `/build/`，尾斜杠不能省。

工具链路径是示例，需替换成实际前缀；对应 gcc、ld、strip、nm 必须可用。输出必须位于源码目录之外，入口会拒绝复用已有路径。

参数 A、B、C、D 对应四类工程配置（`ipq5018_h3c_hw_a_production`、`ipq5018_h3c_rt3000_b`、`ipq5018_h3c_hw_c_production`、`ipq5018_h3c_hw_d_production`）。NAND 配置通过环境变量选择，与 84 个候选的构建方式一致：

| 变量 | 取值 |
|---|---|
| `H3C_NAND_PROFILE_ID` | `h3c-oem-` 开头的配置名，例如 `h3c-oem-c841-128m-2k-sp128` |
| `H3C_NAND_SOURCE_MASK` | `1`、`2`、`4`、`8` 或 `16` |
| `H3C_NAND_LOAD_ECC_BITS` | `4` 或 `8` |
| `H3C_NAND_STORAGE_ECC_BITS` | 必须与 `H3C_NAND_LOAD_ECC_BITS` 相同 |

四个变量必须同时给出，否则构建入口拒绝执行。84 个候选 = 4 个板型 × 21 个 OEM NAND 配置，全部使用 production 配置。完整清单见 [CANDIDATES.json](../release-metadata/uboot-v0.1.0-pre.2/CANDIDATES.json)。

构建会输出 ELF、DTB、`appsbl.mbn`、`appsbl.partition.bin`、配置、来源记录和摘要清单。

## 版本与字节一致性

候选嵌入的来源修订号是 `b0383f96b3282b0aedbd9765464778a9cf596836`，即源码快照 SHA-256 的前 40 位，不是 git 提交号。快照的计算方法、去敏文件与构建适配见 [SOURCE-PROVENANCE.json](../release-metadata/uboot-v0.1.0-pre.2/SOURCE-PROVENANCE.json)。

6 个去敏文件**参与编译**。去敏把样机标识串换成中性标签 `BOOTBRIDGE-HW-D` 并统一宏名，因此用本仓库源码重建时得到的是中性版本；这与发布候选一致（候选中同样不含原样机串，已逐字节核对过 84 个候选、两个 EXE 与规则数据）。若用去敏前的私有源码重建，摘要必然不同。

构建输出通过不等于安装资格通过；输出摘要与发布摘要不同时，需要自己的设备验证。

pre.1 的构建说明与结果保留在 [releases/uboot-v0.1.0-pre.1.md](../releases/uboot-v0.1.0-pre.1.md) 与 [release-metadata/uboot-v0.1.0-pre.1/](../release-metadata/uboot-v0.1.0-pre.1/)。

[返回首页](../README.md)
