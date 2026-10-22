# BootBridge R5 交接文档（暂停点）

**写于**：2026-10-22 13:50（Asia/Singapore）
**状态**：用户要求暂停。R5 构建 1/4 完成，未发布任何东西。
**权威位置**：本目录及 `/home/yu/Project/RT3000/publish/H3C-NX30-Uboot`（WSL 侧）。VM 上的 `work/` 只是副本。

---

## 0. 一句话结论

真正的根因**不是**我之前以为的"准入表缺 84 条"，而是 `rc_c_runtime_platform()` 里
硬编码了一个**根本不在 `policies[]` 里**的候选摘要，导致该分支是死代码；
于是每台 HW-C 设备在出厂 ENV 没有 `machid` 时，都会用自己的 payload 被自己的
provider 拒绝（`BII_ENV_MACHID`），安装永远不生效，设备继续跑旧 U-Boot 和 192.168.1.1。

修复已提交并推送：**`2bbf85a`**（净改动 = 1 个 C 文件 19 行 + 1 个测试文件）。

---

## 1. 用户报告的现象与证据

测试者（HW-C，RC3000）现象：
- 设备启动日志仍是 `U-Boot 2016.01-h3c-common4-043fd74b5db7`
- 维护页仍在 `192.168.1.1`，不是期望的 `192.168.10.1`
- 网页版显示 `Build bootbridge-v0.1.0-pre.2 测试版`、作者链接正常 → 网页本身是新的，**设备侧没换**

`043fd74b5db7` 是我们自己的**基线快照**（见 repo `SOURCE-PROVENANCE.json`
`baseline_snapshot_sha256`），说明更早的某次安装成功过，而 R4 的安装被拒了。

聊天记录与截图原件：`/tmp/chatlog/`（VM 临时目录，未纳入权威仓库；如需保留请拷入本目录）。

---

## 2. 根因（已实测证明）

`bridge_install_identity.c` 中：

```c
if(machids!=1 && !(!machids && (policy->allow_missing_env_machid || runtime_platform_verified)))
    return BII_ENV_MACHID;
```

出厂 RC3000 R013L01 的 APPSBLENV **没有 `machid=` 键**，R4 生成的策略行是
`allow_missing=0`，所以只能靠 `runtime_platform_verified` 这条"用设备树 C2 路由补证"的路径。

而该路径原本是：

```c
eq(candidate,"e768be512e985980070c811cea56f44a822bee3a938bb91f821454764e89cd84")
```

`e768be51…` **在 `policies[]` 里不存在**（我核过：表里 0 次，全文件除该比较外 0 次）。
所以这个分支永远不可能为真 → 所有 R4 候选全部 `BII_ENV_MACHID`。

### 实测矩阵（21 个真实 R4 HW-C 候选 + pre.1 候选 = 22 例）

| 场景 | 修复前 | 修复后 |
|---|---|---|
| 精确 C2 路由 + 无 machid ENV | **0/22 通过** | **22/22 通过** |
| 错误路由 | 22/22 拒绝 | 22/22 拒绝 |
| ENV 里有 machid 但错误 | 22/22 拒绝 | 22/22 拒绝 |
| board A 交叉 | 拒绝 | 拒绝 |

独立验证者（另一个 agent，未复用我的数字）结果一致：
C 板 92 例，修复前 **82/92**，修复后 **92/92**；A/B/D 板两侧都是 92/92。

---

## 3. 我做错又改回来的事（必须记录，避免重犯）

我先前提交了 **`8465ced`**，把 84 条 R4 摘要硬编码进 `policies[]`，并声称那是根因。
**那是错的**：

1. 每个 provider 本来就通过 `BII_EXTRA_CANDIDATE_POLICIES` 编译进**自己那一条**生成行
   （实测 272/272 个 provider 都恰好 1 行）。所以缺表不是问题。
2. 更糟的是，加了 84 条表之后，配合运行时修复，一个 provider 会**同时接纳同板其他候选**——
   独立验证者复现：20/21 个其他 C 板候选被错误放行（table84 变体 72/92）。
   这比原 bug 更危险。

