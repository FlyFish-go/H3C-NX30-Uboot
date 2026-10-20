# BootBridge 离线版桌面端 · UI 重做交接说明

> 交给 Codex 实现。设计已经由用户确认，**视觉和交互以本目录的原型为准**，本文说明原型没法表达的部分：状态、后端对接、平台实现和验收标准。
>
> 本目录：`projects/bootbridge-r6-development/design/desktop-offline-ui/`
> 开发主目录：`projects/bootbridge-r6-development`（相对 QSDK 工作树根）

## 0. 先读这些

| 文件 | 用途 |
|---|---|
| `prototype.html` | 交互原型（单文件，浏览器直接打开）。顶部「交互原型」工具条只用于演示，不进产品。URL hash 可直接定位状态，例如 `#step=4&file=matched&up=1&gen=done&lang=en&theme=dark` |
| `screens/*.png` | 各关键状态的 1:1 截图（100% 缩放，窗口 1040×700）。实现完成后逐张对照 |
| `strings.json` | 中英文完整文案表（从原型抽取，103 个键，中英一一对应）。`{v}` `{m}` `{f}` `{n}` 是运行时变量，`<b>` 表示加粗，`<u>` 表示链接样式 |
| `desktop/gui/offline_main.c` | 现有 Win32 GUI（原生控件、790×570、无 DPI 感知），本次重写的对象 |
| `desktop/gui/test_offline_gui_contract.py` | GUI 静态契约测试，本次需要**有意识地**调整（见 §8） |
| `desktop/tools/check-offline-pe.sh` | PE 导入白名单，本次需要扩展（见 §8） |
| `desktop/catalog/bb_catalog.h` | `bb_bundle_rule` 已有 `display_models`、`official_version`，可直接用于界面 |

`screens/` 清单：`01-model`、`02-download-empty / hashing / matched / wrong-file`、`03-upgrade / upgrade-confirmed`、`04-build-idle / running / done`、`05-flash`、`06-uboot / uboot-faq`、`07-settings`、`08-dark-step2`、`09-en-step3 / en-step4-done`、`10-ota-reserved-step2 / ota-reserved-settings`。

## 1. 目标与范围

把现在的单页「选文件 → 生成」工具，改成**带完整刷机指引的 6 步向导**。面向会用电脑但不懂刷机的普通用户。

**要做**
- 6 步向导，左侧步骤栏，步骤可自由点击跳转；官方固件只选一次，第 2、3、4 步共用。
- 中英双语：首次启动按系统 UI 语言自动选（中文系统 → 中文，其他 → English），用户可在顶栏或设置里切换，选择保存在 EXE 旁的 `BootBridge.ini`。**不写注册表**。
- 浅色 / 深色 / 跟随系统 三种外观。
- 高 DPI 清晰显示（Per-Monitor V2），100%–200% 缩放下布局不变形。
- 为将来的在线功能（工具自更新、规则在线更新、工具内下载官方固件）**预留位置**，离线版隐藏，布局不变（§7）。

**不做**
- 手动选择板卡 / 硬件类型（继续只按文件指纹匹配）。
- 桌面端直接刷机、连接路由器。
- 「校验已有桥接 BIN」功能。
- 任何网络代码（离线版）。OTA 留到在线版，另立目标。

## 2. 窗口与视觉规格

所有尺寸单位为 DIP（96 DPI 下的像素），运行时乘以 `dpi/96`。

### 2.1 窗口
- 初始客户区 **1040×700**，屏幕居中；可调整大小，最小 **920×640**；可最大化。
- 使用**系统原生标题栏**（不要自绘标题栏）。深色主题下用 `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE=20)` 让标题栏变深，旧版 Win10 回退属性 19，失败则忽略。
- 窗口标题：`BootBridge`（英文同）。图标沿用 `resources.rc`。

