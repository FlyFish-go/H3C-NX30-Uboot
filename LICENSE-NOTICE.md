# 许可与上游说明

本项目基于 [chenxin527/uboot-qsdk12.5-build](https://github.com/chenxin527/uboot-qsdk12.5-build)。U-Boot 和其中第三方组件保留原有版权、逐文件声明及许可文本。根目录 [LICENSE](LICENSE) 原样复制自已审公开源码归档的 `source/LICENSE`。

新增构建入口和独立 APPSBL 封装工具按其源文件的 GPL-2.0-or-later 声明提供；该声明仅覆盖相应新增代码，不替换或重新许可整棵上游源码。

完整 U-Boot 许可目录保存在公开源码归档的 `source/u-boot-2016/Licenses/`。源码来源与导出变更见 [SOURCE-PROVENANCE.json](release-metadata/uboot-v0.1.0-pre.1/SOURCE-PROVENANCE.json)。归档未包含两份带 confidential/proprietary 声明的 Qualcomm MBN 辅助脚本，独立封装器不依赖它们。

BootBridge 后续公开时保留其自编代码与第三方文件各自的许可。本次文档入口不包含工具源码或可执行文件。
