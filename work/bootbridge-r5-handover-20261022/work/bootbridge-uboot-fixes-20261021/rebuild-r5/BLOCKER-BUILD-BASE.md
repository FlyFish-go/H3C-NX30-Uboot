# RELEASE BLOCKER: the designated WSL build base cannot ship the fix

## Summary
The brief's build source
  /home/yu/Project/RT3000/evidence/nand-oem-repair-20261017/rebuild-src-b0383f96/nx30-oem-bridge
("tree A") does not contain the code path the fix lives in. Copying
bridge_install_identity.c into it (which I did, correctly, and which the lead
also did) produces a provider whose rc_c_runtime_platform()/
policy_admits_c_candidate() is DEAD CODE — never called. The rebuild would
AUDIT PASS and still ship a device that rejects its own payload exactly as R4
did. That is worse than not building.

## Evidence (all measured in WSL, this session)

### A1. The fix is in the runtime entry point; tree A never calls it
- repo HEAD 2bbf85a src/bridge_install.c has a helper
  `check_current_candidate_identity()` that reads proc/device-tree/compatible
  and calls `bii_check_public_candidate_runtime(...)` (line ~1054).
- tree A src/bridge_install.c (sha 63950b52…) has NO such helper and both
  identity call sites use the STRICT entry:
    1671: bii_code identity=bii_check_public_candidate(...)
    2024: if(bii_check_public_candidate(...)!=BII_OK)
- grep of ALL of tree A src/ + tests/ for `bii_check_public_candidate_runtime`
  returns exactly ONE hit: the definition itself at
  src/bridge_install_identity.c:242. Zero callers.
- tree A src/bridge_install_identity.h (sha cb71032e…) has NO declaration of
  the runtime entry (count 0). Repo header (39d4759f…) has it (count 1).
=> In tree A the fixed function is unreachable from the install path.

### A2. Independent confirmation: the R4 provider binary lacks the C2 route
- The fix's whole premise is the byte-exact compatible route
  "qcom,ipq5018-mp03.1-c2\0qcom,ipq5018".
- Of 68 R4 NX30 providers, 17 contain it (the C-class ones) — but the string
  is reached only via the runtime helper. The shipped R4 provider artifact
  (VARIANT-011, 191948 B) contains machid literals 08040101/08040000/08040001
  (1 each) and the pre.1 digest 8ed8b325 (1), and the stale e768be51 (0) —
  consistent with the strict path being what ships.

### A3. Tree A is a DIFFERENT source lineage than the tree that produced R4
R4's own BUILD-REPORT.json pins the sources it compiled:
  bridge_dispatch.c 0f83807468944d2d6dd1bdc939fa7979a15b9827dabc1bbd37c47c30e915a682
  bridge_oem_return_exec.c 16e64bd651da26aba5c1b93a7ce120bac219703d121d5101374374b9e101db51
  bridge_dispatch.h b8d7e7e6c7f6c56e0294379d44d6cf3c73963b4d5078538b7ff057a5842777fd
Tree A hashes for those: dispatch = 52fe806f… (≠), oem_return_exec = 16e64bd6… (match).
The tree whose hashes match R4 is
  /home/yu/Project/RT3000/work/review-followup-20261017/continue-r3/source-final-v2/source/nx30-oem-bridge
("tree B"): dispatch 0f838074… (match), oem_return_exec 16e64bd6… (match),
dispatch.h b8d7e7e6… (match).

### A4. R4 also carries machinery tree A does not have
- R4 BUILD-REPORTs (bundle + all providers) contain a `root_binding_contract`
  record with a generated header sha 960585ad… and contract 85466482….
- That is produced by tools/root_binding_contract.py, which EXISTS in tree B
  and in the repo, and does NOT exist in tree A.
- tree A build.py (98655 B) never mentions root_binding_contract (0 hits);
  tree B build.py (100197 B) has 9.
- tree A src/bridge_install.c has no BI_ROOT_BINDING_CONTRACT_HEADER include
  (0); tree B has 3.
- R4 provider profile headers emit the 2-tuple "root-a-v1/root-b-v1" scheme;
  the repo's current build.py emits a 4-tuple app/root v2 scheme
  (BI_FIRST_INSTALL_PROFILE_COUNT 4u). Neither tree A nor tree B does that —
  only the repo does.