### 2.2 布局（自上而下）
| 区域 | 规格 |
|---|---|
| 顶栏 appbar | 高 56；背景 `panel`；底部 1px `line`。左：Logo 26×26（圆角 7，teal 底）+「BootBridge」17px/600 +「离线版」胶囊标签。右：语言分段按钮「中文 / EN」+ 设置齿轮按钮（32×32） |
| 左侧步骤栏 | 宽 248；背景 `panel`；右边 1px `line`；内边距 20/14/14。6 个步骤项，每项高约 56、圆角 8；底部为规则状态区（上方 1px 分隔线） |
| 内容区 | 背景 `bg`；内边距 上 34 / 左右 48 / 下 28；**可纵向滚动**（第 3、6 步内容会超出）。内容列最大宽度 760，靠左对齐 |
| 底栏 footer | 高 64；背景 `panel`；顶部 1px `line`；内边距 0/24。左侧放提示或勾选框，右侧「上一步」「下一步/完成」 |

### 2.3 颜色 token
浅色：

| token | 值 | token | 值 |
|---|---|---|---|
| page（原型外部背景，产品不用） | #E7EBEF | bg | #F3F5F7 |
| panel | #FFFFFF | soft | #F6F8F9 |
| ink（主文字） | #1D2A33 | secondary | #53636E |
| muted | #6F7D88 | line | #E4E9ED |
| line-strong | #D4DBE0 | teal（主色） | #148A8A |
| teal-ink | #0F6D6D | teal-hover | #0F7373 |
| on-teal | #FFFFFF | tint | #E6F4F2 |
| tint-line | #CFE9E5 | green / bg / line | #1F7A55 / #E9F6EE / #CFE9DB |
| amber / bg / line | #8A6416 / #FDF6E7 / #F0E2BF | red / bg / line | #B24A3F / #FDF1EE / #F3D7CF |
| track（进度条底） | #E4E9ED | | |

深色：

| token | 值 | token | 值 |
|---|---|---|---|
| bg | #0E1419 | panel | #161F26 |
| soft | #1B262E | ink | #E7EDF0 |
| secondary | #A7B5BD | muted | #8796A0 |
| line | #2A3942 | line-strong | #35454E |
| teal | #4FC3BF | teal-ink / hover | #67D0CB |
| on-teal | #07211F | tint / tint-line | #15322F / #1F3D3A |
| green / bg / line | #78D6AD / #15302A / #214237 | amber / bg / line | #F0CE83 / #2F2A1D / #43391F |
| red / bg / line | #F1A79C / #35211F / #4A2D28 | track | #2A3942 |

### 2.4 字体与字号
- 中文界面：`Microsoft YaHei UI`；英文界面：`Segoe UI`；等宽（哈希、路径、URL、版本号代码块）：`Consolas`。
- 字号（px，按 DPI 换算为字体高度 `-MulDiv(px, dpi, 96)`）：
  - 步骤眉题 eyebrow 12.5 / muted / 500
  - 页面标题 h1 23 / 600（半粗）/ 上 6 下 10
  - 导语 lead 14 / secondary / 行高 1.7 / 下边距 28 / 最大宽 600
  - 正文 14；列表说明、提示框 13.5 / 行高 1.65
  - 步骤栏：标题 14/600，副标题 12/muted
  - 等宽 12.5
- 行高大于单行时用 `DrawTextW(DT_WORDBREAK)` 自行按行高排版，保证段落间距与原型一致。

