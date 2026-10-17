# Portable APPSBL MBN packager

This isolated tool converts a stripped little-endian ARM ELF into the normalized
Qualcomm MBN-v3 byte layout used by the four current HW-A/B/C/D candidates. It
then optionally pads that exact MBN byte stream with `0xff` to the 0x140000-byte
APPSBL partition size. The LOAD segment is copied verbatim; the packager rejects
out-of-bounds, BSS-backed, malformed, or oversized inputs rather than truncating
them.

The container contract was taken from the public GPL implementation in
`rt3000_b_metadata_core.c` (`rt3_md_prepare_elf`) and its public header:

- stripped ARM ELF32, entry and LOAD virtual/physical address `0x4a920000`;
- exactly one file-backed LOAD and the known empty GNU_STACK program header;
- four output program headers, LOAD file offset `0x3000`;
- MBN-v3 hash table at `0x1000`, length `0xa8`, SHA-256 of the 0xb4-byte
  metadata header and the LOAD bytes;
- final normalized MBN size `0x3000 + LOAD filesz`; partition image size
  `0x140000`, padded only after the MBN with `0xff`.

The new Python implementation is marked `GPL-2.0-or-later`. It does not link,
invoke, copy, or inspect Qualcomm's confidential/proprietary `elftombn.py` or
`mbn_tools.py`. Exact byte equality against the four canonical build1 candidate
MBNs and padded partitions is verified by `test_reference_match.py` using local
evidence inputs. The test inputs and output candidates are intentionally kept
outside this small source directory.

Example:

```sh
python3 appsbl_packager.py path/to/hw-d.strip.elf out/appsbl.mbn \
  --partition out/appsbl.partition.bin --board D
```

`--board` is a provenance label only. This formatter does not inspect or prove
the embedded DT/profile/board identity, commit consistency, target NAND/ECC,
physical slot, inactive-slot binding, installation readiness, or first-install
safety. A release driver must validate those facts from the board's build
provenance and independently reviewed DT/descriptor before invoking it. Output
is an offline candidate payload and is not an installation authorization.

Run the byte-level regression in this evidence workspace with:

```sh
python3 -m unittest -v test_reference_match.py
```

That command runs the format and safety tests. The four-candidate golden
comparison is opt-in so this source bundle has no dependency on a particular
workstation or evidence path. Point it at the directory containing `input/`
and `candidate-bundle/` to include the byte-for-byte comparison:

```sh
APPSBL_REFERENCE_ROOT=/path/to/payloads python3 -m unittest -v test_reference_match.py
```

The documented sizes and fields follow the frozen GPL metadata-core
implementation included in the accompanying source review tree. The review
manifest records its original file hashes and the sanitized source overlay.