已 `git revert`，现为 **`2bbf85a`**。净 diff 相对 `1bb3ded` 只有：
- `rc_c_runtime_platform()` 不再比对那个死摘要，改为调用新helper
  `policy_admits_c_candidate()`（扫 `policies[]` 里 board=C 且 machid=08040101 且摘要匹配的记录）
- 新增回归测试 `tests/test_install_identity.py::RuntimeCandidateAdmissionTests`

**教训**：`policies[]` 是"哪个摘要是可准入的"的唯一权威；运行时补证只能替缺失的
machid 兜底，**不能**自行扩大可准入集合。

---

## 4. 另一个被我误判、已澄清的问题

我一度认为 `mksquashfs` 不支持 `-mkfs-time`/`-all-time` 而阻塞重建。**这是我判断错误**：

- WSL `/usr/bin/mksquashfs` = squashfs-tools **4.5**，两个参数都支持
- 其 sha256 `403080bcd98ea7be2cbb261a10e99a89571e3a3beed6ab6cc3b88e01a0b51053`
  与 R4 `RUN-SUMMARY.json` 里 pin 的 mksquashfs **完全一致**
- 功能性验证：两次构建产出字节相同的镜像，`unsquashfs -ll` 显示所有 inode 都是固定 epoch；
  负向对照 `-definitely-not-a-flag` 返回 exit 1

结论：`build.py` 在这点上无需任何修改。

---

## 5. 构建基座问题（重建时最容易再踩的坑）

**不能**用 `evidence/nand-oem-repair-20261017/rebuild-src-b0383f96`（"tree A"）当基座，也不能用
`work/review-followup-20261017/continue-r3/source-final-v2/source`（"tree B"）直接当基座。

两个老树的共同致命问题：`bii_check_public_candidate_runtime()` **零调用者**。
tree A 的 `bridge_install.c` 两处都用严格入口 `bii_check_public_candidate()`；
tree B 连 `rc_c_runtime_platform` 都没有。也就是说把修复文件拷进这两棵树，
**编译会过、AUDIT 会 PASS、但修复是死代码**，出厂仍然坏——和 R4 一模一样。

另外 tree A 的 runner 把 release set 硬编码成 `nand-oem-repair-20261017`，
没有 `--release-set` / `--root-binding-gate-config`，**根本无法表达 R5**。
而 R4 的 `RUN-SUMMARY.json` 里有这两个字段 → R4 出自 repo 血统，不是 tree A/B。

### 目录布局契约（两个坑，都会在浪费 40 分钟后才炸）

1. `tools/nand_schema.py:9`
   `CATALOG = Path(__file__).resolve().parents[2] / "u-boot-2016/include/h3c_nand_catalog.h"`
   → 要求 `<base>/nx30-oem-bridge/` 与 `<base>/u-boot-2016/` **同级**。
   repo 里 `u-boot-2016` 在仓库根、`nx30-oem-bridge` 在 `tools/bootbridge/` 下，
   **布局不满足**，直接从 repo 跑会 `FileNotFoundError`。

2. `tools/audit_bundle.py:1121` 用 `parents[3]`，且硬编码 `"source/"` 前缀：
   `SOURCE_IDENTITY_EVIDENCE[...]["path"] = "source/nx30-oem-bridge/tools/fixtures/RX-OTA-SOURCE-EVIDENCE.json"`
   → 要求 `parents[3]` 下面有个**名字字面叫 `source`** 的子目录装桥接树。
   所以基座目录本身必须命名为 `source`，否则 AUDIT 阶段才炸。

### 实际可用的基座（本次已验证通过）

```
/home/yu/Project/RT3000/work/r5-build-src-20261022-v2/source/
    build-release.sh, release-tools, u-boot-2016      <- 取自 tree B
    nx30-oem-bridge                                    <- repo 血统（含修复）
    nx30-oem-bridge.OLD-treeB                          <- 被替换下来的 tree B 桥接树，保留未删
```