### 2.5 组件
| 组件 | 规格 |
|---|---|
| 按钮 | 高 34，圆角 6，左右内边距 14，图标与文字间距 6。默认：panel 底 + line-strong 边；主按钮：teal 底 + on-teal 字 + 600；禁用：透明度约 40%；悬停：主按钮 teal-hover，次按钮 soft 底；按下略深。键盘焦点：2px teal 外描边，偏移 2 |
| 大主按钮（第 4 步「生成桥接固件」） | 高 40，左右 20，字号 15/600 |
| 卡片 card | panel 底，1px line 边，圆角 10，内边距 20/22；卡片之间 16 |
| 机型卡片 | 内边距 18/20，圆角 10；左侧 40×40 设备图标块（soft 底、圆角 8）；右侧单选圆圈。选中：tint 底 + 2px teal 边（边宽变化不能导致内容位移） |
| 提示框 callout | 圆角 8，内边距 14/16，图标 16 与文字间距 10。amber（必须注意）、teal（安全提示）、info（soft 底 + secondary 字）三种 |
| 结果框 | 成功：green-bg / green-line，标题 green 600；失败：amber 或 red 系，见各状态 |
| 拖放区 | 1.5px 虚线 line-strong，圆角 10，panel 底；拖入悬停时 teal 虚线 + tint 底 |
| 有序步骤列表 | 序号圆 24（soft 底 + line-strong 边，12/600）；序号与文字间距 16；项之间 1px line 分隔，项内上下 14 |
| 进度条 | 高 6，圆角 3，track 底，teal 填充；右侧百分比 12.5 muted |
| 勾选框 | 18×18 圆角 4；选中 teal 底白勾。底栏勾选框整体是一个带边框的胶囊（未选 line-strong 边，选中 tint 底 + teal 边） |
| 复制按钮 | 图标按钮；点击后在按钮附近显示 1.4 秒「已复制」小提示（toast） |
| 图标 | 线性图标，描边 1.75，16/18/20 三种尺寸，SVG 路径见原型 `ICON` 对象，需要转成绘制路径（§9.3） |

## 3. 全局状态

```
step        1..6                 当前步骤（自由跳转）
guide_model NX30|RT3000|RC3000|RW3000   仅用于显示链接与说明，绝不参与匹配
file        empty | hashing | matched | rejected
reject_kind unsupported | ambiguous | read_error | path_not_allowed
gen         idle | running | done | failed
upgraded    bool                 第 3 步勾选「我已升级到 {v}」
lang        zh | en
theme       auto | light | dark
rules_ok    bool                 启动时 bb_offline_rules_load_verify 结果
```

规则：
- **换文件**（file 回到 hashing）时：`gen` 重置为 idle，`upgraded` 保留不动（升级的是路由器，与本次选的文件无关）。
- 文件匹配到 bundle 后，如果该 bundle 对应的机型与 `guide_model` 不一致：**自动把 guide_model 切到匹配的机型**，在成功结果框下方显示一行 info「已识别为 {m} 的官方固件，已自动切换机型。」（新文案见 §6）。匹配只看文件，机型卡片永远不影响生成。
- 步骤栏完成标记（勾）：第 1 步 = 已离开过第 1 步；第 2 步 = `file==matched`；第 3 步 = `upgraded`；第 4 步 = `gen==done`；第 5 步 = 已离开过第 5 步；第 6 步不打勾。
- 「下一步」可用性：第 3 步未勾选时禁用（左侧显示勾选框）；第 4 步 `gen==running` 时禁用；其余均可用（允许用户先看后面的指引）。第 4 步未生成完时「下一步」为次按钮样式，生成完变主按钮。
- 第 6 步按钮为「完成」：关闭窗口（有任务运行时按现有关闭逻辑处理）。

## 4. 各步骤规格

文案键名对应 `strings.json`。

### 第 1 步 · 选择机型（`01-model.png`）
- eyebrow `stepOf`，h1 `s1t`，lead `s1l`。
- 2×2 机型卡片网格，间距 16。每张：机型名（15/600）+ 可选别名胶囊 + 一行 `needVer`。
  - NX30（别名 RX3000）、RT3000、RC3000、RW3000（别名 RW3010）。别名机型用的是同一份固件。
  - 与 `bb_model` 的对应：NX30 卡 = `BB_MODEL_NX30` + `BB_MODEL_RX3000`；RT3000 = `BB_MODEL_RT3000`；RC3000 = `BB_MODEL_RC3000`；RW3000 卡 = `BB_MODEL_RW3000`（RW3010 只是展示别名）。
  - 某机型在当前规则里没有条目时：卡片置灰，第二行显示「暂未支持」，不能选。
- 卡片下方一行 info 小字 `s1fine`（带 info 图标，12.5 muted）。
- 默认选中：上次使用的机型（ini），否则第一张可用的卡片。

