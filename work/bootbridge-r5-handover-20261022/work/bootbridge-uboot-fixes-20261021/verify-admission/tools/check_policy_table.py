#!/usr/bin/env python3
"""Cross-check the compiled policies[] table against the archive-derived digests.

Independent of the C build: parses the table textually and compares set equality
per board, plus cross-board collisions (the dangerous kind: a typo that lands on
another board's real digest).
"""
import json, re, sys

def parse_policies(path):
    text = open(path, encoding="utf-8").read()
    rows = []
    # Rows look like: {"C", "<64 hex>", "08040101", NULL, 1, 0},
    pat = re.compile(r'\{\s*"([ABCD])"\s*,\s*"([0-9a-f]{64})"\s*,\s*"([0-9a-fA-F]{8})"\s*,\s*(NULL|"[^"]*")\s*,\s*(\d+)\s*,\s*(\d+)\s*\}')
    for m in pat.finditer(text):
        rows.append({"board": m.group(1), "candidate": m.group(2),
                     "machid": m.group(3).lower(),
                     "env_alias": None if m.group(4) == "NULL" else m.group(4).strip('"'),
                     "filter_env_root_keys": int(m.group(5)),
                     "allow_missing_env_machid": int(m.group(6))})
    return rows

def main():
    src, digests_path = sys.argv[1], sys.argv[2]
    rows = parse_policies(src)
    arch = json.load(open(digests_path))
    arch_by_board = {}
    for r in arch["rows"]:
        arch_by_board.setdefault(r["board"], {})[r["partition_sha256"]] = r
    tab_by_board = {}
    for r in rows:
        tab_by_board.setdefault(r["board"], []).append(r)

    pre1 = {
        "A": "8ed8b32594d0ebbe3d5f23a8de1853b9ed3e7492c391361ffaf6f4cf7b1b4abb",
        "B": "912159169a8b61adb40e572b5366a21da68b65d48fcd47f113e65de44c908589",
        "C": "57d13d1dad2a7b7854799b7cc69bddec638dd61df70f1b75a4fd94650fc96d8e",
        "D": "91e9d881ad1b5c88f942069908bfdd7484984521422ad7f1923738d2cc653362",
    }
    report = {"source": src, "table_rows": len(rows), "per_board": {}, "problems": []}
    for b in "ABCD":
        tab = tab_by_board.get(b, [])
        arch_set = set(arch_by_board.get(b, {}))
        tab_set = {r["candidate"] for r in tab}
        missing = sorted(arch_set - tab_set)          # archive candidate not admitted
        extra = sorted(tab_set - arch_set)            # table digest with no archive row
        dupes = sorted({d for d in tab_set if [r["candidate"] for r in tab].count(d) > 1})
        report["per_board"][b] = {
            "table_rows": len(tab), "archive_rows": len(arch_set),
            "archive_missing_from_table": missing,
            "table_extra_not_in_archive": extra,
            "table_duplicate_rows": dupes,
            "pre1_present": pre1[b] in tab_set,
            "machids": sorted({r["machid"] for r in tab}),
            "flags": sorted({(r["filter_env_root_keys"], r["allow_missing_env_machid"]) for r in tab}),
        }
        if missing: report["problems"].append(f"{b}: archive digest missing from table: {missing}")
        if dupes: report["problems"].append(f"{b}: duplicate table rows: {dupes}")
        if not report["per_board"][b]["pre1_present"]:
            report["problems"].append(f"{b}: pre.1 digest absent")
    # cross-board collisions: same digest admitted on >1 board
    allmap = {}
    for r in rows:
        allmap.setdefault(r["candidate"], set()).add(r["board"])
    cross = {d: sorted(bs) for d, bs in allmap.items() if len(bs) > 1}
    report["cross_board_digests"] = cross
    if cross: report["problems"].append(f"digest admitted on multiple boards: {cross}")
    # phantom digest the pre-fix code compared against
    phantom = "e768be512e985980070c811cea56f44a822bee3a938bb91f821454764e89cd84"
    report["phantom_digest_in_table"] = any(r["candidate"] == phantom for r in rows)
    report["phantom_digest_in_archive"] = any(r["partition_sha256"] == phantom for r in arch["rows"])
    # extra (non-archive) table rows are allowed only for the 4 frozen pre.1 records
    allowed_extra = set(pre1.values())
    unexpected_extra = [r for b in "ABCD" for r in report["per_board"][b]["table_extra_not_in_archive"]
                        if r not in allowed_extra]
    report["unexpected_table_extras"] = unexpected_extra
    if unexpected_extra: report["problems"].append(f"table rows with no archive or pre.1 basis: {unexpected_extra}")
    print(json.dumps(report, indent=1, sort_keys=True))
    return 1 if report["problems"] else 0

if __name__ == "__main__":
    raise SystemExit(main())
