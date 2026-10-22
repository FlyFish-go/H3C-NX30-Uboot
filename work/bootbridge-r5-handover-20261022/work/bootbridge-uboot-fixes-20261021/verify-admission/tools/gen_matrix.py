#!/usr/bin/env python3
"""Generate va_matrix.h case tables for one build variant.

Variants differ only in which compile-time policy header is supplied:
  static   - no BII_EXTRA_CANDIDATE_POLICIES: the four frozen pre.1 records only
  prodC    - the real generated header of one shipped R4 HW-C provider
  prodA    - the real generated header of one shipped R4 HW-A provider
  table84  - the reverted 84-row hardcoded table (commit 8465ced)
"""
import argparse, json, sys

SOURCE_RC = "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44"
SOURCE_RT = "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b"
SOURCE_RW = "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5"
SOURCE_NX = "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922"

PRE1 = {
    "A": "8ed8b32594d0ebbe3d5f23a8de1853b9ed3e7492c391361ffaf6f4cf7b1b4abb",
    "B": "912159169a8b61adb40e572b5366a21da68b65d48fcd47f113e65de44c908589",
    "C": "57d13d1dad2a7b7854799b7cc69bddec638dd61df70f1b75a4fd94650fc96d8e",
    "D": "91e9d881ad1b5c88f942069908bfdd7484984521422ad7f1923738d2cc653362",
}
PHANTOM = "e768be512e985980070c811cea56f44a822bee3a938bb91f821454764e89cd84"

# Synthetic ENV bodies. Each tuple is the ordered list of NUL-separated k=v pairs.
ENVS = {
    "no_machid":       ("productname=RC3000", "bootcmd=bootipq", "ipaddr=192.168.10.10"),
    "no_machid_rt":    ("productname=RT3000", "bootcmd=bootipq"),
    "no_machid_nx":    ("productname=NX30", "bootcmd=bootipq"),
    "no_machid_alias": ("productname=NX30_2i5x", "bootcmd=bootipq"),
    "no_machid_bare":  ("bootcmd=bootipq",),
    "good_machid":     ("productname=RC3000", "machid=08040101", "bootcmd=bootipq"),
    "good_machid_0x":  ("productname=RC3000", "machid=0x08040101", "bootcmd=bootipq"),
    "wrong_machid":    ("productname=RC3000", "machid=08040000", "bootcmd=bootipq"),
    "wrong_machid_A":  ("productname=RC3000", "machid=08040000", "bootcmd=bootipq"),
    "wrong_machid_B":  ("productname=RT3000", "machid=08040101", "bootcmd=bootipq"),
    "wrong_machid_C":  ("productname=RC3000", "machid=08040000", "bootcmd=bootipq"),
    "wrong_machid_D":  ("productname=NX30", "machid=08040101", "bootcmd=bootipq"),
    "root_args_A":     ("productname=RC3000", "bootcmd=bootipq",
                        "bootargs=console=ttyMSM0 root=/dev/old"),
    "root_args_B":     ("productname=RT3000", "bootcmd=bootipq",
                        "bootargs=console=ttyMSM0 root=/dev/old"),
    "root_args_C":     ("productname=RC3000", "bootcmd=bootipq",
                        "bootargs=console=ttyMSM0 root=/dev/old"),
    "root_args_D":     ("productname=NX30", "bootcmd=bootipq",
                        "bootargs=console=ttyMSM0 root=/dev/old"),
    "wrong_machid_0x": ("productname=RC3000", "machid=0x08040001", "bootcmd=bootipq"),
    "dup_machid":      ("productname=RC3000", "machid=08040101", "machid=08040101", "bootcmd=bootipq"),
    "dup_machid_mix":  ("productname=RC3000", "machid=08040101", "machid=08040000", "bootcmd=bootipq"),
    "no_product":      ("machid=08040101", "bootcmd=bootipq"),
    "wrong_product":   ("productname=RC3000X", "bootcmd=bootipq"),
    "empty_product":   ("productname=", "bootcmd=bootipq"),
    "dup_product":     ("productname=RC3000", "productname=RC3000", "bootcmd=bootipq"),
    "with_root_args":  ("productname=RC3000", "bootcmd=bootipq", "bootargs=console=ttyMSM0 root=/dev/old"),
    "clean_bootargs":  ("productname=RC3000", "bootcmd=bootipq", "bootargs=console=ttyMSM0,115200n8 loglevel=3"),
    "machid_upper":    ("productname=RC3000", "machid=08040101", "bootcmd=bootipq"),
    "machid_short":    ("productname=RC3000", "machid=8040101", "bootcmd=bootipq"),
    "machid_bad_hex":  ("productname=RC3000", "machid=0804010g", "bootcmd=bootipq"),
    "machid_overflow": ("productname=RC3000", "machid=108040101", "bootcmd=bootipq"),
    "good_machid_A":   ("productname=RC3000", "machid=08040001", "bootcmd=bootipq"),
    "good_machid_B":   ("productname=RT3000", "machid=08040000", "bootcmd=bootipq"),
    "good_machid_C":   ("productname=RC3000", "machid=08040101", "bootcmd=bootipq"),
    "good_machid_D":   ("productname=NX30", "machid=08040000", "bootcmd=bootipq"),
    "short_machid_A":  ("productname=RC3000", "machid=8040001", "bootcmd=bootipq"),
    "short_machid_C":  ("productname=RC3000", "machid=8040101", "bootcmd=bootipq"),
    "clean_args_A":    ("productname=RC3000", "machid=08040001", "bootcmd=bootipq",
                        "bootargs=console=ttyMSM0,115200n8 loglevel=3"),
    "clean_args_C":    ("productname=RC3000", "machid=08040101", "bootcmd=bootipq",
                        "bootargs=console=ttyMSM0,115200n8 loglevel=3"),
    "clean_args_B":    ("productname=RT3000", "machid=08040000", "bootcmd=bootipq",
                        "bootargs=console=ttyMSM0,115200n8 loglevel=3"),
    "clean_args_D":    ("productname=NX30", "machid=08040000", "bootcmd=bootipq",
                        "bootargs=console=ttyMSM0,115200n8 loglevel=3"),
    "short_machid_B":  ("productname=RT3000", "machid=8040000", "bootcmd=bootipq"),
    "short_machid_D":  ("productname=NX30", "machid=8040000", "bootcmd=bootipq"),
}