### 第 2 步 · 下载官方固件
- h1 `s2t`，lead `s2l`（`{m}` = 机型显示名，如「NX30（RX3000）」）。
- **指定固件卡片**：标题「`target` · {m}」；三列信息：`kVer` / `kSize` / `kSha`（SHA 显示为前 16 + … + 后 8，等宽，悬停 tooltip 显示完整值，旁边有复制按钮）；分隔线下：URL 只读框（地球图标 + 等宽 URL，超长省略）+ 主按钮 `openBrowser`（调用系统默认浏览器）+ 次按钮 `copyLink`。
- 卡片下方是文件区，四种状态：
  - **empty**（`02-download-empty.png`）：拖放区，上传图标 + `dropB` + `dropS`（「点击选择文件」为 teal 链接样式）+ `dropN`。点击打开文件对话框；支持把文件拖进窗口任何位置（不只是拖放区）。
  - **hashing**（`02-download-hashing.png`）：文件卡片（文件图标 + 文件名 + `hashing`）+ 右侧「取消」+ 进度条 + 下方 `dropN` 与百分比。可取消（沿用现有 `JOB_SELECT` 可取消逻辑）。
  - **matched**（`02-download-matched.png`）：绿色结果框：对勾圆 + `okTitle` + `okSub`；右上 `another`；分隔线下：文件图标 + 文件名 · `sameAs`。
  - **rejected**（`02-download-wrong-file.png`）：amber 结果框：警告图标 + 标题 + 说明；下方一行：文件名 · 大小 · 短 SHA；按钮：主按钮 `another` + 次按钮 `copyDiag`。不同原因：
    - unsupported：`badTitle` / `badSub`
    - ambiguous：标题同 `badTitle`，说明**必须**使用现有原文「此原件对应多个不同输出方案，离线目录未提供唯一合并包，已拒绝生成。」（契约测试锁定）
    - read_error / path_not_allowed：新文案见 §6
- 文件在第 2 步选一次，第 3、4 步直接复用，不再要求选择。

### 第 3 步 · 升级系统（`03-upgrade*.png`）
- h1 `s3t`，lead `s3l`。
- amber 提示框 `s3warn`。
- 卡片内 4 步有序列表 `s3steps`，第 4 项的版本号显示为行内代码样式。
- 底栏左侧：勾选框 `s3check`。未勾选时「下一步」禁用。

### 第 4 步 · 生成桥接包
- h1 `s4t`，lead `s4l`。
- **没有已确认的文件时**：info 卡片 `noFileT` / `noFileP` + 按钮 `goStep2`。
- **idle**（`04-build-idle.png`）：卡片上半：文件图标 + 文件名（15/600）+ 次行「{m} · {v} · ✓`verified`」，右侧链接按钮 `change`（跳回第 2 步并打开文件对话框）；下半：`outFile` 标签 + 只读路径框（等宽）+ 按钮 `change`（打开保存对话框，对话框标题沿用「保存新桥接 BIN（不会覆盖已有文件）」）。卡片下：大主按钮 `gen` + 盾牌图标小字 `genFine`。
  - 默认输出路径：与官方固件同一目录，文件名 `{机型}-{版本}-BootBridge.bin`（例 `NX30-V100R009P02-BootBridge.bin`）；如已存在，自动加 ` (1)`、` (2)`…，**绝不覆盖**（保留现有 `CREATE_NEW` / 不覆盖语义）。
- **running**（`04-build-running.png`）：文件卡片显示输出文件名 + `generating`，进度到导出完成后切换为 `verifying`；进度条；左下 `noClose`。**不可取消**（与现有逻辑一致），「上一步 / 下一步」与步骤栏跳转禁用。
- **done**（`04-build-done.png`）：绿色结果框 `doneT` / `doneS`；`kOut` 输出路径 + 复制、`kSha` 输出文件短 SHA + 复制。下方 info 提示框只有一句 `noUpgrade`。
- **failed**（新状态）：red 结果框，标题「生成失败」，说明显示后端返回的消息；按钮 主「重试」+ 次 `copyDiag`。已写出的半成品文件按现有逻辑清理。

### 第 5 步 · 刷入桥接包（`05-flash.png`）
- h1 `s5t`，lead `s5l`；卡片内 4 步列表 `s5steps`（`{f}` = 实际输出文件名，行内代码样式；未生成时显示默认文件名）；下方 info 提示框 `s5info`。

