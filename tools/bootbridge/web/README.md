# BootBridge 网页版（web/）

在浏览器里完成桌面版的六步向导：选机型 → 拿到指定官方固件 → 升级 → **在本机浏览器里生成桥接包** → 刷入 → 维护页安装 U-Boot。
规则与桌面版 R5 完全相同（同一份 `bb_offline_catalog_generated.h` + `BootBridge.rules.dat`），生成算法是 `desktop/src/bb_export.c` 中 `bb_export_plan_bin()` 的逐条移植，任何文件都不会上传。

## 目录

| 路径 | 作用 |
|---|---|
| `site/` | 静态源码：`index.html`、`assets/app.js`（向导 UI）、`assets/engine.js`（生成引擎）、`assets/nx30.js`（NX30 SN 预检与命令）、`assets/app.css`、`assets/icon.svg` |
| `strings-web.json` | 网页版文案，覆盖/补充 `design/desktop-offline-ui/strings.json` |
| `nx30-download/` | NX30 下载命令模板（`ps51.tpl`、`sh.tpl`），构建时嵌入网页 |
| `tools/build_site.py` | 由发布输入生成完整站点（仅用 Python 标准库） |
| `tests/` | 差分测试、单元测试、构建核验、浏览器端到端测试，`run-tests.sh` 一键运行 |
| `dist/` | 构建产物（不入库）：`dist/site/` 和 `dist/BootBridge-Web-<版本>.zip` |

## 构建与核验（WSL）

```bash
bash tools/bootbridge/web/tests/run-tests.sh          # 单元测试 + 真实构建 + 与桌面 C 引擎逐字节对照
bash tools/bootbridge/web/tests/run-tests.sh --unit   # 只跑不需要发布输入的测试
E2E=1 bash tools/bootbridge/web/tests/run-tests.sh    # 另跑 Playwright 界面测试（需 node 的 playwright 和 chromium）
```

**本仓库不包含发布输入。** 规则数据与官方固件需要自行准备，用环境变量指定：

| 变量 | 内容 |
|---|---|
| `HEADER` | `bb_offline_catalog_generated.h`，见 `tools/bootbridge/rules/H3C-BOOTBRIDGE-NAND-OEM-20261022-R5/` |
| `DAT` | `BootBridge.rules.dat`，从 `bootbridge-v0.1.0-pre.3` Release 下载 |
| `INPUTS` | 官方固件目录（自行从 H3C 下载，文件名形如 `NX30-original.bin`） |
| `OUT` | 站点输出目录（默认 `web/dist/site`） |
| `REPO_URL` | 站点里显示的仓库地址 |
| `BUILD_LABEL` | 站点里显示的构建标签 |

其余素材（`desktop/guide-models.json`、`design/desktop-offline-ui/strings.json`）已在本仓库中。

`build_site.py` 会：解析 header 并重新执行 `valid_plan()` 规则；核对 DAT 大小与 SHA-256 pin；核对 guide 与 pin 一致；
确认每个 bundle 的数据在 DAT 中按操作顺序连续且恰好覆盖整个 DAT；按 bundle 切出 `data/<id>-<sha12>.js`；
用独立的 Python 参考实现对每份找得到的官方固件重建并核对输出 SHA-256；最后写 `SHA256SUMS` 和离线 ZIP。

`run-tests.sh` 还会用同一份 header + DAT 编译桌面版 C 引擎，对四个 bundle 分别用 C 引擎和网页引擎（WebCrypto 与纯 JS 两条 SHA 路径）生成，要求逐字节相同。

## 本地预览

```bash
python3 -m http.server -d web/dist/site 8000     # 打开 http://localhost:8000
```

也可以直接双击 `dist/site/index.html`（file:// 下规则数据用 `<script>` 加载，已在 Chromium 实测）。

## 发布到 GitHub（需余飞扬确认后再做）

- 目标：GitHub 账号 **FlyFish-go**，源码公开。站点产物是 `dist/site/` 的全部内容（静态文件，可直接用 GitHub Pages）。
- 数据文件约 11.5 MB × 4，均低于 GitHub 单文件限制；需要用 git 推送（不能用只支持文本的 API 逐个上传）。
- `SHA256SUMS` 和 ZIP 的 SHA-256 一起写进 Release 说明，方便用户核对。
- 规则发布号目前是 `H3C-BOOTBRIDGE-NAND-OEM-20261022-R5`。

## 网络与隐私

页面 CSP 只允许连接自身、`magic.h3c.com` 和 `h3c-magic.oss-cn-hangzhou.aliyuncs.com`：

1. RT3000 / RC3000 / RW3000：「直接下载并校验」从 H3C 官方 OSS 下载（OSS 允许跨域读取），下载后按大小和 SHA-256 校验。
2. NX30 / RX3000：R012 没有公开链接。网页用 `getGwBestVerNew` 预检 SN（浏览器可读）；拿签名链接的 `getGwDownloadNew`
   浏览器无法读取，所以网页生成嵌入 SN 的命令（Windows PowerShell 5.1 / macOS·Linux），由用户本机下载并校验，再拖回网页确认。
   SN 只校验为 20 位 `[0-9A-Z]` 后才会写进命令；诊断信息不含 SN；网页不保存 SN。
3. 规则数据只从本站加载，加载后先核对长度与 SHA-256，再参与生成。

生成过程：校验计划 → 校验官方固件 SHA-256 → 校验规则数据 SHA-256 → 应用 COPY/XOR/LITERAL → 再次校验官方固件 → 校验输出大小与 SHA-256 → 才交给浏览器保存。任何一步不通过都不会保存文件。

## 浏览器

需要 ES2017 左右的能力（Promise、Uint8Array、Blob、FileReader）。已实测 Chromium（http 与 file://、桌面与手机宽度、浅色/深色、中/英文）。
Firefox、Safari、Edge 尚未实测。老浏览器会停留在页面自带的静态提示。生成时峰值内存约 100 MB。

## 已知限制

- NX30 必须在电脑上运行一次命令下载官方固件；命令输出目前只有中文。
- 浏览器决定保存位置和重名处理（例如自动加 “(1)”），网页无法指定路径。
- 网页版没有自动更新；在线访问即为当前发布版，离线 ZIP 需手动下载新版本。
