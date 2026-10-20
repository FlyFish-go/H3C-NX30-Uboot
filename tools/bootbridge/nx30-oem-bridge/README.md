# BootBridge：来源绑定的 NAND 安装与独立 OEM 返回

本维护版使用 bundle ABI v2。桌面工具只接受四份原厂 BIN 的完整指纹；设备按来源、启动路由、布局及精确 NAND 元组唯一选择候选，不提供手选 HW/NAND。19 组 NAND、20 种几何、21 种来源格式生成 84 个 U-Boot 候选。当前四份 OEM/Linux 来源支持其中 68 个候选的安装合同；其余 16 个源格式明确拒绝，不能据候选编译完成宣称可安装。具体来源及软件验证结果以交付 `reports/` 为准。

`W25N01GWZEIG` 按完整名称匹配。别名必须来自已绑定的原厂证据，不使用前缀、截短或只看厂商的板类判断。B/D 共用 `mp02.1` 时执行各候选的完整只读合同；若不同载荷仍同时匹配，则关闭安装。

`/usr/sbin/bridge-oem-return` 是摘要固定的独立返回程序。分发失败、无兼容候选、候选损坏或安装程序不可用时，安装资格不再决定 OEM 返回资格。第一阶段保留“返回原厂系统”和诊断下载；返回前仍须独立核验来源、保留 OEM 槽、ENV、布局与两份 BC。损坏、歧义、坏块或内容变化均拒绝。返回只改变必要的 rootfs selector 和 AGE；保留 APPSBL selector 及每副本的其它字节。任何可能已写入的失败均冻结后续提交，不自动重试或重启。

维护网地址为 `http://192.168.10.1/`，DHCP 池 `.100–.149`，不提供 WAN 转发。页面的 45 MiB 备份由 9 MiB 启动区和 36 MiB 持久数据子集组成；启动区包含敏感 ENV/ART，不应公开。备份不含独立的 OEM 40 MiB 槽、全闪存尾部或原始 OOB/ECC。仅保存 ECC 解码普通数据。

所有本轮新候选的实机安装、回退及冷启动均交测试人员执行，硬件状态为 `NOT_EXECUTED`。旧 HW-D 样机的通过结论不转授给新字节。本轮不发布 GitHub。

## 源码及构建

生产代码在 `src/`、`runtime/` 和 `web/`。`tools/build_bundle.py` 构建单个来源的 v2 合包；`tools/audit_bundle.py` 独立解析及审计，不把构建器的状态标记当成通过证据。`tools/build_four_sources_remote.py` 是本轮 WSL 固定路径编排器，输入路径及工具摘要绑定当前内部环境，不是通用安装入口。重新布置环境应使用底层构建器及新的输入清单，不修改或重用旧冻结目录。

构建需要精确原厂 BIN 及其解包 rootfs、ARM GCC 8.4.0、ubinize、mksquashfs、dtc、完整候选归档和 NAND catalog 报告。底层构建器的参数见 `python3 tools/build_bundle.py --help`，候选归档、MANIFEST、CANDIDATES 的三个 SHA-256 均须显式传入。OEM 原件和解包出的专有运行时由开发者自己提供；不随源码公开。完整文件摘要和重建限制见交付 `reports/SOURCE-REBUILD.md`。

测试须指向随候选源码提供的 `h3c_nand_catalog.h`。Host 事务、ARM 隔离、浏览器和 Windows GUI 是不同范围的证据，不能替代实机验证。QEMU 用户态测试会直接执行各 ARM provider 和返回程序；嵌套 ARM `fexecve` 受主机 binfmt 限制，单独记录为未覆盖，另有 host 执行边界测试。测试输出必须使用新目录，禁止覆盖旧证据。
