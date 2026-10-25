# BootBridge v2（开发中）

BootBridge 的重写版本。旧版在 `tools/bootbridge/`，本目录稳定前两者并存。

当前内容：

| 路径 | 内容 |
|---|---|
| `runtime/bootconfig.[ch]` | 解析和改写 QCA 双系统 BOOTCONFIG 记录 |
| `runtime/ubi_check.[ch]` | 只读检查一个 rootfs 槽里是否有可启动的 UBI 系统（`kernel` 静态卷 CRC、`ubi_rootfs` 为 squashfs） |
| `runtime/oem_return.[ch]` | 回原厂：把 rootfs selector 指回桥接没有运行的那个槽 |
| `runtime/flash*.[ch]` | NAND 分区访问；全 0xff 的页不编程 |
| `runtime/bb_oem_return_main.c` | 设备端工具 `bb-oem-return check|commit` |
| `tests/` | 主机测试，含模拟 NAND 和写入失败注入 |

## 回原厂的规则

- 只要求：原厂槽的 UBI 系统完好；两份 BOOTCONFIG 至少一份能解析，且所在块不是坏块。
- 不看 ENV、规则、候选、板型和安装结果。
- selector 0 对应较低偏移的槽，1 对应较高的槽。
- 先写 BOOTCONFIG1，再写 BOOTCONFIG；每份写完都读回比较。两份的 age 都设为原来两份中较大的值加 1。每份其余字节保持各自原样；无法解析的那份用另一份重建。

## 构建与测试

```bash
make test        # 需要 BB_FIXTURES 指向私有的真实分区样本，见 tests/test_oem_return.c
make arm ARM_CC=arm-openwrt-linux-gcc
```

## 当前开发验收

- 规则包使用 BBPK v1：规则文本与 LZMA 候选归档，SHA-256 校验；无签名。
- mp02.1 用 ART 中的 FEM 标志区分 B/D；未知组合拒绝安装。
- WPS/Mesh 使用原厂 gpio-button-hotplug 内核事件；长按 3 秒由独立 bb-keyd 调用 `bb-oem-return commit-reboot`。
- HTTP 备份、安装、返回、重启与独立返回工具共用内核文件锁；锁忙时拒绝操作，进程退出自动释放。
- 安装前完整读回备份 ZIP 并校验大小、SHA-256；写入 APPSBL 前要求有效 BOOTCONFIG 的 APPSBL 选择位明确且一致。
- 两份 APPSBL 均按擦除块读回校验，最后切换系统启动选择位。备用 APPSBL 写入失败时保留当前副本。
- 调试用原始分区与规则包下载接口不随本版保留；设备事实仍可通过诊断下载获取。

主机测试需要外部 `BB_FIXTURES`、`BB_RULES`、`BB_PACK`；`make test` 也覆盖跨进程锁竞争和备份损坏。
网页测试入口为 `web/tests/e2e.js`，逐个校验四种官方固件的浏览器输出。
设备页面测试为 `tests/e2e_device.js`；仅在明确授权的台架上使用 `--install`，它会实际写入 U-Boot。
实机通过范围以对应私有验收报告为准，不由主机测试数推断。
