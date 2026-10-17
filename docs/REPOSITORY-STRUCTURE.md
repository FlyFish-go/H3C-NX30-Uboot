# H3C-NX30-Uboot 仓库目录说明

一个仓库维护 U-Boot 与配套工具，组件分别构建、分别发布。首轮公开内容聚焦 U-Boot；BootBridge 先保留文档入口。

## 主要目录

```text
H3C-NX30-Uboot/
├── README.md
├── LICENSE
├── LICENSE-NOTICE.md
├── u-boot-2016/                  U-Boot 主源码，保留上游布局
├── build-release.sh             H3C A/B/C/D 构建入口
├── release-tools/               独立 MBN / APPSBL 封装工具
├── SOURCE-PROVENANCE.json        已审源码来源与导出变更
├── docs/
│   ├── HARDWARE.md               家族范围与内部硬件分类
│   ├── VALIDATION.md             当前候选的精确实测范围
│   ├── BUILDING.md               构建入口与工具链要求
│   ├── INSTALLATION.md           本次交付与安装边界
│   ├── NAND-ECC.md               NAND / ECC 兼容状态
│   ├── TEST-REPORT-TEMPLATE.md   测试反馈模板
│   └── REPOSITORY-STRUCTURE.md
├── releases/
│   └── uboot-v0.1.0-pre.1.md     首个预发布说明草稿
├── release-metadata/
│   └── uboot-v0.1.0-pre.1/
│       ├── HARDWARE-SCOPE.json
│       ├── HW-D-SUMMARY.json
│       ├── CANDIDATES.json
│       ├── SOURCE-PROVENANCE.json
│       ├── ASSETS.json
│       └── SHA256SUMS.txt
└── tools/
    └── bootbridge/
        └── README.md            同仓配套工具进度
```

`u-boot-2016/`、`build-release.sh`、`release-tools/` 和根级 `SOURCE-PROVENANCE.json` 对应已审源码归档的 `source/` 层。上表聚焦本项目的源码、文档与发布入口，保留的上游其它目录未逐项展开。

## 源码与构建入口

U-Boot 源码继续使用上游的 `u-boot-2016/` 位置，便于比较和整理补丁。H3C 候选使用已审的 `build-release.sh` 与 `release-tools/`，构建方法见[构建说明](BUILDING.md)。

本仓库保留上游提交历史。H3C 改动与工具改动分别维护；首发源码与已审归档核对，来源与构建适配变更单独记录。

上游的 `build.sh`、`staging_dir/` 和 `.github/workflows/` 单独保留。H3C 候选使用上述专用入口，精确工具链不包含在本次源码归档中；本次发布不依赖上游自动发布工作流。

## 配套工具后续目录

工具准备好后，在 `tools/bootbridge/` 下加入桌面工具、桥接运行程序及各自构建说明。具体子目录随实际导入确定，本轮只放进度页，不导入 EXE、DAT 或工具源码。

安装规则、U-Boot 候选和共享 NAND 信息通过精确版本与摘要清单关联。同仓便于同时审阅这些变化，工具版本仍可独立推进。

## 发布附件与版本

`releases/` 保存人可读的发布说明，`release-metadata/` 保存可核验的候选摘要和去敏记录。固件 BIN、完整源码归档、工具安装包和外层 ZIP 作为 Release 附件管理，不放入 Git 日常源码目录。

首个版本为 `uboot-v0.1.0-pre.1`，类型为 Pre-release。工具后续可使用 `bootbridge-v…` 标签，并通过自己的更新清单选择版本。

设备备份、ENV/ART 原件、完整 OEM 原件、凭据、原始日志和私有开发历史继续留在私有工作区。

[返回首页](../README.md)