### 第 6 步 · 安装 U-Boot（`06-uboot*.png`）
- h1 `s6t`，lead `s6l`。
- teal 提示框 `s6backup`。
- 卡片内 4 步列表 `s6steps`；第 1 项说明下方单独一行：链接图标 + 等宽 `http://192.168.10.1/` 只读框 + 主按钮 `openBrowser` + 复制图标按钮。
- 可折叠问答（默认收起）：标题 `faqQ`，展开后 `faqA` + 有序列表 `faqSteps`。
- 最下方 info 提示框 `noUpgrade`。
- 底栏「完成」。

## 5. 顶栏、步骤栏底部与设置

- **步骤栏底部规则状态**：绿点 + `rulesOk`，下一行 `rulesOff`（「规则集 R6 · 离线」，R6 取自目录 `release_set`）。规则校验失败时：红点 +「离线规则校验失败」，下一行显示失败原因；此时第 2 步拖放区禁用，替换为 red 提示框，显示 `bb_offline_rules_load_verify` 返回的消息。
- **语言切换**：顶栏分段按钮即时切换全部文案（包括已显示的结果文字），写入 ini。
- **设置与关于**（`07-settings.png`）：模态对话框，宽约 460，圆角 12，背景遮罩。Esc / 右上 × 关闭。
  - 语言：中文 / English
  - 外观：跟随系统 / 浅色 / 深色
  - 更新：离线版只显示一段说明 `offlineUpd`
  - 诊断：`diagS` + 按钮 `copyDiag`（复制：工具版本、规则集、引擎 ABI、Windows 版本、DPI、当前步骤、所选文件名/大小/SHA、最近一次错误；路径中的用户名替换为 `%USERNAME%`，不包含其他个人信息）
  - 关于：`about`（版本号来自 `resources.rc` 版本资源，不要写死）

## 6. 原型里没有、需要新增的文案

请加入字符串表（中英都要）：

| 键 | 中文 | English |
|---|---|---|
| autoSwitched | 已识别为 {m} 的官方固件，已自动切换机型。 | Recognized as official firmware for {m}. Model switched automatically. |
| notSupported | 暂未支持 | Not supported yet |
| readErrT | 无法读取这个文件 | Can't read this file |
| readErrS | 文件可能正在被占用，或已被移动。请关闭占用它的程序后重试。 | The file may be in use or was moved. Close any program using it and try again. |
| pathErrT | 不支持这个位置的文件 | Files in this location aren't supported |
| pathErrS | 请把文件复制到本地磁盘的普通文件夹（例如「下载」）后再选择。 | Copy the file to a regular local folder (such as Downloads) and choose it again. |
| genFailT | 生成失败 | Build failed |
| retry | 重试 | Try again |
| rulesBadT | 离线规则校验失败 | Offline rules failed verification |
| rulesBadS | 请重新下载完整的 BootBridge 压缩包，并保持 BootBridge.rules.dat 与程序在同一文件夹。 | Re-download the complete BootBridge package and keep BootBridge.rules.dat in the same folder as the program. |
| noUrl | 请在 H3C 官网搜索「{m} {v}」下载。 | Search for "{m} {v}" on the H3C website to download it. |
| saveTitle | 保存新桥接 BIN（不会覆盖已有文件） | Save new bridge BIN (existing files are never overwritten) |
| oneFile | 一次只能选择一个文件 | Choose one file at a time |
| ambiguous | 此原件对应多个不同输出方案，离线目录未提供唯一合并包，已拒绝生成。 | This file maps to more than one output, and the offline catalog has no single combined package. Build refused. |

另外：
- `strings.json` 里的 `opened`、`version`、`about`、`newVer` 中的版本号是示意数据，运行时取真实值。
- 英文文案里不要用中文全角引号。

## 7. 在线功能预留（离线版隐藏）

截图 `10-ota-reserved-*.png` 展示了在线版会出现的位置。**离线版不编译任何网络代码，这些元素不显示，其余布局保持不变。**

