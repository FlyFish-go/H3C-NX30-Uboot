# BootBridge 离线版 GUI 代码审阅 · 第 2 轮

- 审阅对象：R1 修复后的 `desktop/gui/offline_main.c`（2237 行）、`bb_ui_text.c/.h`、`tools/gen_ui_strings.py`
- 方式：静态审阅 + 在 Linux host 上直接编译 `bb_ui_text.c` 做断行实测（沙盒无 Windows）。行号以本次审阅版本为准。

## 0. 结论

R1 的大部分问题确实修好了，已核实：启动 DPI（`primary_monitor_dpi`/`GetDpiForWindow`/`AdjustWindowRectExForDpi`）、初始窗口按工作区 90% 收缩并居中、命名占位符（生成器编码成控制字符，`bb_ui_format` 未知占位符直接失败）、`job_done` 所有分支先检查 epoch、`auto_switched` 只在 `job_done` 设置、弹窗分层命中、`max_scroll = content − viewport`、`TrackMouseEvent`、`OFN_OVERWRITEPROMPT`。

**但新写的断行函数有一个阻断性 bug：英文单词之间的空格全部丢失。** 另外，遮罩是逐像素画的，会很卡。这两项修完之前，不要上实机验收。

## 1. P0（阻断）

### R2-1 英文空格丢失（已实测复现）

`bb_ui_text.c:176` 遇到空白直接 `p++; continue;`，`x` 没有加上空格宽度，下一个单词紧贴着上一个画。host 实测（每个字符按 10px 测量，宽 300）：

```
输入: "Open the router's admin address in a browser and sign in."
输出: |Opentherouter'sadminaddressina|
      |browserandsignin.|
```

英文界面所有段落都会变成这样，中文不受影响。现有 host 测试没覆盖到这种情况。

修复：记录待输出的空白；下一个词放在同一行时，先加上 `measure(L" ", style)`（连续空白折叠成一个）；换行后行首空白丢弃。
测试：① 不换行时，按 span.x 重建出的字符串必须等于原文（空白折叠后）；② 断行位置在空格处，且每行宽度 ≤ width；③ 中英混排（如 “升级到 1.0.47 后”）前后不丢空格。

### R2-2 遮罩逐像素 `SetPixelV`

`offline_main.c:1538–1542`：对客户区隔一个像素调用一次 `SetPixelV`。150% 下约 80 万次 GDI 调用，而且**每次重绘都要跑一遍**（鼠标悬停、Toast、计时器都会触发重绘），弹窗打开时会明显卡顿，点阵效果也不是原型的半透明。

报告里说的 “MinGW C GDI+ 头文件没有矩形填充 API” 不准确：代码里已经在用 `GdipCreateSolidFill` + `GdipFillPath`。`gdiplusflat.h` 也提供 `GdipFillRectangleI`。直接用 ARGB 半透明画刷（如 `0x73` 透明度 ≈45%，配原型的遮罩色）填满整个客户区即可，不需要 msimg32。GDI+ 不可用时，退化为不透明纯色，不要用点阵。

## 2. P1

| # | 问题 | 位置 | 修复要求 |
|---|------|------|----------|
| R2-3 | **超长单词不会断开，直接溢出**。实测宽 200 时 `Redmi-AX3000-1.0.47-BootBridge-very-long-name.bin` 占 490px。第 5 步 `{f}` 文件名、URL、SHA-256 都会超出卡片。 | `bb_ui_wrap` | 单个词宽度大于行宽时，按字符断开（`-`、`_`、`.`、`/` 之后优先断开）；单行字段继续用中间省略。加 host 测试。 |
| R2-4 | **空行导致高度算少**：`"A\n\nB"` 的行号是 0、2，但返回高度是 `line_count × line_height` = 2 行，下面的内容会压到第 3 行上。 | `layout_paragraph`、`measured_paragraph_height` | 高度 = `lines[last].y + line_height`（无行时为 0 或 1 行）；加测试。 |
| R2-5 | **加粗/下划线样式丢了**：生成器把主表里的标签剥掉，生成的 `_runs` 表在 C 里没有用到，所以 `s3warn`/`s6backup` 的强调和 `dropS` 的链接样式都没有了；另外 `paragraph()` 的粗体字体固定是 14px 的 `font_body_strong`，在 13px 正文里会大一号。 | gen_ui_strings.py、`paragraph()`、`layout_paragraph` | 需要样式的文案改用 `_runs` 表绘制（或给 paragraph 传带标签的原文）；为每种基础字号建对应的粗体/下划线字体；下划线段用 teal 色。 |
| R2-6 | **拖放区副标题左对齐**，上下两行都是居中的。 | 1151 | `layout_paragraph` 支持居中：每行 x = (width − 行宽)/2（`bb_ui_wrap` 已有 `x_offset` 参数，但一直是 0；需要输出每行宽度）。 |
| R2-7 | **拖放区虚线看不见 / 悬停态永远不出现**：`rounded()` 先画了一圈实线边框，`dashed_rect` 叠在上面，看起来仍是实线；`state.drag_over` 从来没有被赋值（`WM_DROPFILES` 收不到拖入悬停事件）。 | 1145–1147 | `rounded()` 的边框色传成和填充色一样，只画虚线；高亮改为鼠标悬停（`state.hover == EL_PICK`）。真正的拖入高亮需要 OLE `IDropTarget`（要新增 ole32.dll），本轮不做，在交接里注明。 |
| R2-8 | `WM_GETMINMAXINFO` 用的是 (0,0) 所在的主显示器，不是窗口当前所在的显示器。多显示器、不同缩放时，最小尺寸会算错。 | 2025–2038 | 用 `MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)`，DPI 用 `window_dpi(hwnd)`。 |

## 3. P2

1. 中文避头：`，。、；：？！）」』” %` 不应出现在行首（必要时把上一个字一起带到下一行），`（「『“` 不应留在行尾。
2. `bb_ui_wrap` 失败（span > 512 或行 > 256）时，当前什么都不画，应退化为 `DrawTextW(DT_WORDBREAK)`。
3. 图标（勾、箭头、圆点等）仍是 GDI 画的，可以统一改成 GDI+ 抗锯齿（低优先级）。

## 4. 验收

1. host：新增 R2-1、R2-3、R2-4、居中、避头的测试，全部通过。
2. 契约测试：禁止 `SetPixel`/`SetPixelV` 出现在绘制路径里。
3. Windows 实机截图要求同 R1 第 6 节，**英文截图优先**，重点检查单词间距和长文件名。
