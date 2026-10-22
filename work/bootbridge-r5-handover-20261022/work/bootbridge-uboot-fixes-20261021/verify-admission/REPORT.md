# Independent admission-matrix verification (task-2)

**Verdict: the runtime-platform fix at HEAD `2bbf85a` is correct and does not widen
the admission boundary.** It admits exactly the payload the provider already ships,
on exactly one board and one DT route, and refuses every other candidate I could
construct. The revert of `8465ced` was necessary — I independently reproduced the
cross-candidate hole it removed. Two pre-existing behaviours are documented below
that are **not** regressions but do not match a strict "one provider, one payload"
reading; they need an explicit decision, not a code change by me.

| | |
|---|---|
| Repo HEAD verified | `2bbf85a00d665030480feffe20b44e6b0255a1d4` |
| Normative source sha256 | `4e68462161f1ccfbded9ecec3ab094c79b8bfbb70e36ab22c1d6ce5edeba964c` |
| Before-fix source sha256 | `8122fb6f12bf0635c87d86a73af65e5ffb8e26217d4a7f712f6909709128d0a8` (`1bb3ded`) |
| Archive sha256 | `2e0972448bf39b2a37a814b725fe9514e8e7e03705ed12955c82a9cca1805008` (308,700,371 B) |
| Matrix cases executed | 92 per board × 4 boards, plus 30 static, plus 92 for the reverted table = **490 row evaluations per build variant** |
| Candidate sweep | 84/84 archive candidates, each compiled as its own single-row provider |
| Provider binaries audited | 272/272 (68 per model × 4 models) |

All results below are **EXECUTED** on the VM host with gcc 14.2.0. Nothing in this
report is inferred from the lead's numbers.

---

## 1. What the matrix actually models

The brief's premise — "one provider, 21 board-C candidates" — is not how the shipped
artefacts are built. Each R4 provider compiles **exactly one** generated policy row
via `BII_EXTRA_CANDIDATE_POLICIES`. I verified this directly: all 272 providers have
a `bii_candidate_policy.generated.h` with exactly 1 row (0 multi-row, 0 missing).

So the matrix is built **per provider**:

- **own candidate** — must be admitted on the exact route / no-machid ENV
- **the other 20 same-board candidates** — must be refused (`BII_POLICY`)
- **63 other-board candidates** — must be refused

That is the shape that catches the bug the lead reverted. A single 21-row provider
model would have hidden it.

Digests were derived independently of the lead's dump: I streamed the archive, hashed
every `appsbl.partition.bin` myself, and cross-checked against `CANDIDATES.json`, the
embedded `SHA256SUMS` and `MANIFEST.json`. 84 candidates, 21 per board, all unique,
**0 digest mismatches**, and all three pins match the brief.

## 2. Before / after

Normative expectation set is the intended post-fix behaviour, so the before-fix run
shows real deltas rather than a second hand-tuned expectation list.

| Build | Board A | Board B | Board C | Board D |
|---|---|---|---|---|
| **before** `1bb3ded` | 92/92 | 92/92 | **82/92** | 92/92 |
| **after** `2bbf85a` | 92/92 | 92/92 | **92/92** | 92/92 |

The fix is confined to board C. All 10 pre-fix C failures are the no-machid path:

| case | before | after |
|---|---|---|
| `own-exact-route-base-env` (own payload, exact C2 route, stock no-machid ENV) | `env-machid` | `ok` |
| `own-root-bootargs` | `env-machid` | `ok` |
| `own-cmdline-{good,wrong,dup,bad,empty,none,null}-machid` (7 cases) | `env-machid` | correct per case |
| `pre1-ownboard-on-this-provider` | `env-machid` | `ok` |

Note the cmdline cases: before the fix they short-circuited at `env-machid` and the
cmdline gate was never reached. After the fix the cmdline machid assertions are
actually evaluated — `cmdline-machid` for wrong/duplicate/malformed values, `ok` for
absent or correct. That is a second, previously dead guard coming alive, and it
behaves correctly.

Board D is unchanged by design: its rows already carry
`allow_missing_env_machid=1`, so the runtime proof was never on its critical path.
Boards A and B have no runtime route proof at all and are untouched.