| 位置 | 在线版内容 | 离线版 |
|---|---|---|
| 顶栏语言按钮左侧 | 「有新版本」胶囊（teal 点） | 不显示 |
| 步骤栏底部第二行 | 「规则集 R6 · 已是最新」 | 「规则集 R6 · 离线」 |
| 第 2 步 URL 行 | 主按钮「直接下载并校验」，「在浏览器打开」降为次按钮；下载中状态 `downloading` / `dlSrc` | 只有「在浏览器打开」「复制链接」 |
| 设置 · 更新 | 新版本条、工具/规则/固件三行、自动检查开关、检查更新 | 只有 `offlineUpd` 一段说明 |

实现建议：在线相关代码放在单独的源文件（未来的在线目标才链接），离线 `offline_main.c` 及其链接的 GUI 源码里不出现这些代码。布局用常量预留，不要用 `#if` 把在线代码留在离线源码里（契约测试是按源码文本扫描的）。**本次不实现在线版。**

## 8. 后端 / 构建 / 测试需要配合的改动

用户已明确：**后端迁就前端**。下面的约束改动是有意的，请在提交说明里逐条写清原因。

### 8.1 机型卡片数据（不要在 GUI 源码里写死）
卡片需要：卡片 id、显示名、别名、覆盖的 `bb_model` 列表、指定官方版本、官方文件大小、官方文件 SHA-256、官方下载页 URL。
- 建议由发布工程提供一个 `guide-models.json`，由 `tools/build_offline_bundle_catalog.py` 校验后写入生成的目录头文件（例如 `BB_GUIDE_MODELS` 表）。
- 构建时校验：每个卡片的 版本/大小/SHA 必须与目录中某个 bundle 的输入指纹完全一致，否则构建失败。
- **版本、大小、SHA 从仓库里已审计的 bundle 清单 / R6 基线数据取真实值。官方下载页 URL 如果仓库里找不到，不要编造**：该字段留空，界面显示 `noUrl` 文案并隐藏「在浏览器打开 / 复制链接」，并在交付说明里列出缺失项让用户补。
- 原型里的版本号、链接、哈希全部是示意数据。

### 8.2 `desktop/gui/test_offline_gui_contract.py`
保留它守护的核心语义，按新设计调整写法：
1. **禁用词**：从禁用正则中去掉 `ShellExecute` 和 `https?://`。改为新增检查：
   - `ShellExecuteW` 只在一个辅助函数（例如 `bb_open_url`）中出现，且该函数只接受 `http://` / `https://` 开头的地址；
   - GUI 源码中的 URL 字面量只允许 `http://192.168.10.1/`；官方下载页 URL 只能来自生成的目录数据。
   - 继续禁止：`WinHTTP`、`WinINet`、`URLDownload`、`CreateProcess`、`WinExec`、`official_download`、`transport`、`OTA`、`ID_UPDATE`、`ID_DOWNLOAD`、`ID_AUTHOR`、`author`、`github.com`。注意 `author` 是不区分大小写的子串匹配，变量名里也不要出现。
2. **机型选择**：去掉对 `ID_MODEL` / `ui_model` 的禁止，改为断言：展示用的机型变量（例如 `guide_model`）**从不**作为参数出现在 `bb_select_bundle(...)` 与 `bb_export_plan_bin(...)` 调用中；继续保留 `bb_select_bundle(j->catalog,j->size,j->digest,&j->bundle)` 原样调用。继续禁止 `COMBOBOX`、`ID_BOARD`、`ui_board`。
3. **锁定文案**：
   - 「BootBridge · 离线试用」→ 改为新标题/版本标签（`BootBridge` +「离线版」）。
   - 「没有生成伴随文件」→ 新界面不再显示，删除该断言。
   - 「保存新桥接 BIN（不会覆盖已有文件）」、多方案歧义原文、「普通用户权限」保留（现在在字符串表里）。
   - `clear_selection(L"正在核验所选本地原件…")` 的正则改为匹配新的字符串表引用写法，但**保持语义**：清空选择后紧接着 `j->selection_epoch=selection_epoch`。
