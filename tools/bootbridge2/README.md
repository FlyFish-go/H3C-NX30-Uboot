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
