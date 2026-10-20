# H3C-NX30-Uboot v0.1.0-pre.2（预发布）

> [!WARNING]
> 本版候选正在由测试人员上机测试，尚未取得整机验收结论。不要把 APPSBL 分区文件直接上传到原厂升级页；普通用户请使用 [BootBridge](../tools/bootbridge/README.md) 并等待测试结论。

## 概要

| 项目 | 内容 |
|---|---|
| 源码提交 | 本仓库 `main` 上导入 U-Boot 源码的那个提交（见仓库提交历史） |
| 嵌入的来源修订号 | `b0383f96b3282b0aedbd9765464778a9cf596836`（源码快照 SHA-256 的前 40 位） |
| 候选 | HW-A / HW-B / HW-C / HW-D × 21 个 NAND 配置，共 84 个 |
| APPSBL 分区 | 每个 `0x140000`（1,310,720 字节） |
| 工具链 | OpenWrt GCC 8.4.0 `r16847-f8282da11e` + binutils 2.34 |
| 交付方式 | 候选随 BootBridge 规则 `H3C-BOOTBRIDGE-NAND-OEM-20261020-R4` 安装 |

## 相对 pre.1 的变化

- 源码基线从 pre.1 的四候选树更新为 NAND/OEM 修复后的 84 候选树（按 NAND 型号与 ECC 配置分别构建）。
- 四类硬件的恢复地址统一为 `192.168.10.1/24`，电脑端 `192.168.10.2`；U-Boot DHCP 地址池为 `192.168.10.100`–`192.168.10.200`。地址只在内存中设置，不写回原厂 ENV。
- 非 H3C 的上游板型保持原来的 `192.168.1.1`。
- 工具链改为 GCC 8.4.0 / binutils 2.34；GCC 5.2.0 构建本版源码会得到不同结果，不要使用。
- 构建时用 `-ffile-prefix-map` 把源码根映射为 `/build/`，二进制中不再含构建机的本地路径，构建结果也不再依赖源码所在目录。

## 摘要

84 个候选的 MBN 与分区摘要见 [`release-metadata/uboot-v0.1.0-pre.2/CANDIDATES.json`](../release-metadata/uboot-v0.1.0-pre.2/CANDIDATES.json) 与 [`SHA256SUMS.txt`](../release-metadata/uboot-v0.1.0-pre.2/SHA256SUMS.txt)。来源与去敏说明见 [`SOURCE-PROVENANCE.json`](../release-metadata/uboot-v0.1.0-pre.2/SOURCE-PROVENANCE.json)。

## 验证状态

- 离线：84/84 构建通过。与 NAND/OEM 修复版候选（快照 `043fd74b5db7`）逐项核对结果如下：
  - 函数层面：四类硬件都**只有 `do_network_check` 一个函数变大（均为 +52 字节）**，函数总数不变，没有新增或消失的函数。
  - 段层面：`.text` 增加 32 字节（HW-D 为 64 字节，多出的是对齐填充）；`.rodata` 增加 154–168 字节，原因是版本串内容变化（HW-A/HW-C 另含 `ipaddr`/`serverip` 由 `192.168.1.1/.2` 改为 `192.168.10.1/.2`）。`.data`、`.bss`、`.dtb`、`.u_boot_list` 等其余段完全不变。
  - 体积：每个 `appsbl.mbn` 比对应旧候选大 192 字节；`appsbl.partition.bin` 始终是 `0x140000`，大小不变。
  - `autoboot_command` 大小与指令数都不变（108 字节 / 39 条），只有一条跳转到 `h3c_release_autoboot` 的相对地址因前置函数变大而后移，逻辑没有变化。
- 本版构建另加了-ffile-prefix-map：上面的比较不计源码路径字符串；二进制里的源码路径已变为 /build/，所以摘要与修复版候选不同。
- 实机：本版尚无实机结论，测试中。pre.1 的 HW-D 单样机结论不自动转授给本版。

## 构建

见[构建说明](../docs/BUILDING.md)。自行构建得到不同摘要时，需要自己重新验证。