=> Tree A cannot reproduce the R4 artifacts it is supposed to be the successor of.

### A5. Consequence for the file the lead synced
The synced identity.c (4e684621…) is correct in itself: policy_admits
present, e768be51 gone, 4 policy rows. But dropping it into tree A yields
"fixed function, no callers". The three checks the lead specified
(policy_admits present / e768be51 absent / 4 rows) ALL PASS on tree A while
the shipped behavior is unchanged. Those checks are necessary but NOT
sufficient — they do not test reachability.

## What I recommend (needs lead decision — I did not proceed unilaterally)
Option 1 (preferred): build from the REPO tree
  /home/yu/Project/H3C_Magic/work/bootbridge-uboot-fixes-20261021/repo/tools/bootbridge/nx30-oem-bridge
  It is the only tree that contains all of: the runtime entry + its header
  decl + the install.c caller + root_binding_contract.py + the generated
  contract header wiring + the 4-profile app/root v2 scheme. Its
  build_four_sources_remote.py is present (21088 B).
  Caveats to confirm before running: (a) tools/root_binding_guest/guest-modules/
  in the repo holds only modules.json, no .ko files, while tree B holds 4 .ko —
  check whether build.py needs them (grep suggests build.py copies OEM .ko from
  the official image, not from guest-modules, so likely fine);
  (b) repo tests/fixtures has nand-dmesg.SANITIZED.md instead of tree B's
  nand-dmesg.raw — grep shows no build.py reference, likely fine.
  (c) The repo tree is a git checkout the lead owns; I would build from a
  private copy so the checkout is untouched, per the brief.

Option 2: use tree B (the actual R4 lineage) and apply the 19-line identity.c
  fix plus the header decl + the install.c runtime-caller change. That is a
  larger source change than "sync one file" and needs its own review.

Option 3: if the intent is ONLY to prove the admission fix, a narrow
  provider-only rebuild from the repo tree is enough and much cheaper.

I have NOT launched any build. No output root exists, no evidence tree was
modified, nothing pushed or committed.

## A6. DECISIVE PROOF: R4 was built by the REPO-lineage runner, not tree A's
The R4 run's own RUN-SUMMARY.json records:
  "release_set": "H3C-BOOTBRIDGE-NAND-OEM-20261020-R4"
  "root_binding_gate_config": {815 B, GUEST-GATE-CONFIG-FINAL-V2.json, sha 6e14b2ff…}
Those two fields are emitted ONLY by the repo-lineage runner, which has
`--release-set` and a REQUIRED `--root-binding-gate-config` argument
(repo runner sha f59cbfcf6dcb82a7605bedb7eedc97cb90f7ce5ad4da56ec52b7af5a9b49ee70).

Tree A's runner (the one the brief names) has NEITHER:
  grep "release-set"                  -> 0
  grep "root_binding_gate_config"     -> 0
  and it hardcodes release_set "nand-oem-repair-20261017".
Its --help shows no --release-set and no --root-binding-gate-config at all.

So the brief's own required inputs (a NEW R5 release set, and the gate config
it pins) are UNREPRESENTABLE on tree A. The R4 artifact's provenance is the
repo-lineage tree. Tree A is not a downgrade of the right tree; it is a
different, older line that cannot express R5 at all.

## A7. Option 1 is staged and verified (private copy, repo checkout untouched)
Private WSL copy at /tmp/r5sync/opt1-tree/nx30-oem-bridge (extracted from a
tarball of the repo tree, sha256 f5a95361131946f0367fd6f13a8a36251fd0a0a4dd159afffe28ed19d478b8b4):
  install.c runtime callers ................ 4   (repo 2 call sites + helper)
  identity.h runtime decl .................. 1
  identity.c policy_admits_c_candidate ..... 2
  identity.c e768be51 ...................... 0
  identity.c sha256 ........................ 4e68462161f1ccfbded9ecec3ab094c79b8bfbb70e36ab22c1d6ce5edeba964c
  tools/root_binding_contract.py ........... present
  tools/build_four_sources_remote.py ....... present, sha f59cbfcf… (matches repo)
  runner --help ............................ accepts --root-binding-gate-config
                                             and --release-set
This copy is OUTSIDE the repo checkout and outside every evidence tree, so the
brief's "do not touch the repo checkout / do not modify evidence" constraints
both hold.
