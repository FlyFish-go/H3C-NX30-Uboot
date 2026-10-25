#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the BootBridge web page.

    build_site.py --builds DIR --pack rules.bbpack --out SITE [--inputs OFFICIAL_DIR]
                  [--rule-set BB2-R1] [--repo-url URL] [--build-label TEXT] [--zip FILE]

--builds holds one build_bridge.py output directory per official firmware
(plan.json, plan.bin, layout.json). With --inputs, every bundle is rebuilt in
Python from the official file, the pack is injected, and the result is
checked against the image built directly by build/inject_pack.py.
"""
import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "build"))
import inject_pack  # noqa: E402

WEB_VERSION = "0.2.0"
COPY, LITERAL, XOR = 1, 2, 3


def die(msg):
    sys.exit("build_site: " + msg)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb" if isinstance(data, bytes) else "w", **({} if isinstance(data, bytes) else {"encoding": "utf-8"})) as f:
        f.write(data)


def js_literal(name, obj):
    return "window.%s = %s;\n" % (name, json.dumps(obj, ensure_ascii=False, separators=(",", ":")))


def merge(base, over):
    out = dict(base)
    for k, v in over.items():
        out[k] = merge(out[k], v) if isinstance(v, dict) and isinstance(out.get(k), dict) else v
    return out


def apply_plan(plan, src, data):
    out, d = bytearray(), 0
    for kind, off, size in plan["ops"]:
        if kind == COPY:
            out += src[off:off + size]
        elif kind == XOR:
            out += bytes(a ^ b for a, b in zip(src[off:off + size], data[d:d + size]))
            d += size
        else:
            out += data[d:d + size]
            d += size
    return bytes(out)


def data_script(ident, raw):
    b64 = base64.b64encode(raw).decode("ascii")
    return b64, 'BBData.add("%s","%s");\n' % (ident, b64)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--builds", required=True)
    ap.add_argument("--pack", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--inputs")
    ap.add_argument("--rule-set", default="")
    ap.add_argument("--repo-url", default="")
    ap.add_argument("--build-label", default="")
    ap.add_argument("--zip")
    a = ap.parse_args()

    if os.path.exists(a.out) and os.listdir(a.out):
        if not os.path.isfile(os.path.join(a.out, "SHA256SUMS")):
            die("--out exists, is not empty and is not a previous build")
        shutil.rmtree(a.out)
    shutil.copytree(os.path.join(HERE, "site"), a.out)

    guide = json.load(open(os.path.join(HERE, "guide-models.json"), encoding="utf-8"))
    by_sha = {g["sha256"]: g for g in guide["models"]}
    pack = open(a.pack, "rb").read()
    pack_sha = hashlib.sha256(pack).hexdigest()
    if pack[:4] != b"BBPK":
        die("--pack is not a rule pack")
    rule_set = a.rule_set
    m = re.search(rb"^set (\S+)$", pack[64:64 + int.from_bytes(pack[8:12], "little")], re.M)
    if m:
        rule_set = rule_set or m.group(1).decode()

    bundles = []
    for name in sorted(os.listdir(a.builds)):
        d = os.path.join(a.builds, name)
        if not os.path.isfile(os.path.join(d, "plan.json")):
            continue
        plan = json.load(open(os.path.join(d, "plan.json")))
        layout = json.load(open(os.path.join(d, "layout.json")))
        data = open(os.path.join(d, "plan.bin"), "rb").read()
        g = by_sha.get(plan["inSha"]) or die("%s: official firmware not in guide-models.json" % name)
        if g["bytes"] != plan["inSize"] or layout["base"]["sha256"] != plan["outSha"]:
            die("%s: guide/plan/layout disagree" % name)
        if len(data) != plan["data"]["size"] or hashlib.sha256(data).hexdigest() != plan["data"]["sha"]:
            die("%s: plan data pin" % name)
        if len(pack) > layout["pack"]["capacity"]:
            die("%s: pack does not fit the reserved area" % name)
        b64, script = data_script(g["id"], data)
        fname = "data/%s-%s.js" % (g["id"], plan["data"]["sha"][:12])
        write(os.path.join(a.out, fname), script)
        bundles.append(dict(id=g["id"], model=g["id"], display=g["primary"], version=g["version"],
                            inSize=plan["inSize"], inSha=plan["inSha"], outSize=plan["outSize"],
                            outSha=plan["outSha"], ops=plan["ops"],
                            data=dict(file=fname, size=len(data), sha=plan["data"]["sha"], b64=len(b64)),
                            layout=layout))
        if a.inputs:
            src_path = os.path.join(a.inputs, g["id"] + "-original.bin")
            if not os.path.isfile(src_path):
                print("verify %-8s SKIP (official input not found)" % g["id"])
                continue
            base = apply_plan(plan, open(src_path, "rb").read(), data)
            if hashlib.sha256(base).hexdigest() != plan["outSha"]:
                die("%s: plan rebuild mismatch" % g["id"])
            final = inject_pack.inject(base, layout, pack)
            print("verify %-8s OK base %s final %s" % (g["id"], plan["outSha"][:12],
                                                       hashlib.sha256(final).hexdigest()[:12]))
    if not bundles:
        die("no builds found")
    missing = [g["id"] for g in guide["models"] if g["id"] not in {b["id"] for b in bundles}]
    if missing:
        die("no build for " + ", ".join(missing))

    b64, script = data_script("pack", pack)
    pack_file = "data/pack-%s.js" % pack_sha[:12]
    write(os.path.join(a.out, pack_file), script)
    guide_js = [dict(id=g["id"], primary=g["primary"], alias=g.get("alias", ""), products=g.get("products", []),
                     version=g["version"], bytes=g["bytes"], sha=g["sha256"], url=g.get("url", ""))
                for g in guide["models"]]
    meta = dict(schema="bootbridge-web-catalog-v2", web=WEB_VERSION, release=rule_set, engine=2,
                repo=a.repo_url, label=a.build_label)
    catalog = dict(meta=meta, guide=guide_js, bundles=bundles,
                   pack=dict(id="pack", data=dict(file=pack_file, size=len(pack), sha=pack_sha, b64=len(b64))))
    write(os.path.join(a.out, "assets/catalog.js"), js_literal("BB_CATALOG", catalog))

    s = json.load(open(os.path.join(HERE, "strings.json"), encoding="utf-8"))
    w = json.load(open(os.path.join(HERE, "strings-web.json"), encoding="utf-8"))
    write(os.path.join(a.out, "assets/strings.js"),
          js_literal("BB_STRINGS", {lang: merge(s[lang], w.get(lang, {})) for lang in ("zh", "en")}))
    cmds = {}
    for key, fn in (("ps51", "ps51.tpl"), ("sh", "sh.tpl")):
        t = open(os.path.join(HERE, "nx30-download", fn), encoding="utf-8").read().rstrip("\n")
        if t.count("__SN__") != 1:
            die("%s must contain exactly one __SN__" % fn)
        cmds[key] = t
    write(os.path.join(a.out, "assets/commands.js"), js_literal("BB_COMMANDS", cmds))

    idx = os.path.join(a.out, "index.html")
    html = open(idx, encoding="utf-8").read()
    h = hashlib.sha256()
    for rel in sorted(re.findall(r'(?:src|href)="(assets/[^"?]+)\?v=__BUILD__"', html)):
        h.update(open(os.path.join(a.out, rel), "rb").read())
    write(idx, html.replace("__BUILD__", h.hexdigest()[:12]).replace("__WEB_VERSION__", WEB_VERSION))

    sums = []
    for root, _, files in os.walk(a.out):
        for fn in sorted(files):
            rel = os.path.relpath(os.path.join(root, fn), a.out).replace(os.sep, "/")
            if rel != "SHA256SUMS":
                sums.append("%s  %s" % (hashlib.sha256(open(os.path.join(root, fn), "rb").read()).hexdigest(), rel))
    write(os.path.join(a.out, "SHA256SUMS"), "\n".join(sorted(sums, key=lambda x: x[66:])) + "\n")
    if a.zip:
        top = "BootBridge-Web-%s" % WEB_VERSION
        with zipfile.ZipFile(a.zip, "w", zipfile.ZIP_DEFLATED) as z:
            for root, _, files in os.walk(a.out):
                for fn in sorted(files):
                    p = os.path.join(root, fn)
                    z.write(p, top + "/" + os.path.relpath(p, a.out).replace(os.sep, "/"))
    print("built %d bundles, rule set %s, pack %s, web %s -> %s" % (len(bundles), rule_set, pack_sha[:12],
                                                                    WEB_VERSION, a.out))


if __name__ == "__main__":
    main()