预检全部通过：
- `nand_schema.profiles()` 返回 20（不是 FileNotFoundError）
- `audit_bundle` 的 `parents[3]` 证据路径存在
- `u-boot-2016/include/h3c_nand_catalog.h` sha `3cee0f2279e9473d799ab73fb3e28a9dd8a16c1c7ca8c9af0b638a0e5c1a2db4`（repo / tree B / R4 base 三处一致）
- `bridge_install_identity.c` sha `4e68462161f1ccfbded9ecec3ab094c79b8bfbb70e36ab22c1d6ce5edeba964c`（== HEAD）
- `bridge_install.c` 有 4 处 runtime 入口引用；`bridge_install_identity.h` 有声明
- `policy_admits_c_candidate` 出现 2 次；`e768be51` 出现 0 次
- runner sha `f59cbfcf6dcb82a7605bedb7eedc97cb90f7ce5ad4da56ec52b7af5a9b49ee70`

---

## 6. R5 构建现状（暂停处）

**输出根**：`/home/yu/Project/RT3000/evidence/bridge10-remap-20261021/bundles-r5/`
**release set**：`H3C-BOOTBRIDGE-NAND-OEM-20261022-R5`
**命令**（在 `.../v2/source/nx30-oem-bridge` 下，setsid+nohup，`--jobs 1` 串行）：

```
python3 tools/build_four_sources_remote.py \
  --input-manifest /home/yu/Project/RT3000/evidence/nand-oem-repair-20261017/inputs/BUNDLE-INPUTS-v2.json \
  --candidate-archive /home/yu/Project/RT3000/evidence/bridge10-remap-20261020/nand-uboot-candidates-b0383f96b328-v2.tar.gz \
  --candidate-archive-sha256 2e0972448bf39b2a37a814b725fe9514e8e7e03705ed12955c82a9cca1805008 \
  --candidate-manifest-sha256 14a25186205e35c03a9632ecabe2492a2ee474ccde48fd33fbcd40026e6b6195 \
  --candidate-descriptors-sha256 798aed04258f1966e2b5aee0cd459fbb5177d42efab27fdc17a38e94af7a09d9 \
  --root-binding-gate-config /home/yu/Project/RT3000/work/review-followup-20261017/continue-r3/GUEST-GATE-CONFIG-FINAL-V2.json \
  --release-set H3C-BOOTBRIDGE-NAND-OEM-20261022-R5 \
  --output-root /home/yu/Project/RT3000/evidence/bridge10-remap-20261021/bundles-r5 \
  --jobs 1
```

**进度（暂停时已手动 TERM 停止，无残留进程）**：

| 机型 | provider | BUILD-REPORT | AUDIT |
|---|---|---|---|
| RC3000 | 68/68 ✅ | ✅ 已生成 | ❌ 未跑（runner 串行，被暂停打断） |
| RT3000 | 0 | — | — |
| RW3000 | 0 | — | — |
| NX30 | 0 | — | — |

`RUN-SUMMARY.json` 的 `status` 仍是 `RUNNING`（因为被中途停止），`failure` 为 null。

### RC3000 已产出的结果（可用）

- candidate：`17,695,572` B，sha256 `514d0f29071049132aa2e758dde46335e5cebfb73a2f6155cdc0d708dbe5ce34`
- 每板 17 个 provider，共 68（A/B/C/D 各 17）
- `status`: `OFFLINE_LAB_CANDIDATE`（离线实验候选，非实机验收）
- 文件名 `RC3000V100R013L01-BOOTBRIDGE-BUNDLE-TRIAL.bin`（与 candidate sha 相同）

### 关键的"修复真的在产物里"指纹（RC3000 已实测）

| 检查 | 结果 |
|---|---|
| provider 总数 | 68 |
| 含 C2 路由串 `qcom,ipq5018-mp03.1-c2` | **68 / 68** ← 证明 runtime 路径在产物里可达 |
| 含陈旧摘要 `e768be51…` | **0** ← 证明旧死代码没进产物 |
| provider 大小 | 196,044 B（R4 是 191,948 B） |

这一项是**必须**的：老两棵树即使 AUDIT PASS 也会是死代码，只有这个指纹能证明修复真的被编译进可执行文件。

---

## 7. 恢复后要做什么（按顺序）

1. **重建其余 3 个机型**。runner 要求输出根不存在，所以要么整个 `bundles-r5` 删掉重跑（会重做 RC3000），
   要么手动只跑 RT3000/RW3000/NX30（需自己拆 runner 逻辑，风险高，不推荐）。
   推荐：删掉 `bundles-r5` 整体重跑，`--jobs 1`，约 40 分钟。
