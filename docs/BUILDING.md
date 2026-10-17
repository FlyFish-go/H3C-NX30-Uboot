# U-Boot 构建说明

本次提供完整公开源码与四类候选。仓库根目录提供 `build-release.sh` 构建入口；使用源码归档时，进入 `ABCD-UBOOT-SOURCE-AND-CANDIDATES-20261016.tar.gz` 中的 `abcd-uboot-public-review-20261016/source/` 目录，使用同一入口。目录说明见[仓库结构](REPOSITORY-STRUCTURE.md)。

## 准备环境

需要 Bash、GNU make、Python 3、GNU sha256sum、GNU date、主机 C 编译器与链接器、zlib 开发文件，以及旧版 U-Boot 构建所需的主机工具。主机 libc 缺少 strlcpy/strlcat 时，构建入口会探测并使用 libbsd，需具备相应开发文件。

ARM 工具链由构建者另行准备。本次四类候选使用 GCC 5.2.0 / binutils 2.24 构建；归档不附工具链，也没有提供该精确工具链的已核验公开下载地址与摘要。其他版本的编译器能否完成构建需实际核验。

## 构建示例

解开源码归档后进入上述 `source/` 目录，再指定工具链前缀和一个尚不存在的绝对输出路径：

```sh
CROSS_COMPILE=/opt/toolchains/bin/arm-openwrt-linux- \
  ./build-release.sh D /tmp/h3c-nx30-hw-d-build
```

工具链路径是示例，需替换成已安装的实际前缀；对应 gcc、ld、strip、nm 必须可用。输出必须位于源码目录之外，入口会拒绝复用已有路径。

参数 A、B、C、D 用于构建对应的工程配置，各自使用独立输出目录。此处是开发者的构建选择，不是普通用户的手动安装选板步骤。

构建会输出 ELF、DTB、`appsbl.mbn`、`appsbl.partition.bin`、配置、来源记录和摘要清单。MBN 使用随源码提供的独立 Python 封装器生成。

## 版本与字节一致性

源码来源、去敏和构建适配变更见 [SOURCE-PROVENANCE.json](../release-metadata/uboot-v0.1.0-pre.1/SOURCE-PROVENANCE.json)。嵌入的来源修订号用于追溯，不是尚未创建的 GitHub 发布提交。

原编译器会嵌入部分路径；不同工具链或构建路径可能产生不同镜像。本次不承诺任意环境下逐字节重建。构建输出通过不等于安装资格通过，输出摘要变化后需要自己的设备验证。

原始英文说明和历史构建结果仍保存在源码归档中的 `source/BUILDING.md` 与 `BUILD-RESULTS.md`。已验公开 D 的最新范围以本仓库[验证记录](VALIDATION.md)及其精确摘要为准。

[返回首页](../README.md)
