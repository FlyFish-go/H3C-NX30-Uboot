# NX30 R012：网页填 SN → 本地命令下载 → 网页选文件

状态：草案 v0（2026-10-20），沙箱已测；**Windows PowerShell 5.1 与真实 SN 尚未实测**。

## 为什么不能纯网页
| 接口 | 浏览器（file:// 与 https 页面均实测） |
|---|---|
| `getGwBestVerNew`（版本查询） | ✅ 可调用、可读结果 → 用于网页 SN 预检 |
| `getGwDownloadNew`（签名下载链接） | ❌ 预检 403、响应无 CORS 头；text/plain、表单 → `Content type not supported`；GET → 405 |
| OSS 固件对象 | ✅ GET/Range 可读（Content-Range 不暴露，用 Content-Length） |

R012 的 VerURL 是约 4 分钟有效的 OSS 签名链接，只能由 `getGwDownloadNew` 给出，因此“查链接+下载”必须在浏览器外执行。

## H3C 返回码（仅用虚构 SN 观察，未用真实 SN）
| retCode | 观察到的条件 | 网页提示 |
|---|---|---|
| 0 | 真实 SN（2026-10-19 用户实测） | 已识别，官方提供 R012 |
| 18 | 缺 gwSn 或 gwVer / gwSn 为空 | — |
| 20 | SN 不是 20 位字母数字（长度 8/16/18/19/21/22、含 `-` 或空格） | 本地已拦截 |
| 24 | 20 位格式正确但 H3C 不认识（版本查询） | “H3C 不认识这个 SN，请逐位核对” |
| 26 | 同上（下载查询） | 命令输出 retCode=26 |

结论：H3C SN 规则为**恰好 20 位 [0-9A-Z]**。网页去空白、转大写、按 4 位分组显示。

## 流程
1. 网页输入 SN → 本地格式检查（20 位）→ `precheckSn()` 调版本查询（先 R011 后 R012，与桌面版一致）。
2. `ok` → 生成命令（SN 已嵌入，只含 [0-9A-Z]，无注入可能）；`unknown-sn` → 不生成；`network` → 允许生成并提示“终端里会再查一次”。
3. 用户复制命令：Windows → `ps51.tpl`（Windows PowerShell 5.1 / 7）；macOS / Linux → `sh.tpl`（zsh / bash 同一条）。
4. 命令：查链接（R011→R012）→ 校验 retCode=0、gwBestVer=R012、主机为 `h3c-magic.oss-cn-hangzhou.aliyuncs.com` → http 改 https → 下载到“下载”文件夹的 `NX30V100R012.bin.part` → 校验 31981568 字节 + SHA256 `61fbc2a3…e922` → 通过才改名，失败即删除。
5. 网页选择 `NX30V100R012.bin`（本地读取，不上传）→ 再校验大小与 SHA → 生成。

## 命令约束（修改时必须保持）
- 单行；不得含 `exit` 于外层（PowerShell 用 `& { … return }`，sh 用子 shell `( … exit )`），否则会关掉用户终端。
- sh：不得有 `#` 注释（zsh 默认不认交互注释）、不得有 `!`（历史展开）、`=https` 必须加引号（zsh EQUALS 展开）、不用 `-#`（extended_glob）、变量一律 `${}`（zsh 允许非 ASCII 标识符）、行首空格（HISTCONTROL=ignorespace 时不进历史）。
- PowerShell：兼容 5.1（`-bor 3072` 开 TLS1.2、`$ProgressPreference` 关进度条、`-UseBasicParsing`），不用 `iex`、不下载执行任何程序。
- 只连 `magic.h3c.com` 与 `h3c-magic.oss-cn-hangzhou.aliyuncs.com`。

## 已测（沙箱，2026-10-20）
- `tests/run-mock-tests.sh`：bash、zsh、pwsh 7.4 各测成功与校验失败删除，7/7 通过（模拟查询接口；固件为官方 OSS 上的 RC3000 公开对象，https 下载为真实下载）。验证了 R011→R012 回退、`\/` 与 `\u0026` 反转义、http→https。
- 三个模板对真实 H3C 接口用虚构 SN：均正确输出 `retCode=26`。
- `tests/precheck.test.mjs`：格式检查、真实接口虚构 SN → `unknown-sn`、模拟 ok/other-version/network。
- Chromium 无头：file:// 与 https 页面均可调用版本查询、读 OSS Range；下载查询被拦截；file:// 是安全上下文，`crypto.subtle` 可用。

## 待实测
- [ ] Windows PowerShell 5.1：虚构 SN 出错路径、中文显示（DSH 可在 WSL 里用 powershell.exe 跑）。
- [ ] 真实 SN：Windows PowerShell 粘贴运行（Windows Terminal 与旧控制台各一次）、WSL/Linux bash。
- [ ] macOS（BSD sed、shasum）：有 Mac 的测试者跑一次。