4. **扫描范围**：测试现在只读 `offline_main.c`。如果拆分多个源文件，改为读取离线目标链接的全部 GUI 源文件和生成的字符串头文件。
5. 以下断言语义不变，必须继续通过：规则只在 `WM_CREATE` 中校验一次且在首次刷新界面之前；哈希任务使用 `FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN` 并拒绝目录/重解析点；`bb_windows_path_allowed(input)`；selection epoch 防止过期结果；只有哈希任务可取消；关闭窗口等待工作线程；不使用 `TerminateThread`；导出走 `bb_export_plan_bin(input,out,j->bundle->plan,1,j->bundle->plan->id)` 且使用 `OPEN_EXISTING` 打开输入。

### 8.3 `desktop/tools/check-offline-pe.sh` 与构建脚本
导入白名单按实际用到的扩展，预计新增：
| DLL | 用途 |
|---|---|
| shell32.dll | `ShellExecuteW`（浏览器打开）、`DragAcceptFiles` / `DragQueryFileW`（拖放） |
| advapi32.dll | `RegGetValueW` **只读** `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize\AppsUseLightTheme` |
| dwmapi.dll | 深色标题栏 |
| gdiplus.dll | 抗锯齿圆角、圆形、图标路径（如采用 §9.2 方案） |

同步修改 `tools/build-windows-offline.sh` 的 `-l` 链接参数和源文件列表；新增源文件后更新 `SOURCE-MANIFEST.json`、`tools/build-offline-generated.py` 的源码快照校验。子系统版本保持 6.01，`asInvoker` 保持不变。

### 8.4 设备端维护页（单独提交，优先级低于桌面端）
`nx30-oem-bridge/web/`：当前未升级到指定官方版本时，维护页只显示笼统的「安装资格未通过 / 暂不可安装」。希望在**原因是官方版本不符**时明确提示：「当前原厂系统不是指定版本 {v}。请先返回原厂系统，升级到 {v} 后重新上传桥接包。」
- 只改提示文案和展示，**不改安装资格判断逻辑本身**。
- 如果后端目前没有区分原因的状态码，先评估并在交付说明里写清楚需要改哪里，不要为了显示文案放宽任何检查。

## 9. Win32 实现建议

C 语言 + MinGW-w64，保持单 EXE + 旁边的 `BootBridge.rules.dat`，不引入 C++ 运行时、不引入第三方 UI 库、不加壳。

### 9.1 结构
- 一个顶层窗口，**整个客户区自绘**（双缓冲：内存 DC → `BitBlt`），不用原生 STATIC/BUTTON/EDIT 控件。
- 简单的「每帧重建」控件列表：布局时生成一组带矩形和 id 的元素，统一处理命中测试、悬停、按下、键盘焦点（Tab / Shift+Tab / Enter / Space / Esc）。
- 内容区独立滚动：滚轮平滑滚动、自绘细滚动条（悬停时出现），切换步骤时滚回顶部。
- 建议拆分文件（名字仅供参考）：`ui_theme.c`（token）、`ui_draw.c`（绘制基元）、`ui_widgets.c`、`ui_steps.c`（6 步布局）、`ui_i18n.c` + 生成的 `strings_generated.h`、`ui_settings.c`（ini）。业务、任务线程、epoch 逻辑留在 `offline_main.c`，以免契约测试失去锚点。
- 字符串表：新增 `tools/gen_ui_strings.py`，从本目录 `strings.json` 生成 `wchar_t` 字符串头文件，避免手抄出错。

### 9.2 绘制
- 文字：GDI + ClearType（`CreateFontW` 按 DPI 缓存 HFONT；`DrawTextW`）。
- 形状：GDI+ Flat API（`GdipCreateFromHDC`、`GdipSetSmoothingMode(AntiAlias)`、路径填充/描边）画圆角矩形、圆、勾、图标；GDI 原生 `RoundRect` 有锯齿，不要用。
- 图标：把原型 `ICON` 对象里约 20 个 SVG 路径转换为绘制命令（可写一个小脚本生成 C 数组），按 24 网格缩放，描边 1.75。
- 悬停 / 按下只需颜色变化，不需要动画；进度条用定时器刷新。