## 3. The reverted table was a real hole — reproduced

I rebuilt the `8465ced` 84-row table verbatim from
`git show 8465ced:...bridge_install_identity.c`, combined it with the `2bbf85a`
runtime fix, and ran the same matrix:

| Build | Result |
|---|---|
| `2bbf85a` alone | 92/92 |
| `8465ced` table + `2bbf85a` runtime fix | **72/92 — 20 failures** |

All 20 failures are `other-C-NN`: with the wide table present,
`policy_admits_c_candidate()` scans every board-C row, so a provider for candidate X
admits all 21 board-C candidates. The lead's revert was correct and necessary.

## 4. 84-candidate isolation sweep

For each of the 84 archive candidates I compiled a policy header containing **only
that candidate's row** and ran the full board matrix.

| Board | Candidates | Clean |
|---|---|---|
| A | 21 | 21 |
| B | 21 | 21 |
| C | 21 | 21 |
| D | 21 | 21 |
| **total** | **84** | **84** |

Every candidate is admitted on its own board, and every one of its 20 same-board
siblings plus all 63 other-board candidates is refused.

## 5. Guards that hold (board C, own candidate)

| Attack | Expected | Actual |
|---|---|---|
| exact route `qcom,ipq5018-mp03.1-c2\0qcom,ipq5018`, 36 B | `ok` | `ok` |
| route `mp03.1` (near miss) | `env-machid` | `env-machid` |
| generic `qcom,ipq5018` only | `env-machid` | `env-machid` |
| swapped order | `env-machid` | `env-machid` |
| trailing extra member | `env-machid` | `env-machid` |
| first member without its NUL | `env-machid` | `env-machid` |
| 35 of 36 bytes (truncated) | `env-machid` | `env-machid` |
| empty buffer | `env-machid` | `env-machid` |
| NULL pointer | `env-machid` | `env-machid` |
| ENV machid present but wrong (`08040000`) | `env-machid` | `env-machid` |
| ENV machid duplicated (same / conflicting) | `env-machid` | `env-machid` |
| ENV machid non-hex / >32-bit | `env-machid` | `env-machid` |
| cmdline machid wrong / duplicated / malformed / empty | `cmdline-machid` | `cmdline-machid` |
| wrong productname / empty / duplicated / missing | `product` | `product` |
| root keys in ENV bootargs on C (`filter_env_root_keys=1`) | `ok` | `ok` |
| bad ENV CRC | `crc` | `crc` |
| ENV size 0x7ffff instead of 0x80000 | `size` | `size` |
| all-zero / random / short / empty / uppercase digest | `policy` | `policy` |
| phantom digest `e768be51…` | `policy` | `policy` |
| source sha `RT3000` / `RW3000` / `NX30` / all-zero | not `ok` | not `ok` |
| own digest presented as board A / B / D | not `ok` | not `ok` |

The critical one: **a present-but-wrong machid is still `BII_ENV_MACHID`.** The
missing-machid escape does not cover it. That was the main way this fix could have
gone wrong, and it did not.

### 5a. Adversarial probes against the helper itself

The fix replaces a hardcoded digest with a scan of `policies[]`. Two ways that scan
could be too wide, tested directly:

| probe | expected | actual |
|---|---|---|
| one board-C row whose `machid` is `08040000` (not `08040101`), own digest, exact route, no-machid ENV | `policy` | `policy` |
| two board-C rows: own digest at `08040101` **and** a sibling at `08040000` | own → `ok`, sibling → `policy` | own `ok`, sibling `policy` |

The first is stronger than I expected: a C row with a non-`08040101` machid is
refused at the **policy** stage by `check_public_candidate`'s
`!eq(expected,policy->machid)` gate, before the runtime escape is even consulted.
The helper's `eq("08040101", policies[i].machid)` predicate is therefore
belt-and-braces, not the only thing holding that line. The second confirms the
machid filter is per-row, not per-table.

## 6. Provider binaries (independent re-confirmation)

272/272 providers under `bundles-r4/<MODEL>/provider-builds/VARIANT-*/` carry
**both** their own candidate digest and their board's literal machid.

