# H3C-NX30-Uboot uboot-v0.1.0-pre.1

NX30 家族双系统 U-Boot 首个预发布。配套 BootBridge 工具后续在同一仓库发布。

## 本次提供

- 完整 U-Boot 公开源码、构建配置与独立 APPSBL 封装工具。
- HW-A/B/C/D 四类精确分区候选、校验清单、硬件范围与 D 验证摘要。
- 双系统槽位查看、固件识别、启动与默认槽管理，以及网页维护与恢复功能。

## 当前验证范围

D 镜像在一台 NX30 测试样机通过有限完整流程：Winbond EF:BA:21 / W25N01GWZEIG、spare64/ECC4、R012 SBL。A/B/C 为本轮待实测候选。

D 验证覆盖安装事务、完整读回、默认原厂 B 启动、独立冷启动和 UART 维护入口。安装起点 LOW 已含相同公开 D 载荷，本轮不称首次从空白安装。

生产 U-Boot 自更新、ECC8、NOR、无线与长期稳定性不在本轮通过范围；结果不自动转授给其它样机或不同摘要的镜像。

## 安装与进入

**分区 BIN 不能直接上传原厂升级页。** 本次不附普通用户首次安装工具，普通用户请等待后续工具与指南；测试者仅沿用覆盖自己精确样机的备份、安装和恢复流程。

安装本版 U-Boot 后，按住 RESET 上电进入维护网页，不按启动默认系统。也可通过 UART 截停后输入 `httpd`。

## 候选摘要

四份分区镜像均为 1,310,720 字节：

```text
8ed8b32594d0ebbe3d5f23a8de1853b9ed3e7492c391361ffaf6f4cf7b1b4abb  images/HW-A/appsbl.partition.bin
912159169a8b61adb40e572b5366a21da68b65d48fcd47f113e65de44c908589  images/HW-B/appsbl.partition.bin
57d13d1dad2a7b7854799b7cc69bddec638dd61df70f1b75a4fd94650fc96d8e  images/HW-C/appsbl.partition.bin
91e9d881ad1b5c88f942069908bfdd7484984521422ad7f1923738d2cc653362  images/HW-D/appsbl.partition.bin
```

源码归档保留原构建时点的来源与历史记录；D 的后续结论以本次精确摘要及验证摘要为准。自行重建产生不同摘要时需独立验证。

## 配套工具

后续完善官方适用最新版 BIN 查询下载、OTA 和界面。官方固件可下载与精确来源规则已适配分别展示，下载成功不自动放行生成或安装。

## 许可

上游 U-Boot 和第三方组件保留原许可；新增构建与封装代码遵循各自声明。

## 发布附件

`H3C-NX30-Uboot-uboot-v0.1.0-pre.1.zip`，22,244,731 字节，包含源码与四份候选。

外层 ZIP SHA-256：

```text
d7781bdb789617a3935058fb50209ad83d955f9fd399b736ecc7830d21adaa61
```

[完整硬件范围](../docs/HARDWARE.md) · [验证说明](../docs/VALIDATION.md) · [安装说明](../docs/INSTALLATION.md) · [工具进度](../tools/bootbridge/README.md)