### 9.3 平台细节
- DPI：manifest 改为 `<dpiAwareness>PerMonitorV2, PerMonitor</dpiAwareness>` + `<dpiAware>true/pm</dpiAware>`；处理 `WM_DPICHANGED`（使用系统建议矩形、重建字体）。`GetDpiForWindow` 通过 `GetProcAddress` 获取，Win7 回退到 `GetDeviceCaps(LOGPIXELSX)`。
- 语言：`GetUserDefaultUILanguage()`，主语言为中文 → zh，否则 en。
- 主题：跟随系统时读取 `AppsUseLightTheme`；监听 `WM_SETTINGCHANGE`（`ImmersiveColorSet`）即时切换。
- 配置：`GetPrivateProfileStringW` / `WritePrivateProfileStringW` 读写 EXE 同目录的 `BootBridge.ini`（键：`Language`、`Theme`、`Model`）。目录不可写时静默不保存，不弹错误，**不回退到注册表或 AppData**。
- 打开浏览器：`ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL)`，只传 http/https。
- 复制：`OpenClipboard` / `SetClipboardData(CF_UNICODETEXT)`。
- 文件对话框：继续用 `GetOpenFileNameW` / `GetSaveFileNameW`（comdlg32），过滤 `*.bin`。
- 拖放：`DragAcceptFiles` + `WM_DROPFILES`，只取第一个文件；多个文件时忽略其余并 toast 提示「一次只能选择一个文件」。
- 杀软：此前卡巴斯基误报。保持静态导入、不做动态加载可疑 API、不加壳、补齐 `VERSIONINFO`（CompanyName、ProductName、FileDescription、版本号）、保留 manifest。

## 10. 交付顺序

1. **P0**：新 GUI 全部 6 步 + 双语 + 主题 + DPI + 设置；契约测试 / 白名单 / 构建脚本同步修改；所有现有测试通过。
2. **P1**：§8.1 机型卡片真实数据接入（含构建期一致性校验）；所有错误状态（§4、§6）。
3. **P2**：§8.4 设备端维护页提示。

## 11. 验收标准

- [ ] `desktop/gui/test_offline_gui_contract.py`（更新后）、`desktop/catalog/test_bundle_selector.c`、`desktop/offline/test_offline_rules.c`、`desktop/offline/test_bundle_export.c`、`desktop/tools/test_build_offline_*.py` 全部通过。
- [ ] `tools/build-windows-offline.sh x86` 与 `x64` 交叉编译通过（`-Wall -Wextra -Werror`），`check-offline-pe.sh` 通过。构建需要已审计的 provider-bundle `RESULT.json`（见 `DEVELOPMENT-HANDOFF.json` 指向的基线目录）；**找不到就先完成源码和测试，如实说明，不要伪造输入**。
- [ ] 核心语义无回归：只按大小 + SHA-256 唯一匹配；歧义拒绝；输出绝不覆盖；哈希可取消、导出不可取消；关闭等待线程；规则启动时只校验一次。
- [ ] 视觉与 `screens/*.png` 一致（间距、颜色、字号、圆角），100% / 150% / 200% 缩放下无截断、无模糊。
- [ ] 中英文切换后所有可见文字都变化，无遗漏、无溢出；英文最长文案（第 3 步提示框）能正常换行。
- [ ] 深色模式下无白色闪烁（`WM_ERASEBKGND` 返回 1，首帧即用正确底色），标题栏为深色。
- [ ] 第 3、6 步内容在 1040×700 下可以滚动看全；最大化时内容列不超过 760 宽。
- [ ] 无网络 API 导入；`BootBridge.ini` 是唯一写入的配置位置；不写注册表。
- [ ] 完成后更新 `DEVELOPMENT-HANDOFF.json` 的状态和 `next_work`，并在交付说明里列出：改了哪些约束及原因、缺失的数据（如官方下载页 URL）、未能验证的项目（如未在真实 Windows 上运行）。

## 12. 需要用户确认或补充的

- 四个机型的**真实**指定官方版本、官方下载页 URL（若仓库里没有）。
- NX30 与 RX3000、RW3000 与 RW3010 是否确实使用同一份官方固件（设计按「是」处理）。
- 是否愿意给 EXE 做代码签名（可降低杀软误报，非本次范围）。