CASES = []
def add(cid, category, board, product, source, machid, cand, env, expect,
        compat="exact", env_variant=0, cmdline="console=ttyMSM0,115200n8 loglevel=3"):
    CASES.append(dict(id=cid, category=category, board=board, product=product,
                      source=source, machid=machid, candidate=cand, env=env,
                      expect=expect, compat=compat, env_variant=env_variant,
                      cmdline=cmdline))

def c_escape(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

def render(variant, digests, own_c, own_a):
    rows = digests["rows"]
    by_board = {b: [r["partition_sha256"] for r in rows if r["board"] == b] for b in "ABCD"}
    CASES.clear()

    if variant == "static":
        # No generated row: only the four frozen pre.1 digests exist.
        for b in "ABCD":
            prod = {"A": "RC3000", "B": "RT3000", "C": "RC3000", "D": "NX30"}[b]
            src = {"A": SOURCE_RC, "B": SOURCE_RT, "C": SOURCE_RC, "D": SOURCE_NX}[b]
            mach = {"A": "08040001", "B": "08040000", "C": "08040101", "D": "08040000"}[b]
            env = {"A": "no_machid", "B": "no_machid_rt", "C": "no_machid",
                   "D": "no_machid_nx"}[b]
            # A/B/C pre.1 rows carry allow_missing_env_machid=0, so a no-machid ENV
            # is refused there by design; only D carries the flag.
            add(f"static/pre1-{b}-no-machid", "pre1-baseline-no-machid", b, prod, src, mach,
                PRE1[b], env, "ok" if b in "CD" else "env-machid")
            add(f"static/pre1-{b}-present-machid", "pre1-baseline-present-machid", b, prod, src,
                mach, PRE1[b], f"good_machid_{b}", "ok")
        for i, d in enumerate(by_board["C"]):
            add(f"static/r4C-{i:02d}", "r4-without-generated-row", "C", "RC3000", SOURCE_RC,
                "08040101", d, "no_machid", "policy")
        add("static/r4C-machid-present", "r4-without-generated-row", "C", "RC3000", SOURCE_RC,
            "08040101", by_board["C"][0], "good_machid", "policy")
    else:
        board = (variant[len("prod")] if variant.startswith("prod") else "C")
        own = own_c if board == "C" else own_a
        prod = {"A": "RC3000", "B": "RT3000", "C": "RC3000", "D": "NX30"}[board]
        src = {"A": SOURCE_RC, "B": SOURCE_RT, "C": SOURCE_RC, "D": SOURCE_NX}[board]
        mach = {"A": "08040001", "B": "08040000", "C": "08040101", "D": "08040000"}[board]
        # A/B rows carry allow_missing_env_machid=0, so a board-A provider needs a
        # PRESENT machid ENV to be admitted at all. C carries the flag off too, but
        # the runtime C2 proof substitutes it, which is what we are testing.
        # Only C (runtime C2 proof) and D (allow_missing_env_machid=1) can be
        # admitted with no machid in the ENV. A and B require a present machid.
        base_env = {"A": "good_machid_A", "B": "good_machid_B",
                    "C": "no_machid", "D": "no_machid_nx"}[board]
        # The candidate this provider actually ships.
        add(f"{variant}/own-exact-route-base-env", "own-candidate", board, prod, src, mach,
            own, base_env, "ok")
        add(f"{variant}/own-exact-route-present-machid", "own-candidate", board, prod, src, mach,
            own, f"good_machid_{board}", "ok")
        add(f"{variant}/own-exact-route-0x-machid", "own-candidate", board, prod, src, mach,
            own, "good_machid_0x" if board == "C" else f"good_machid_{board}", "ok")
        add(f"{variant}/own-exact-route-short-machid", "own-candidate", board, prod, src, mach,
            own, f"short_machid_{board}", "ok")
        add(f"{variant}/own-wrong-route-mp031", "own-candidate-wrong-route", board, prod, src, mach,
            own, base_env, "env-machid" if board == "C" else "ok", compat="near_mp031")
        add(f"{variant}/own-wrong-route-generic-only", "own-candidate-wrong-route", board, prod, src,
            mach, own, base_env, "env-machid" if board == "C" else "ok", compat="only_generic")
        add(f"{variant}/own-wrong-route-swapped", "own-candidate-wrong-route", board, prod, src, mach,
            own, base_env, "env-machid" if board == "C" else "ok", compat="swapped")
        add(f"{variant}/own-wrong-route-trailing", "own-candidate-wrong-route", board, prod, src, mach,
            own, base_env, "env-machid" if board == "C" else "ok", compat="trailing")
        add(f"{variant}/own-wrong-route-c2-no-nul", "own-candidate-wrong-route", board, prod, src, mach,
            own, base_env, "env-machid" if board == "C" else "ok", compat="c2_no_nul")
        add(f"{variant}/own-wrong-route-truncated", "own-candidate-wrong-route", board, prod, src, mach,
            own, base_env, "env-machid" if board == "C" else "ok", compat="trunc_one")
        add(f"{variant}/own-wrong-route-empty", "own-candidate-wrong-route", board, prod, src, mach,
            own, base_env, "env-machid" if board == "C" else "ok", compat="empty")
        add(f"{variant}/own-wrong-route-null", "own-candidate-wrong-route", board, prod, src, mach,
            own, base_env, "env-machid" if board == "C" else "ok", compat="null")
        add(f"{variant}/own-wrong-env-machid", "own-env-machid", board, prod, src, mach,
            own, f"wrong_machid_{board}", "env-machid")
        add(f"{variant}/own-dup-env-machid", "own-env-machid", board, prod, src, mach,
            own, "dup_machid", "env-machid")
        add(f"{variant}/own-dup-env-machid-mixed", "own-env-machid", board, prod, src, mach,
            own, "dup_machid_mix", "env-machid")
        add(f"{variant}/own-machid-bad-hex", "own-env-machid", board, prod, src, mach,
            own, "machid_bad_hex", "env-machid")
        add(f"{variant}/own-machid-overflow", "own-env-machid", board, prod, src, mach,
            own, "machid_overflow", "env-machid")
        # "no_product" carries machid=08040101: correct for C, wrong for A/B/D, and
        # the machid gate runs before the product gate.
        add(f"{variant}/own-no-product", "own-product", board, prod, src, mach, own,
            "no_product", "product" if board == "C" else "env-machid")
        add(f"{variant}/own-wrong-product", "own-product", board, prod, src, mach, own,
            "wrong_product", "product")
        add(f"{variant}/own-empty-product", "own-product", board, prod, src, mach, own,
            "empty_product", "product")
        add(f"{variant}/own-dup-product", "own-product", board, prod, src, mach, own,
            "dup_product", "product")
        add(f"{variant}/own-bare-env", "own-product", board, prod, src, mach, own,
            "no_machid_bare", "product")
        # C/D compile filter_env_root_keys=1 (release filters at boot); A/B reject.
        add(f"{variant}/own-root-bootargs", "own-bootargs", board, prod, src, mach, own,
            f"root_args_{board}", "ok" if board in "CD" else "bootargs")
        add(f"{variant}/own-clean-bootargs", "own-bootargs", board, prod, src, mach, own,
            f"clean_args_{board}", "ok")
        # The missing-machid escape must not cover a machid supplied by cmdline.
        add(f"{variant}/own-cmdline-good-machid", "own-cmdline-machid", board, prod, src, mach,
            own, base_env, "ok", cmdline=f"console=ttyMSM0 machid={mach}")
        wrong = "08040000" if mach != "08040000" else "08040101"
        add(f"{variant}/own-cmdline-wrong-machid", "own-cmdline-machid", board, prod, src, mach,
            own, base_env, "cmdline-machid", cmdline=f"console=ttyMSM0 machid={wrong}")
        add(f"{variant}/own-cmdline-dup-machid", "own-cmdline-machid", board, prod, src, mach,
            own, base_env, "cmdline-machid",
            cmdline=f"console=ttyMSM0 machid={mach} machid={mach}")
        add(f"{variant}/own-cmdline-bad-machid", "own-cmdline-machid", board, prod, src, mach,
            own, base_env, "cmdline-machid", cmdline="console=ttyMSM0 machid=zzzz")
        add(f"{variant}/own-cmdline-empty-machid", "own-cmdline-machid", board, prod, src, mach,
            own, base_env, "cmdline-machid", cmdline="console=ttyMSM0 machid=")
        add(f"{variant}/own-cmdline-no-machid", "own-cmdline-machid", board, prod, src, mach,
            own, base_env, "ok", cmdline="console=ttyMSM0,115200n8 root=/dev/ram")
        add(f"{variant}/own-cmdline-null", "own-cmdline-machid", board, prod, src, mach,
            own, base_env, "ok", cmdline=None)
        add(f"{variant}/own-bad-crc", "own-env-crc", board, prod, src, mach, own,
            "no_machid", "crc", env_variant=1)
        add(f"{variant}/own-wrong-env-size", "own-env-size", board, prod, src, mach, own,
            "no_machid", "size", env_variant=2)
        # THE cross-candidate isolation test: one generated row must not admit others.
        for i, d in enumerate(by_board[board]):
            if d == own:
                continue
            add(f"{variant}/other-{board}-{i:02d}", "cross-candidate-isolation", board, prod, src,
                mach, d, base_env, "policy")
        for i, d in enumerate(by_board["C"]):
            add(f"{variant}/crossBoard-A-cand-{i:02d}", "cross-board-isolation", "A", "RC3000",
                SOURCE_RC, "08040001", d, "good_machid_A", "not-ok")
        # RT3000 source is registered for all four classes, so a matching
        # productname on that source is legitimately accepted (pre-existing).
        # sources[] registers the RT3000 source for all four classes, so with a
        # matching productname and NO machid only D's allow_missing record admits
        # it. On A/B/C the missing machid is refused. Pre-existing baseline
        # behaviour, unaffected by the runtime-platform fix (asserted explicitly).
        add(f"{variant}/src-rt3000", "source-substitution", board, "RT3000", SOURCE_RT, mach,
            own, "no_machid_rt", "ok" if board == "D" else "not-ok")
        add(f"{variant}/src-rw3000", "source-substitution", board, "RW3000", SOURCE_RW, mach,
            own, "no_machid", "not-ok")
        # NX30 source is registered D-only in sources[]; on D it is legitimate.
        add(f"{variant}/src-nx30", "source-substitution", board, "NX30", SOURCE_NX, mach,
            own, "no_machid_nx", "not-ok" if board != "D" else "ok")
        add(f"{variant}/src-unknown", "source-substitution", board, "RC3000", "0" * 64, mach,
            own, "no_machid", "not-ok")
        # The provider's own digest presented as a DIFFERENT board must be refused.
        add(f"{variant}/board-A-with-own", "board-substitution", "A", "RC3000", SOURCE_RC,
            "08040001", own, "good_machid_A", "ok" if board == "A" else "not-ok")
        add(f"{variant}/board-B-with-own", "board-substitution", "B", "RT3000", SOURCE_RT,
            "08040000", own, "good_machid_B", "ok" if board == "B" else "not-ok")
        add(f"{variant}/board-D-with-own", "board-substitution", "D", "NX30", SOURCE_NX,
            "08040000", own, "good_machid_D", "ok" if board == "D" else "not-ok")
        add(f"{variant}/alias-env-nx", "product-alias", board, prod, src, mach, own,
            "no_machid_alias", "ok" if board == "D" else "not-ok")
        # The four frozen pre.1 records are statically compiled into EVERY
        # provider, so the pre.1 digest for this board is policy-known and
        # admitted. This is pre-existing baseline acceptance, not introduced by
        # the runtime-platform fix; recorded explicitly rather than asserted away.
        add(f"{variant}/pre1-ownboard-on-this-provider", "static-pre1-baseline", board, prod, src,
            mach, PRE1[board], base_env, "ok")
        add(f"{variant}/phantom-digest", "phantom-digest", board, prod, src, mach,
            PHANTOM, base_env, "policy")
        for ob in "ABCD":
            if ob == board:
                continue
            add(f"{variant}/pre1-{ob}-digest-on-{board}-provider", "foreign-pre1", board, prod, src,
                mach, PRE1[ob], base_env, "policy")
        add(f"{variant}/allzero-digest", "unknown-digest", board, prod, src, mach,
            "0" * 64, base_env, "policy")
        add(f"{variant}/random-digest", "unknown-digest", board, prod, src, mach,
            "0123456789abcdef" * 4, base_env, "policy")
        add(f"{variant}/short-digest", "unknown-digest", board, prod, src, mach,
            "abc123", base_env, "policy")
        add(f"{variant}/empty-digest", "unknown-digest", board, prod, src, mach,
            "", base_env, "policy")
        add(f"{variant}/uppercase-digest", "unknown-digest", board, prod, src, mach,
            own.upper(), base_env, "policy")

    # Header text.
    out = ["/* generated by tools/gen_matrix.py -- do not edit */",
           "#ifndef VA_MATRIX_H", "#define VA_MATRIX_H", ""]
    used_envs = sorted({c["env"] for c in CASES})
    for name in used_envs:
        vals = ENVS[name]
        out.append(f"static const char *const env_{name}[] = {{"
                   + ", ".join(c_escape(v) for v in vals) + ", 0};")
    out.append("")
    out.append("typedef struct {")
    out.append("    const char *id, *category, *board, *product, *source, *machid, *candidate;")
    out.append("    const char *const *env;")
    out.append("    int env_variant, compat;")
    out.append("    const char *cmdline, *expect;")
    out.append("} va_case;")
    out.append("enum { VA_COMPAT_EXACT = 0, VA_COMPAT_NEAR_MP031, VA_COMPAT_ONLY_FIRST, "
               "VA_COMPAT_ONLY_GENERIC,")
    out.append("       VA_COMPAT_SWAPPED, VA_COMPAT_TRAILING, VA_COMPAT_C2_NO_NUL, VA_COMPAT_EMPTY,")
    out.append("       VA_COMPAT_TRUNC_ONE, VA_COMPAT_NULL };")
    out.append("static const va_case va_cases[] = {")
    for c in CASES:
        cmd = "NULL" if c["cmdline"] is None else c_escape(c["cmdline"])
        out.append('    {%s, %s, %s, %s, %s, %s, %s, env_%s, %d, VA_COMPAT_%s, %s, %s},' % (
            c_escape(c["id"]), c_escape(c["category"]), c_escape(c["board"]),
            c_escape(c["product"]), c_escape(c["source"]), c_escape(c["machid"]),
            c_escape(c["candidate"]), c["env"], c["env_variant"], c["compat"].upper(),
            cmd, c_escape(c["expect"])))
    out.append("};")
    out.append(f"#define VA_CASE_COUNT {len(CASES)}u")
    out.append("#endif")
    return "\n".join(out) + "\n"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", required=True)
    ap.add_argument("--digests", required=True)
    ap.add_argument("--own-c", default=None)
    ap.add_argument("--own-a", default=None)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    digests = json.load(open(a.digests))
    text = render(a.variant, digests, a.own_c, a.own_a)
    open(a.out, "w").write(text)
    print(f"{a.out}: {len(CASES)} cases")

if __name__ == "__main__":
    raise SystemExit(main())