| Model | Variants | Providers | own digest | machid | both |
|---|---|---|---|---|---|
| RC3000 | 68 | 68 | 68 | 68 | 68 |
| RT3000 | 68 | 68 | 68 | 68 | 68 |
| RW3000 | 68 | 68 | 68 | 68 | 68 |
| NX30 | 68 | 68 | 68 | 68 | 68 |

Boards A/B/C/D × 17 each per model. No bundle tree was modified (read-only audit).

## 7. Two pre-existing behaviours — decision needed, not regressions

**(a) The four frozen pre.1 digests are compiled into every provider.**
So an R4 HW-C provider also admits the pre.1 C candidate `57d13d1d…` on the
no-machid route. This was already true before the fix (pre-fix it was admitted too,
once the ENV carried a machid), so it is not a regression. But if "a provider must
admit only its own payload" is a hard requirement, this violates it, and the fix's
own helper — which scans `policies[]` — cannot distinguish a generated row from a
frozen one. Closing it would need the helper to key on generated rows only.

**(b) Board D admits the RT3000 source with a matching productname.**
`sources[]` registers the RT3000 source for all four classes, and D's rows carry
`allow_missing_env_machid=1`. Pre-existing; asserted explicitly in the matrix so it
is recorded rather than silently passing.

## 8. A build-path risk I could not close (reported, not fixed)

R4 providers were built through `build_bundle.py`'s fresh path
(`candidate_descriptor` set), which sets `candidate_profile_codegen=True` and renders
the BII header — confirmed from `RUN-SUMMARY.json` pins and the per-provider
`payload.descriptor` entries in `BUILD-REPORT.json`.

At HEAD `2bbf85a`, the **other** branch in `build.py`
(`elif args.candidate_archive is not None:`, ~line 1387) still does not call
`render_headers` and does not set `candidate_profile_codegen`. A build reaching that
path would emit a provider with no `BII_EXTRA_CANDIDATE_POLICIES` at all — only the 4
static pre.1 rows — and its own R4 payload would be refused with `BII_POLICY` on
every board.

`tools/build_four_sources_remote.py` always passes all three archive pins, so
`build_bundle.py` takes `fresh = any(pins) = True` and the descriptor path. On that
evidence the neutral-archive path is **not** on the R5 route, so I am **not** calling
this a live defect. I am flagging it because a provider born unable to install its
own payload would present identically to the bug just fixed while having a different
cause. I also observed the lead's uncommitted `build.py` edit adding exactly this
codegen to that branch, and `git status` now shows `build.py` clean — so that edit is
not in the tree. **Worth one explicit check before trusting R5 `AUDIT=PASS`.**

## 9. Honest scope limits

- **EXECUTED**: everything in sections 2–6.
- **NOT_EXECUTED**: a real end-to-end R5 build through the neutral-archive path
  (§8) — I read the control flow and the R4 evidence, I did not run a build.
- **NOT_EXECUTED**: on-device install. This is host-level admission logic only; no
  hardware was touched, and nothing here is a hardware qualification.
- The matrix calls the production TU directly. It does not exercise
  `bridge_install.c`'s surrounding gates (`verify_bridge_identity`, qfprom,
  NAND/profile matching), which are separate contracts.
- 2 harness bugs of my own were found and fixed during bring-up (ENV pairs not
  NUL-terminated; route length 36 vs 37 bytes). Both are recorded here because they
  changed results: the first produced spurious `BII_PRODUCT` everywhere, the second
  made the exact-route case look like a near-miss.

## 10. Reproduction

```
tools/extract_digests_wsl.py     # independent digest derivation from the archive
tools/gen_matrix.py              # 92-case matrix per board
tools/build_matrix.sh            # compile one (snapshot × policy header)
tools/sweep_all_providers.py     # 84-candidate isolation sweep
tools/audit_providers_wsl.py     # 272 provider binary audit
tools/make_result.py             # assemble RESULT.json
```

`RESULT.json` carries per-row expected/actual/pass for every case and every build,
plus sha256 of every source snapshot and log.
