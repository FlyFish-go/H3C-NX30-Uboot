#!/usr/bin/env python3
"""Build the BootBridge web edition from the audited desktop release inputs.

Inputs are the same release artefacts the desktop build uses:
  * bb_offline_catalog_generated.h  (plan programs, pins)   --header
  * BootBridge.rules.dat            (XOR/LITERAL bytes)     --dat
  * desktop/guide-models.json       (display guide)         --guide
  * design/desktop-offline-ui/strings.json + web/strings-web.json
  * web/nx30-download/{ps51,sh}.tpl (NX30 download commands)

Outputs into --out a complete static site: a copy of web/site/ (--site) plus
the generated assets/catalog.js, assets/strings.js, assets/commands.js,
data/<bundle>-<sha12>.js and SHA256SUMS (optionally also a ZIP via --zip).
Only the standard library is used.  With --inputs DIR, every bundle is additionally rebuilt by an
independent Python reference of bb_export_plan_bin() against the official
firmware found in DIR and must reproduce the pinned output SHA-256.
"""
import argparse, base64, hashlib, json, os, re, shutil, sys, zipfile

COPY, LITERAL, XOR = 1, 2, 3
KIND = {"COPY": COPY, "LITERAL": LITERAL, "XOR": XOR}
MAX_OPS = 262144
WEB_VERSION = "0.1.0"


def die(msg):
    sys.exit("build_site: " + msg)


def c_string(lit):
    """Decode the generator's octal-escaped C string literal."""
    out, i = bytearray(), 0
    while i < len(lit):
        c = lit[i]
        if c == "\\":
            m = re.match(r"[0-7]{1,3}", lit[i + 1:])
            if not m:
                die("unsupported C escape in %r" % lit)
            out.append(int(m.group(0), 8)); i += 1 + len(m.group(0))
        else:
            out += c.encode("utf-8"); i += 1
    return out.decode("utf-8")


def hexbytes(body):
    vals = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body)]
    if len(vals) != 32:
        die("digest is not 32 bytes")
    return bytes(vals).hex()


def parse_header(text):
    m = re.search(r"#define BB_OFFLINE_DATA_SIZE (\d+)ULL", text) or die("no DATA_SIZE")
    data_size = int(m.group(1))
    m = re.search(r"bb_offline_expected_sha256\[32\] = \{([^}]*)\}", text) or die("no DAT pin")
    data_sha = hexbytes(m.group(1))
    m = re.search(r'#define BB_RELEASE_SET_ID "((?:[^"\\]|\\.)*)"', text) or die("no release set")
    release = c_string(m.group(1))
    m = re.search(r"#define BB_ENGINE_REVISION (\d+)U", text) or die("no engine revision")
    engine = int(m.group(1))
    ops = {}
    for idx, body in re.findall(r"static bb_plan_op bb_offline_ops_(\d+)\[\] = \{\n(.*?)\n?\};", text, re.S):
        lst = []
        for line in body.strip().splitlines():
            om = re.fullmatch(r"\{BB_PLAN_(COPY|LITERAL|XOR),(\d+)ULL,(\d+)ULL,(NULL|bb_offline_data\+(\d+)ULL)\},", line.strip())
            if not om:
                die("unexpected op line: %r" % line[:80])
            lst.append((KIND[om.group(1)], int(om.group(2)), int(om.group(3)),
                        None if om.group(4) == "NULL" else int(om.group(5))))
        ops[idx] = lst
    plans = {}
    for idx, pid, insz, insha, outsz, outsha, ref, count in re.findall(
            r'static bb_plan_recipe bb_offline_plan_(\d+) = \{"((?:[^"\\]|\\.)*)",(\d+)ULL,\{([^}]*)\},(\d+)ULL,\{([^}]*)\},bb_offline_ops_(\d+),(\d+)U\};', text):
        if ref != idx or ref not in ops or len(ops[ref]) != int(count):
            die("plan %s does not match its operation table" % idx)
        plans[idx] = dict(id=c_string(pid), inSize=int(insz), inSha=hexbytes(insha),
                          outSize=int(outsz), outSha=hexbytes(outsha), ops=ops[ref])
    bundles = []
    for bid, model, disp, ver, rel, rev, pref in re.findall(
            r'\{"((?:[^"\\]|\\.)*)",BB_MODEL_(\w+),"((?:[^"\\]|\\.)*)","((?:[^"\\]|\\.)*)",(BB_RELEASE_SET_ID|"(?:[^"\\]|\\.)*"),(\d+)U,&bb_offline_plan_(\d+),bb_offline_providers_\d+,\d+U\},', text):
        p = plans.get(pref) or die("bundle refers to missing plan")
        if c_string(bid) != p["id"]:
            die("bundle/plan id mismatch")
        if rel != "BB_RELEASE_SET_ID":
            die("bundle uses a foreign release set")
        bundles.append(dict(p, model=model, display=c_string(disp), version=c_string(ver), minEngine=int(rev)))
    if not bundles or len(bundles) != len(plans):
        die("no bundles or unreferenced plans")
    return dict(dataSize=data_size, dataSha=data_sha, release=release, engine=engine, bundles=bundles)


