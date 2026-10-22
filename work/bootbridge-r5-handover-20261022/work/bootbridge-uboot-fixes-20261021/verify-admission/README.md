# verify-admission — independent admission-matrix verification (task-2)

Owner: teammate `admission-verifier`. Do not edit repo source from here.

**Verdict: HEAD `2bbf85a` is correct and does not widen the admission boundary.**
See `REPORT.md` for the full before/after table, the 84-candidate isolation sweep,
and two pre-existing behaviours that need a decision rather than a fix.

| file | what it is |
|---|---|
| `REPORT.md` | human-readable verdict, tables, scope limits |
| `RESULT.json` | machine-readable: per-row expected/actual/pass for every build |
| `logs/` | raw run output; every file hashed into RESULT.json |
| `src-snapshots/` | byte-identical snapshots of the before/after source |
| `harness/` | C runner + generated per-board matrices |
| `include/` | policy headers (real R4 generated rows, reverted 84-row table) |
| `tools/` | derivation, matrix generation, sweep, provider audit, assembly |

## Reproduce

```
python3 tools/make_result.py            # rebuild RESULT.json from logs/
tools/build_matrix.sh <src.c> <header|none> <matrix.h> <outdir>
./out/head-prodC/va_runner             # one matrix, JSON on stdout
```

Digests are derived independently from the R4 archive by
`tools/extract_digests_wsl.py` (runs in WSL, streams the 308 MB tar and hashes
each `appsbl.partition.bin` itself; does not import the repo parser).

## Headline numbers

- before `1bb3ded`: board C 82/92 (10 failures, all the no-machid path)
- after `2bbf85a`: board C 92/92; A/B/D 92/92 on both sides
- reverted `8465ced` table + fix: 72/92 — 20 board-C siblings wrongly admitted
- 84-candidate single-row sweep: 84/84 clean
- 272/272 provider binaries carry their own digest and board machid