2. **确认四个机型都 AUDIT PASS**，并对每个机型重跑第 6 节的三个指纹检查。
3. **重新生成规则集**：从 R5 bundle 生成 `bb_offline_catalog_generated.h` + `BootBridge.rules.dat`，
   确认真实 release id（不要沿用 R4 名字，除非规则数据确实字节相同）。
4. **重建网页版 + Windows 离线包**（`tools/bootbridge/web/tools/build_site.py`）：
   - `WEB_VERSION` 保持 `"0.1.0"`（用户明确选过 A，这是格式版本）
   - `--build-label "bootbridge-v0.1.0-pre.3 测试版"`
5. **发布 `bootbridge-v0.1.0-pre.3`**，7 个资产，`SHA256SUMS` 全量校验，
   部署 gh-pages 并**实际抓取页面确认新 label**（不要假设 Pages 已重建）。
6. **提交作者只能是 `FlyFish-go <2541290956@qq.com>`**，任何地方都不要 AI/co-author 署名。

---

## 8. 未决事项 / 待决策

1. **neutral-archive 分支的潜在缺陷**（独立验证者发现，尚未修）：
   `build.py:1327` 的 `if candidate_descriptor is not None:` 分支会调
   `candidate_profile_codegen.render_headers()` 并置 `candidate_profile_codegen=True`；
   而 `elif args.candidate_archive is not None:`（中性归档，line ~1386）**不调**、也不置位。
   走该分支产出的 provider 将**没有任何** `BII_EXTRA_CANDIDATE_POLICIES`，只剩 4 条 pre.1 静态行，
   于是自己的 R4 payload 会被 `BII_POLICY` 拒绝。
   目前休眠（`build_bundle.py` 总是设置 `candidate_descriptor`），但一旦有人直接用 `build.py`
   走中性归档就会中招。**需要决定：本次修，还是单独记录**。
2. **4 条冻结 pre.1 摘要在每个 provider 里**：所以 R4 HW-C provider 也会接纳 pre.1 的 C 候选
   （在同样无 machid 的路由下）。这是**既有设计、不是本次回归**，但如果要求"一个 provider 只认自己的
   payload"，这一点不满足。建议明确记录为已知接受项，不要偷偷改。
3. R5 是否沿用 R4 的规则集 id，取决于规则数据是否变化 —— 需在第 3 步实际比对后决定。
4. 测试者机器上仍跑着旧 U-Boot（`043fd74b5db7`）和 192.168.1.1；
   **修复只有装上新包才会生效**，本次没有接触任何实机。

---

## 9. 本目录内容

```
work/bootbridge-r5-handover-20261022/
  HANDOVER.md                                  <- 本文件
  work/bootbridge-uboot-fixes-20261021/
    verify-admission/                          <- 独立验证者完整交付
      REPORT.md                                <- 结论 + 前后对照表（建议先读这个）
      RESULT.json                              <- 逐行 expected/actual/pass
      harness/ include/ src-snapshots/ tools/ logs/
    rebuild-r5/
      BLOCKER-BUILD-BASE.md                    <- 为什么不能用 tree A/B（证据充分）
      PREP-STATUS.md                           <- 工具/输入 pin 全部核对结果
  verify-admission-RESULT.sha256               <- RESULT.json 的 sha256
```

验证者结论摘要：**HEAD `2bbf85a` 正确，且没有扩大准入边界**；
`8465ced` 的回退是必要的（他独立复现了 20 个跨候选放行）；
84/84 候选单行隔离扫描全清；272/272 provider 都带自己的摘要和本板 machid。

---

## 10. 权威仓库位置

- **WSL 权威**：`/home/yu/Project/RT3000/publish/H3C-NX30-Uboot`，当前 HEAD `2bbf85a`，工作区干净
- **GitHub**：`https://github.com/FlyFish-go/H3C-NX30-Uboot`，`main` 已同步到 `2bbf85a`
- **构建基座**：`/home/yu/Project/RT3000/work/r5-build-src-20261022-v2/source`
- **R5 输出**：`/home/yu/Project/RT3000/evidence/bridge10-remap-20261021/bundles-r5`（仅 RC3000）
- VM 侧 `work/bootbridge-uboot-fixes-20261021/repo` 是同一 git 仓库的另一份检出，非权威
