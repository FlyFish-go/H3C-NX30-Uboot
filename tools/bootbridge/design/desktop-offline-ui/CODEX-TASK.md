# 给 Codex 的任务（可直接粘贴）

在 `projects/bootbridge-r6-development` 中，按 `design/desktop-offline-ui/UI-HANDOFF.md` 重写 BootBridge 离线版 Windows 桌面端界面（`desktop/gui/offline_main.c`）。

要求：
1. 先完整阅读 `design/desktop-offline-ui/UI-HANDOFF.md`，在浏览器里打开同目录的 `prototype.html` 并对照 `screens/*.png`。原型是视觉和交互的唯一依据，文案以 `strings.json` 为准。
2. 按交接文档 §10 的顺序实现：P0（6 步向导、双语、主题、DPI、设置，同步修改契约测试 / PE 白名单 / 构建脚本）→ P1（机型卡片真实数据、错误状态）→ P2（设备端维护页提示，单独提交）。
3. 不得削弱核心安全语义：只按大小 + SHA-256 唯一匹配、歧义拒绝、输出不覆盖、规则启动时校验一次、哈希可取消而导出不可取消、关闭窗口等待线程。机型卡片只用于显示，绝不参与匹配。
4. 离线版不加入任何网络代码。约束调整（契约测试、导入白名单）是用户同意的，但每一条都要在提交说明里写明原因。
5. 版本号、大小、SHA 从仓库已审计数据取真实值；官方下载页 URL 等找不到的数据不要编造，留空并列出来。
6. 跑通交接文档 §11 的全部验收项；无法验证的（例如没有在真实 Windows 上运行）如实写明。完成后更新 `DEVELOPMENT-HANDOFF.json`。

交付时给我：改动文件清单、约束调整及原因、测试与构建结果、缺失数据清单、需要我在 Windows 上手动检查的项目。