def valid_plan(b):
    """Mirror of valid_plan() in desktop/src/bb_export.c."""
    if not (0 < b["inSize"] <= (2**64 - 1) // 8 and 0 < b["outSize"] <= (2**64 - 1) // 8):
        return False
    if int(b["inSha"], 16) == 0 or int(b["outSha"], 16) == 0:
        return False
    if not b["ops"] or len(b["ops"]) > MAX_OPS:
        return False
    total = 0
    for kind, off, size, ptr in b["ops"]:
        if not size or size > b["outSize"] - total:
            return False
        if kind in (COPY, XOR):
            if off > b["inSize"] or size > b["inSize"] - off:
                return False
            if kind == COPY and ptr is not None:
                return False
        elif kind == LITERAL:
            if off:
                return False
        else:
            return False
        if kind != COPY and ptr is None:
            return False
        total += size
    return total == b["outSize"]


def slice_bundle(b, dat):
    start = cur = None
    for kind, off, size, ptr in b["ops"]:
        if ptr is None:
            continue
        if cur is None:
            start = cur = ptr
        if ptr != cur:
            die("%s: data is not sequential in operation order" % b["id"])
        cur += size
    if start is None or cur > len(dat):
        die("%s: data range outside the sidecar" % b["id"])
    return start, cur


def apply_plan(b, src, data):
    """Independent reference of emit_plan(); data is the bundle's own slice."""
    out, d = bytearray(), 0
    for kind, off, size, _ in b["ops"]:
        if kind == LITERAL:
            out += data[d:d + size]; d += size
        else:
            seg = src[off:off + size]
            if kind == XOR:
                seg = bytes(x ^ y for x, y in zip(seg, data[d:d + size])) if size < 4096 else \
                    (int.from_bytes(seg, "big") ^ int.from_bytes(data[d:d + size], "big")).to_bytes(size, "big")
                d += size
            out += seg
    if d != len(data):
        die("%s: data not fully consumed" % b["id"])
    return bytes(out)


def js_literal(name, obj):
    body = json.dumps(obj, ensure_ascii=False, separators=(",", ":"))
    return ("/* Generated by web/tools/build_site.py; do not edit. */\n"
            "(function(g){var v=%s;if(typeof module==='object'&&module.exports)module.exports=v;else g.%s=v;})"
            "(typeof self!=='undefined'?self:this);\n" % (body, name))


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def merge(base, over):
    out = dict(base)
    for k, v in over.items():
        out[k] = merge(out[k], v) if isinstance(v, dict) and isinstance(out.get(k), dict) else v
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--header", required=True)
    ap.add_argument("--dat", required=True)
    ap.add_argument("--guide", required=True)
    ap.add_argument("--strings", required=True)
    ap.add_argument("--strings-web", required=True)
    ap.add_argument("--commands", required=True, help="directory with ps51.tpl and sh.tpl")
    ap.add_argument("--site", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "site"),
                    help="static sources (index.html, assets/) copied into --out")
    ap.add_argument("--out", required=True)
    ap.add_argument("--zip", help="also write an offline ZIP of the site to this path")
    ap.add_argument("--inputs", help="directory of official firmware for reference verification")
    ap.add_argument("--repo-url", default="")
    ap.add_argument("--build-label", default="")
    a = ap.parse_args()

    if os.path.exists(a.out) and os.listdir(a.out):
        if not os.path.isfile(os.path.join(a.out, "SHA256SUMS")):
            die("--out exists, is not empty and is not a previous build")
        shutil.rmtree(a.out)
    shutil.copytree(a.site, a.out)
    for gen in ("assets/catalog.js", "assets/strings.js", "assets/commands.js", "SHA256SUMS"):
        if os.path.exists(os.path.join(a.out, gen)):
            die("--site must not contain generated file " + gen)

    cat = parse_header(open(a.header, encoding="ascii").read())
    dat = open(a.dat, "rb").read()
    if len(dat) != cat["dataSize"] or hashlib.sha256(dat).hexdigest() != cat["dataSha"]:
        die("BootBridge.rules.dat does not match the generated header pin")
    guide = json.load(open(a.guide, encoding="utf-8"))
    gmap = {g["id"]: g for g in guide["models"]}

    covered = 0
    bundles_js = []
    for b in cat["bundles"]:
        if not valid_plan(b):
            die("%s: plan fails the bb_export validity rules" % b["id"])
        g = gmap.get(b["model"]) or die("%s: no guide entry for %s" % (b["id"], b["model"]))
        if g["bytes"] != b["inSize"] or g["sha256"] != b["inSha"] or g["version"] != b["version"]:
            die("%s: guide entry disagrees with the plan pin" % b["id"])
        s, e = slice_bundle(b, dat)
        covered += e - s
        data = dat[s:e]
        if sum(sz for k, _, sz, _ in b["ops"] if k != COPY) != len(data):
            die("%s: data size mismatch" % b["id"])
        b64 = base64.b64encode(data).decode("ascii")
        dsha = hashlib.sha256(data).hexdigest()
        fname = "data/%s-%s.js" % (b["id"], dsha[:12])
        write(os.path.join(a.out, fname), 'BBData.add("%s","%s");\n' % (b["id"], b64))
        bundles_js.append(dict(
            id=b["id"], model=b["model"], display=b["display"], version=b["version"],
            inSize=b["inSize"], inSha=b["inSha"], outSize=b["outSize"], outSha=b["outSha"],
            ops=[[k, o, n] for k, o, n, _ in b["ops"]],
            data=dict(file=fname, size=len(data), sha=dsha, b64=len(b64))))
    if covered != len(dat):
        die("sidecar bytes are not exactly covered by the bundles")

    if a.inputs:
        pool = {}
        for root, _, files in os.walk(a.inputs):
            for fn in files:
                p = os.path.join(root, fn)
                if os.path.isfile(p) and os.path.getsize(p) in {b["inSize"] for b in cat["bundles"]}:
                    pool.setdefault(hashlib.sha256(open(p, "rb").read()).hexdigest(), p)
        for b, bj in zip(cat["bundles"], bundles_js):
            src_path = pool.get(b["inSha"])
            if not src_path:
                print("verify %-24s SKIP (official input not found)" % b["id"]); continue
            s, e = slice_bundle(b, dat)
            out = apply_plan(b, open(src_path, "rb").read(), dat[s:e])
            ok = len(out) == b["outSize"] and hashlib.sha256(out).hexdigest() == b["outSha"]
            print("verify %-24s %s" % (b["id"], "OK" if ok else "FAIL"))
            if not ok:
                die("reference rebuild of %s does not match the pinned output" % b["id"])

    guide_js = [dict(id=g["id"], primary=g["primary"], alias=g.get("alias", ""), products=g.get("products", []),
                     version=g["version"],
                     bytes=g["bytes"], sha=g["sha256"], url=g.get("url", "")) for g in guide["models"]]
    meta = dict(schema="bootbridge-web-catalog-v1", web=WEB_VERSION, release=cat["release"],
                engine=cat["engine"], datSha=cat["dataSha"], repo=a.repo_url, label=a.build_label)
    write(os.path.join(a.out, "assets/catalog.js"),
          js_literal("BB_CATALOG", dict(meta=meta, guide=guide_js, bundles=bundles_js)))

    s = json.load(open(a.strings, encoding="utf-8"))
    w = json.load(open(a.strings_web, encoding="utf-8"))
    strings = {lang: merge(s[lang], w.get(lang, {})) for lang in ("zh", "en")}
    write(os.path.join(a.out, "assets/strings.js"), js_literal("BB_STRINGS", strings))

    cmds = {}
    for key, fn in (("ps51", "ps51.tpl"), ("sh", "sh.tpl")):
        t = open(os.path.join(a.commands, fn), encoding="utf-8").read().rstrip("\n")
        if t.count("__SN__") != 1:
            die("%s must contain exactly one __SN__" % fn)
        cmds[key] = t
    write(os.path.join(a.out, "assets/commands.js"), js_literal("BB_COMMANDS", cmds))

    # Cache-busting token for the static assets referenced by index.html.
    idx = os.path.join(a.out, "index.html")
    html = open(idx, encoding="utf-8").read()
    h = hashlib.sha256()
    for rel in sorted(re.findall(r'(?:src|href)="(assets/[^"?]+)\?v=__BUILD__"', html)):
        h.update(open(os.path.join(a.out, rel), "rb").read())
    html = html.replace("__BUILD__", h.hexdigest()[:12]).replace("__WEB_VERSION__", WEB_VERSION)
    write(idx, html)

    sums = []
    for root, _, files in os.walk(a.out):
        for fn in sorted(files):
            p = os.path.join(root, fn)
            rel = os.path.relpath(p, a.out).replace(os.sep, "/")
            if rel != "SHA256SUMS":
                sums.append("%s  %s" % (hashlib.sha256(open(p, "rb").read()).hexdigest(), rel))
    write(os.path.join(a.out, "SHA256SUMS"), "\n".join(sorted(sums, key=lambda x: x[66:])) + "\n")
    if a.zip:
        top = "BootBridge-Web-%s" % WEB_VERSION
        with zipfile.ZipFile(a.zip, "w", zipfile.ZIP_DEFLATED) as z:
            for root, _, files in os.walk(a.out):
                for fn in sorted(files):
                    p = os.path.join(root, fn)
                    z.write(p, top + "/" + os.path.relpath(p, a.out).replace(os.sep, "/"))
    print("built %d bundles, release %s, web %s -> %s" % (len(bundles_js), cat["release"], WEB_VERSION, a.out))


if __name__ == "__main__":
    main()
