#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read the one reviewed public ABCD candidate archive and emit descriptors.

This is an offline artifact parser. It does not identify a device, qualify an
installation, select a physical slot, or infer BSS from stripped output.
"""

import argparse
import atexit
import copy
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import sys
import tarfile
import tempfile
import threading
from typing import Any


ARCHIVE_SHA256 = "bcca16ce8060b6f09094c10d0af40f9e48343d62da8e08ffe6cadbca8725c732"
ARCHIVE_BYTES = 20670650
ROOT = "abcd-uboot-public-review-20261016"
MANIFEST_SHA256 = "7e904ed5fdd4f98fb2c1c819e0844f60194988aeb4fdba0309539bfcb8376139"
CANDIDATES_SHA256 = "f75edb9ced00124717b55430709e35e3620295aa16be378d605cacf2a0ded29a"
SOURCE_REVISION = "5f486f5f9852c26d7d16eb5980a06cccea2a5217"
EMBEDDED_GIT_REVISION = "5f486f5f9852"
SOURCE_DATE_EPOCH = "1792126813"
BOARDS = ("A", "B", "C", "D")
PARTITION_BYTES = 0x140000
LOAD_ADDRESS = 0x4A920000
LOAD_OFFSET = 0x3000
HASH_OFFSET = 0x1000
HASH_BYTES = 0xA8
SDRAM_LIMIT = 0x4AA00000
ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
PROGRAM_HEADER = struct.Struct("<IIIIIIII")
PT_LOAD = 1
PT_GNU_STACK = 0x6474E551


class CandidateError(ValueError):
    """The archive or candidate does not match the reviewed public artifact."""


# Per-process acceleration only. Every API call still rereads and hashes the
# caller's archive before using these entries; cached files are also rehashed
# against the validated manifest descriptor on every member read.
_V2_PARSE_CACHE: dict[tuple[str, str, str], dict[str, Any]] = {}
_V2_MEMBER_CACHE: dict[tuple[str, str], tuple[dict[str, Any], Path]] = {}
_V2_CACHE_ROOT: Path | None = None
_V2_CACHE_LOCK = threading.RLock()


def _v2_cache_root() -> Path:
    global _V2_CACHE_ROOT
    with _V2_CACHE_LOCK:
        if _V2_CACHE_ROOT is None:
            root = Path(tempfile.mkdtemp(prefix="bootbridge-candidate-cache-"))
            root.chmod(0o700)
            _V2_CACHE_ROOT = root
        return _V2_CACHE_ROOT


def _cleanup_v2_cache() -> None:
    global _V2_CACHE_ROOT
    with _V2_CACHE_LOCK:
        if _V2_CACHE_ROOT is not None:
            shutil.rmtree(_V2_CACHE_ROOT, ignore_errors=True)
            _V2_CACHE_ROOT = None


atexit.register(_cleanup_v2_cache)


def _regular_file_sha256(path: Path) -> str:
    """Hash an ordinary file without loading a large archive into memory."""
    try:
        fd = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
        with os.fdopen(fd, "rb") as stream:
            st = os.fstat(stream.fileno())
            if not stat.S_ISREG(st.st_mode):
                raise CandidateError("candidate archive must be an ordinary file")
            digest = hashlib.sha256()
            while True:
                block = stream.read(1024 * 1024)
                if not block:
                    break
                digest.update(block)
            return digest.hexdigest()
    except OSError as exc:
        raise CandidateError("cannot hash candidate archive") from exc


def _parse_candidate_sums(raw_sums: bytes, label: str) -> dict[str, str]:
    """Parse canonical SHA256SUMS rows while allowing punctuation in paths."""
    try:
        text = raw_sums.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise CandidateError(f"{label} checksum file is not UTF-8") from exc
    result = {}
    for line in text.splitlines():
        if len(line) < 67 or line[64:66] != "  " or not re.fullmatch(r"[0-9a-f]{64}", line[:64]):
            raise CandidateError(f"{label} checksum row is not canonical")
        digest, rel = line[:64], line[66:]
        if (not _safe_relpath(rel) or rel in result or
                any(ord(char) < 0x20 or ord(char) == 0x7f for char in rel)):
            raise CandidateError(f"{label} checksum path is unsafe or duplicated")
        result[rel] = digest
    if list(result) != sorted(result):
        raise CandidateError(f"{label} checksum inventory is not sorted")
    canonical = "".join(digest + "  " + rel + "\n" for rel, digest in result.items()).encode("utf-8")
    if raw_sums != canonical:
        raise CandidateError(f"{label} checksum file is not canonical")
    return result


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _json(data: bytes, label: str) -> Any:
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise CandidateError(f"duplicate JSON key in {label}: {key}")
            result[key] = value
        return result
    try:
        return json.loads(data, object_pairs_hook=unique)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise CandidateError(f"invalid JSON in {label}") from exc


def _safe_relpath(path: str) -> bool:
    if not isinstance(path, str) or not path or "\\" in path:
        return False
    if any(part in ("", ".", "..") for part in path.split("/")):
        return False
    p = PurePosixPath(path)
    return not p.is_absolute()


def _manifest_index(raw_manifest: bytes) -> dict[str, dict[str, Any]]:
    if sha256(raw_manifest) != MANIFEST_SHA256:
        raise CandidateError("public MANIFEST.json digest mismatch")
    doc = _json(raw_manifest, "MANIFEST.json")
    if (not isinstance(doc, dict) or set(doc) != {"schema", "entries"} or
            type(doc.get("schema")) is not int or doc.get("schema") != 1):
        raise CandidateError("unsupported public MANIFEST.json schema")
    entries = doc.get("entries")
    if not isinstance(entries, list) or len(entries) != 12139:
        raise CandidateError("public manifest entry count mismatch")
    result = {}
    for item in entries:
        if not isinstance(item, dict) or set(item) != {"path", "bytes", "sha256"}:
            raise CandidateError("malformed public manifest entry")
        path, size, digest = item["path"], item["bytes"], item["sha256"]
        if (not _safe_relpath(path) or path in result or
                type(size) is not int or size < 0 or
                not isinstance(digest, str) or len(digest) != 64 or
                any(ch not in "0123456789abcdef" for ch in digest)):
            raise CandidateError("unsafe or invalid public manifest entry")
        result[path] = item
    return result


def parse_mbn(mbn: bytes, allow_fresh: bool = False) -> dict[str, Any]:
    """Validate the release's normalized ELF32 ARM / MBN-v3 byte contract."""
    if not isinstance(mbn, bytes) or len(mbn) < 0x3000 + 1:
        raise CandidateError("MBN is shorter than normalized header/hash/load envelope")
    values = ELF_HEADER.unpack_from(mbn)
    (ident, e_type, machine, version, entry, phoff, shoff, flags,
     ehsize, phentsize, phnum, shentsize, shnum, shstrndx) = values
    allowed_header_pairs = ({(3, 0x05000202)} if not allow_fresh else
                            {(2, 0x05000200), (3, 0x05000202)})
    if (ident[:7] != b"\x7fELF\x01\x01\x01" or (e_type, flags) not in allowed_header_pairs or machine != 40 or
            version != 1 or entry != LOAD_ADDRESS or phoff != ELF_HEADER.size or
            shoff != 0 or ehsize != ELF_HEADER.size or
            phentsize != PROGRAM_HEADER.size or phnum != 4 or
            shentsize != 40 or shnum != 0 or shstrndx != 0):
        raise CandidateError("MBN ELF32 little-endian ARM header differs from reviewed profile")
    ph_end = phoff + phnum * phentsize
    if ph_end > len(mbn):
        raise CandidateError("MBN program-header table exceeds file")
    ph = [PROGRAM_HEADER.unpack_from(mbn, phoff + i * phentsize) for i in range(phnum)]
    first, hash_seg, load, stack = ph
    if first != (0, 0, 0, 0, 0xB4, 0, 0x07000000, 0):
        raise CandidateError("MBN Qualcomm metadata program-header differs")
    p_type, hash_off, hash_vaddr, hash_paddr, hash_filesz, hash_memsz, hash_flags, hash_align = hash_seg
    if (p_type != 0 or hash_off != HASH_OFFSET or hash_vaddr != hash_paddr or
            hash_filesz != HASH_BYTES or hash_memsz != 0x1000 or
            hash_flags != 0x02200000 or hash_align != 0x1000):
        raise CandidateError("MBN hash-table segment metadata differs")
    p_type, load_off, load_vaddr, load_paddr, load_bytes, load_memsz, load_flags, load_align = load
    if (p_type != PT_LOAD or load_off != LOAD_OFFSET or
            load_vaddr != LOAD_ADDRESS or load_paddr != LOAD_ADDRESS or
            not load_bytes or load_memsz != load_bytes or load_flags != 7 or load_align != 0x1000):
        raise CandidateError("MBN LOAD segment fields differ from the reviewed contract")
    if (load_off < ph_end or load_off > len(mbn) or load_bytes > len(mbn) - load_off or
            load_vaddr + load_bytes > SDRAM_LIMIT or
            load_vaddr + load_bytes > 0xFFFFFFFF or load_off + load_bytes != len(mbn) or
            load_bytes > PARTITION_BYTES - LOAD_OFFSET or entry < load_vaddr or
            entry >= load_vaddr + load_bytes):
        raise CandidateError("MBN LOAD segment bounds or exact file extent are invalid")
    if stack != (PT_GNU_STACK, 0x2000, 0, 0, 0, 0, 7, 0x10):
        raise CandidateError("MBN GNU_STACK header differs from the reviewed empty record")
    hash_address = (LOAD_ADDRESS + load_bytes + 0xFFF) & ~0xFFF
    words = struct.unpack_from("<10I", mbn, HASH_OFFSET)
    expected_words = (21, 3, 0, hash_address + 0x1028, 128, 128,
                      hash_address + 0x10A8, 0, hash_address + 0x10A8, 0)
    if words != expected_words:
        raise CandidateError("Qualcomm MBN-v3 metadata words differ from normalized layout")
    load_data = mbn[load_off:load_off + load_bytes]
    if mbn[HASH_OFFSET + 40:HASH_OFFSET + 72] != hashlib.sha256(mbn[:0xB4]).digest():
        raise CandidateError("MBN header hash mismatch")
    if mbn[HASH_OFFSET + 104:HASH_OFFSET + 136] != hashlib.sha256(load_data).digest():
        raise CandidateError("MBN LOAD hash mismatch")
    if any(mbn[ph_end:HASH_OFFSET]) or any(mbn[HASH_OFFSET + 72:HASH_OFFSET + 104]):
        raise CandidateError("MBN normalized padding/unused hash bytes are not zero")
    return {
        "format": "Qualcomm MBN-v3 normalized ELF32 little-endian ARM",
        "elf_type": "ET_EXEC" if e_type == 2 else "ET_DYN",
        "machine": "EM_ARM",
        "entry": entry,
        "mbn_metadata": {"image_id": 21, "version": 3,
                         "signature_bytes": 0, "certificate_bytes": 0},
        "load": {"file_offset": load_off, "virtual_address": load_vaddr,
                 "physical_address": load_paddr, "bytes": load_bytes,
                 "file_end": load_off + load_bytes,
                 "virtual_end": load_vaddr + load_bytes,
                 "entry_offset": entry - load_vaddr,
                 "memory_bytes_in_stripped_artifact": load_memsz,
                 "flags": load_flags, "alignment": load_align,
                 "sha256": sha256(load_data)},
        "hash_table": {"file_offset": HASH_OFFSET, "bytes": HASH_BYTES,
                       "address": hash_address},
        "mbn_bytes": len(mbn),
        "mbn_sha256": sha256(mbn),
        "bss": {"bytes": None, "end_vaddr": None,
                "status": "unknown; not inferred from stripped ELF/MBN"},
    }


def validate_fresh_elf_mbn_pair(elf: bytes, mbn: bytes) -> dict[str, int]:
    """Require the normalized MBN header identity to match its exact ELF row."""
    if not isinstance(elf, bytes) or len(elf) < ELF_HEADER.size:
        raise CandidateError("fresh unstripped ELF is shorter than its ELF header")
    elf_fields = ELF_HEADER.unpack_from(elf)
    mbn_fields = ELF_HEADER.unpack_from(mbn)
    elf_ident, elf_type, elf_machine, elf_version, _, _, _, elf_flags = elf_fields[:8]
    _, mbn_type, mbn_machine, mbn_version, _, _, _, mbn_flags = mbn_fields[:8]
    allowed = {(2, 0x05000200), (3, 0x05000202)}
    if (elf_ident[:7] != b"\x7fELF\x01\x01\x01" or elf_machine != 40 or elf_version != 1 or
            (elf_type, elf_flags) not in allowed or
            (mbn_type, mbn_flags) not in allowed or
            (elf_type, elf_flags) != (mbn_type, mbn_flags) or
            mbn_machine != elf_machine or mbn_version != elf_version):
        raise CandidateError("fresh MBN ELF type/flags do not match its exact unstripped ELF")
    return {"elf_type": elf_type, "elf_flags": elf_flags}


def validate_candidate_material(board: str, candidate: dict[str, Any],
                                mbn: bytes | None, partition: bytes | None) -> dict[str, Any]:
    """Validate one board's exact public candidate material against its row."""
    if board not in BOARDS or not isinstance(candidate, dict) or candidate.get("board") != board:
        raise CandidateError("board label does not match candidate descriptor")
    files = candidate.get("files")
    if not isinstance(files, dict) or "appsbl.mbn" not in files or "appsbl.partition.bin" not in files:
        raise CandidateError("candidate descriptor lacks MBN/partition file identities")
    if mbn is None or partition is None:
        raise CandidateError("candidate MBN or partition member is missing")
    mbn_doc, part_doc = files["appsbl.mbn"], files["appsbl.partition.bin"]
    for name, raw, desc in (("appsbl.mbn", mbn, mbn_doc), ("appsbl.partition.bin", partition, part_doc)):
        if not isinstance(desc, dict) or set(desc) != {"path", "bytes", "sha256"}:
            raise CandidateError(f"{board} {name} descriptor has unexpected fields")
        expected_path = f"payloads/HW-{board}/{name}"
        if desc.get("path") != expected_path:
            raise CandidateError(f"{board} {name} is bound to another board/path")
        if type(desc.get("bytes")) is not int or len(raw) != desc["bytes"] or sha256(raw) != desc.get("sha256"):
            raise CandidateError(f"{board} {name} length or SHA-256 mismatch")
    if len(partition) != PARTITION_BYTES or len(mbn) > PARTITION_BYTES:
        raise CandidateError(f"{board} candidate partition length is not 0x140000")
    if partition[:len(mbn)] != mbn or partition[len(mbn):] != b"\xff" * (PARTITION_BYTES - len(mbn)):
        raise CandidateError(f"{board} partition MBN prefix or 0xff padding mismatch")
    parsed = parse_mbn(mbn)
    if parsed["mbn_sha256"] != mbn_doc["sha256"]:
        raise CandidateError(f"{board} parsed MBN identity mismatch")
    return {
        "board": board,
        "candidate_id": candidate["candidate_id"],
        "source_revision": candidate["source_revision"],
        "profile": candidate["profile"],
        "source_date_epoch": candidate["source_date_epoch"],
        "files": {"appsbl.mbn": dict(mbn_doc), "appsbl.partition.bin": dict(part_doc)},
        "container": parsed,
        "hardware_validation": "NOT_EXECUTED",
        "installation_qualification": "NOT_GRANTED_BY_CANDIDATE_PARSER",
    }


def read_public_candidates(archive_path: Path, expected_archive_sha256: str) -> dict[str, Any]:
    """Read only the pinned public archive; expected digest must be caller supplied."""
    if expected_archive_sha256 != ARCHIVE_SHA256:
        raise CandidateError("expected archive digest must be the reviewed ABCD public archive SHA-256")
    archive_path = Path(archive_path)
    try:
        info = archive_path.lstat()
    except OSError as exc:
        raise CandidateError("cannot inspect public archive path") from exc
    if not stat.S_ISREG(info.st_mode) or archive_path.is_symlink():
        raise CandidateError("archive input must be an ordinary, non-symlink file")
    archive = archive_path.read_bytes()
    if len(archive) != ARCHIVE_BYTES or sha256(archive) != expected_archive_sha256:
        raise CandidateError("public archive length or outer SHA-256 mismatch")

    prefix = ROOT + "/"
    try:
        with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as tf:
            members = tf.getmembers()
            files = {}
            all_paths = set()
            for member in members:
                if not member.name.startswith(prefix):
                    raise CandidateError("archive member is outside the reviewed root")
                rel = member.name[len(prefix):]
                if not _safe_relpath(rel) or rel in all_paths:
                    raise CandidateError("archive has unsafe or duplicate member path")
                all_paths.add(rel)
                if not (member.isfile() or member.isdir()):
                    raise CandidateError("archive contains a non-regular/non-directory member")
                if member.isfile():
                    extracted = tf.extractfile(member)
                    if extracted is None:
                        raise CandidateError("cannot read archive member")
                    files[rel] = extracted.read()
    except (tarfile.TarError, OSError) as exc:
        raise CandidateError("invalid public tar archive") from exc

    for required in ("MANIFEST.json", "CANDIDATES.json", "SHA256SUMS", "source/SHA256SUMS"):
        if required not in files:
            raise CandidateError(f"required public archive member missing: {required}")
    manifest = _manifest_index(files["MANIFEST.json"])
    if sha256(files["CANDIDATES.json"]) != CANDIDATES_SHA256:
        raise CandidateError("public CANDIDATES.json digest mismatch")
    candidates = _json(files["CANDIDATES.json"], "CANDIDATES.json")
    if (not isinstance(candidates, dict) or set(candidates) != {"schema", "boards", "claim"} or
            type(candidates.get("schema")) is not int or candidates.get("schema") != 1 or
            not isinstance(candidates.get("boards"), dict) or set(candidates["boards"]) != set(BOARDS) or
            candidates.get("claim") != "offline candidates only; no installation authorization or hardware qualification"):
        raise CandidateError("public CANDIDATES.json top-level identity mismatch")
    # The manifest covers every regular member except these two checksum files
    # and itself. The outer archive digest pins their exact bytes.
    unlisted = set(files) - set(manifest) - {"MANIFEST.json", "SHA256SUMS", "source/SHA256SUMS"}
    missing = set(manifest) - set(files)
    if unlisted or missing:
        raise CandidateError("archive member inventory differs from public MANIFEST.json")
    for path in ("CANDIDATES.json",):
        entry = manifest.get(path)
        if entry is None or entry["bytes"] != len(files[path]) or entry["sha256"] != sha256(files[path]):
            raise CandidateError("CANDIDATES.json differs from MANIFEST.json")

    descriptors = {}
    for board in BOARDS:
        row = candidates["boards"][board]
        expected_keys = {"board", "candidate_id", "canonical_comparison", "embedded_git_revision",
                         "files", "hardware_validation", "installation_status", "profile",
                         "source_date_epoch", "source_revision"}
        if not isinstance(row, dict) or set(row) != expected_keys:
            raise CandidateError(f"{board} candidate metadata has unexpected schema")
        if (row.get("board") != board or
                row.get("candidate_id") != f"{SOURCE_REVISION}:HW-{board}:neutral-path-rebuild:offline" or
                row.get("source_revision") != SOURCE_REVISION or
                row.get("embedded_git_revision") != EMBEDDED_GIT_REVISION or
                row.get("profile") != "release" or row.get("source_date_epoch") != SOURCE_DATE_EPOCH or
                row.get("canonical_comparison") != "differs from frozen canonical candidate; no qualification inheritance" or
                row.get("hardware_validation") != "NOT_EXECUTED" or
                row.get("installation_status") != "offline candidate; NOT_INSTALL_READY"):
            raise CandidateError(f"{board} candidate identity/status differs from reviewed release")
        board_files = row.get("files")
        if not isinstance(board_files, dict):
            raise CandidateError(f"{board} candidate file index is malformed")
        expected_file_names = {"appsbl.mbn", "appsbl.partition.bin", "build.config", "dtb_combined.bin",
                               "provenance.json"}
        dtb_name = next((name for name in board_files if name.endswith(".packed.dtb")), None)
        if dtb_name:
            expected_file_names.add(dtb_name)
        if set(board_files) != expected_file_names:
            raise CandidateError(f"{board} candidate file index schema differs")
        material = {}
        for name, desc in board_files.items():
            expected_path = f"payloads/HW-{board}/{name}"
            if not isinstance(desc, dict) or set(desc) != {"bytes", "path", "sha256"} or desc.get("path") != expected_path:
                raise CandidateError(f"{board} file path metadata mismatch: {name}")
            entry = manifest.get(expected_path)
            data = files.get(expected_path)
            if (entry is None or data is None or desc != entry or len(data) != desc["bytes"] or
                    sha256(data) != desc["sha256"]):
                raise CandidateError(f"{board} file does not match candidate and public manifests: {name}")
            material[name] = data
        descriptors[board] = validate_candidate_material(
            board, row, material.get("appsbl.mbn"), material.get("appsbl.partition.bin"))
    return {
        "schema": 1,
        "archive_sha256": expected_archive_sha256,
        "archive_bytes": len(archive),
        "manifest_bytes": len(files["MANIFEST.json"]),
        "candidates_bytes": len(files["CANDIDATES.json"]),
        "candidate_set": "ABCD public neutral-path release candidates",
        "installation_qualification": "NOT_GRANTED_BY_CANDIDATE_PARSER",
        "boards": descriptors,
    }


def read_candidate_archive_v2(archive_path: Path, expected_archive_sha256: str,
                              expected_manifest_sha256: str,
                              expected_candidates_sha256: str) -> dict[str, Any]:
    """Read a fresh variant archive only with all three caller-supplied pins.

    This parser deliberately does not inherit the frozen ABCD archive pins or
    installation qualifications. Variant source identity is a snapshot digest,
    never an asserted Git revision.
    """
    pins = (expected_archive_sha256, expected_manifest_sha256, expected_candidates_sha256)
    if any(not isinstance(x, str) or len(x) != 64 or any(c not in "0123456789abcdef" for c in x)
           for x in pins):
        raise CandidateError("fresh candidate archive requires explicit archive/manifest/descriptors SHA-256 pins")
    path = Path(archive_path)
    try:
        st = path.lstat()
    except OSError as exc:
        raise CandidateError("cannot inspect fresh candidate archive") from exc
    if not stat.S_ISREG(st.st_mode) or path.is_symlink():
        raise CandidateError("candidate archive must be an ordinary non-symlink file")
    cache_key = (expected_archive_sha256, expected_manifest_sha256, expected_candidates_sha256)
    with _V2_CACHE_LOCK:
        cached = _V2_PARSE_CACHE.get(cache_key)
    if cached is not None:
        if _regular_file_sha256(path) != expected_archive_sha256:
            raise CandidateError("fresh candidate archive SHA-256 mismatch")
        # Its outer digest cryptographically binds both embedded descriptor files.
        return copy.deepcopy(cached)
    archive = path.read_bytes()
    if sha256(archive) != expected_archive_sha256:
        raise CandidateError("fresh candidate archive SHA-256 mismatch")
    root = "h3c-uboot-candidates-v2/"
    try:
        with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as tf:
            files, names = {}, set()
            for member in tf.getmembers():
                if not member.name.startswith(root):
                    raise CandidateError("fresh archive member is outside exact v2 root")
                rel = member.name[len(root):]
                if not _safe_relpath(rel) or rel in names:
                    raise CandidateError("fresh archive contains unsafe or duplicate path")
                names.add(rel)
                if member.isdir():
                    continue
                if not member.isfile():
                    raise CandidateError("fresh archive contains non-regular member")
                stream = tf.extractfile(member)
                if stream is None:
                    raise CandidateError("cannot read fresh archive member")
                files[rel] = stream.read()
    except (tarfile.TarError, OSError) as exc:
        raise CandidateError("invalid fresh candidate tar archive") from exc
    required = {"MANIFEST.json", "CANDIDATES.json", "SHA256SUMS", "source/SHA256SUMS"}
    if not required.issubset(files):
        raise CandidateError("fresh candidate archive is missing required metadata")
    if sha256(files["MANIFEST.json"]) != expected_manifest_sha256:
        raise CandidateError("fresh candidate manifest SHA-256 mismatch")
    if sha256(files["CANDIDATES.json"]) != expected_candidates_sha256:
        raise CandidateError("fresh candidate descriptors SHA-256 mismatch")
    manifest = _json(files["MANIFEST.json"], "fresh MANIFEST.json")
    if (not isinstance(manifest, dict) or set(manifest) != {"schema", "entries"} or
            type(manifest["schema"]) is not int or manifest["schema"] != 1 or
            not isinstance(manifest["entries"], list)):
        raise CandidateError("fresh candidate manifest schema mismatch")
    index = {}
    for item in manifest["entries"]:
        if (not isinstance(item, dict) or set(item) != {"path", "bytes", "sha256"} or
                not _safe_relpath(item.get("path")) or item["path"] in index or
                type(item["bytes"]) is not int or item["bytes"] < 0 or
                not isinstance(item["sha256"], str) or len(item["sha256"]) != 64):
            raise CandidateError("fresh candidate manifest entry malformed")
        index[item["path"]] = item
    excluded = {"MANIFEST.json", "SHA256SUMS", "source/SHA256SUMS"}
    if set(files) - excluded != set(index):
        raise CandidateError("fresh candidate manifest inventory mismatch")
    for rel, desc in index.items():
        raw = files.get(rel)
        if raw is None or len(raw) != desc["bytes"] or sha256(raw) != desc["sha256"]:
            raise CandidateError("fresh candidate manifest member digest mismatch")

    # Bind the source snapshot to every packaged source byte. Both checksum
    # files are canonical sorted inventories, and neither may omit or add a
    # member. The snapshot identity hashes compact canonical JSON rows.
    source_members = {name[len("source/"):]: raw for name, raw in files.items()
                      if name.startswith("source/") and name != "source/SHA256SUMS"}
    if not source_members:
        raise CandidateError("fresh archive contains no packaged source files")

    archive_sums = _parse_candidate_sums(files["SHA256SUMS"], "archive")
    if set(archive_sums) != (set(files) - {"SHA256SUMS"}):
        raise CandidateError("archive SHA256SUMS omits or adds a member")
    if any(archive_sums[name] != sha256(raw) for name, raw in files.items()
           if name != "SHA256SUMS"):
        raise CandidateError("archive SHA256SUMS digest mismatch")
    source_sums = _parse_candidate_sums(files["source/SHA256SUMS"], "source")
    if set(source_sums) != set(source_members):
        raise CandidateError("source SHA256SUMS omits or adds a packaged source file")
    source_rows = []
    for rel, digest in source_sums.items():
        raw = source_members.get(rel)
        if raw is None or sha256(raw) != digest:
            raise CandidateError("fresh source checksum does not match packaged source bytes")
        source_rows.append({"path": rel, "sha256": digest})
    snapshot_binding = sha256(json.dumps(source_rows, sort_keys=True,
        separators=(",", ":"), ensure_ascii=True).encode("ascii"))
    doc = _json(files["CANDIDATES.json"], "fresh CANDIDATES.json")
    top = {"schema", "claim", "source_snapshot_sha256", "source_date_epoch", "candidates"}
    if (not isinstance(doc, dict) or set(doc) != top or type(doc.get("schema")) is not int or
            doc["schema"] != 2 or doc["claim"] !=
            "offline candidates only; installation authorization and hardware qualification are separate" or
            not isinstance(doc["source_snapshot_sha256"], str) or
            not re.fullmatch(r"[0-9a-f]{64}", doc["source_snapshot_sha256"]) or
            doc["source_snapshot_sha256"] != snapshot_binding or
            not isinstance(doc["source_date_epoch"], str) or not doc["source_date_epoch"].isdigit() or
            not isinstance(doc["candidates"], list) or not doc["candidates"]):
        raise CandidateError("fresh CANDIDATES.json top-level identity mismatch")
    seen = set()
    descriptors = []
    for row in doc["candidates"]:
        keys = {"board", "candidate_id", "nand_profile_id", "load_ecc_bits", "storage_ecc_bits",
                "source_snapshot_sha256", "source_date_epoch", "profile", "hardware_validation",
                "installation_status", "files"}
        if not isinstance(row, dict) or set(row) != keys:
            raise CandidateError("fresh candidate row schema mismatch")
        board, profile = row["board"], row["nand_profile_id"]
        load_ecc, storage_ecc = row["load_ecc_bits"], row["storage_ecc_bits"]
        if (board not in BOARDS or not isinstance(profile, str) or
                not re.fullmatch(r"[A-Za-z0-9._-]{1,95}", profile) or
                load_ecc not in (4, 8) or storage_ecc not in (4, 8) or
                load_ecc != storage_ecc or
                row["source_snapshot_sha256"] != doc["source_snapshot_sha256"] or
                row["source_date_epoch"] != doc["source_date_epoch"] or
                row["profile"] != "release" or row["hardware_validation"] != "NOT_EXECUTED" or
                row["installation_status"] != "offline candidate; NOT_INSTALL_READY"):
            raise CandidateError("fresh candidate variant identity/status mismatch")
        expected_id = (doc["source_snapshot_sha256"][:40] + ":HW-" + board + ":" + profile +
                       ":load" + str(load_ecc) + "-store" + str(storage_ecc) + ":offline")
        if row["candidate_id"] != expected_id or expected_id in seen:
            raise CandidateError("fresh candidate ID is not exact or duplicated")
        seen.add(expected_id)
        files_doc = row["files"]
        if not isinstance(files_doc, dict) or set(files_doc) != {
                "appsbl.elf", "appsbl.mbn", "appsbl.partition.bin"}:
            raise CandidateError("fresh candidate must bind only its exact ELF, MBN, and padded partition")
        variant_root = f"payloads/HW-{board}/{profile}/load{load_ecc}-store{storage_ecc}"
        material = {}
        for name, desc in files_doc.items():
            if (not isinstance(desc, dict) or set(desc) != {"path", "bytes", "sha256"} or
                    desc["path"] != f"{variant_root}/{name}" or desc["path"] not in files):
                raise CandidateError("fresh candidate file path or descriptor invalid")
            raw = files[desc["path"]]
            if (len(raw) != desc["bytes"] or sha256(raw) != desc["sha256"] or
                    index.get(desc["path"]) != desc):
                raise CandidateError("fresh candidate file differs from manifest")
            material[name] = raw
        mbn, partition = material.get("appsbl.mbn"), material.get("appsbl.partition.bin")
        if (mbn is None or partition is None or len(partition) != PARTITION_BYTES or len(mbn) > PARTITION_BYTES or
                partition[:len(mbn)] != mbn or partition[len(mbn):] != b"\xff" * (PARTITION_BYTES-len(mbn))):
            raise CandidateError("fresh candidate partition must exactly pad its MBN with 0xff")
        elf = material.get("appsbl.elf")
        elf_pair = validate_fresh_elf_mbn_pair(elf, mbn)
        parsed = parse_mbn(mbn, allow_fresh=True)
        if parsed["elf_type"] != ("ET_EXEC" if elf_pair["elf_type"] == 2 else "ET_DYN"):
            raise CandidateError("fresh parsed MBN type differs from exact ELF header")
        parsed["elf_flags"] = elf_pair["elf_flags"]
        descriptors.append({"board": board, "candidate_id": expected_id,
            "nand_profile_id": profile, "load_ecc_bits": load_ecc,
            "storage_ecc_bits": storage_ecc, "source_snapshot_sha256": doc["source_snapshot_sha256"],
            "source_date_epoch": doc["source_date_epoch"], "profile": row["profile"],
            "hardware_validation": "NOT_EXECUTED", "installation_status": row["installation_status"],
            "files": {k: dict(v) for k, v in files_doc.items()}, "container": parsed,
            "archive_sha256": expected_archive_sha256,
            "manifest_sha256": expected_manifest_sha256,
            "candidates_sha256": expected_candidates_sha256})
    bss_name = "VARIANT-BSS-METADATA.json"
    if bss_name not in files:
        raise CandidateError("fresh candidate archive lacks pinned ELF BSS/build metadata")
    bss_doc = _json(files[bss_name], bss_name)
    if (not isinstance(bss_doc, dict) or set(bss_doc) != {"schema", "source_snapshot_sha256", "claim", "variants"} or
            bss_doc["schema"] != 1 or bss_doc["source_snapshot_sha256"] != doc["source_snapshot_sha256"] or
            bss_doc["claim"] != "build-derived ELF BSS facts; not hardware qualification" or
            not isinstance(bss_doc["variants"], list)):
        raise CandidateError("fresh BSS metadata envelope mismatch")
    bss_by_key = {}
    bss_keys = {"board", "nand_profile_id", "load_ecc_bits", "storage_ecc_bits", "elf_sha256",
        "bss_start", "bss_end", "bss_bytes", "sdram_limit", "target", "defconfig",
        "defconfig_sha256", "packed_dtb", "packed_dtb_sha256", "dts_sha256", "machid",
        "build_config_sha256", "dt_compatible", "env_alias", "filter_env_root_keys",
        "allow_missing_env_machid", "method"}
    for item in bss_doc["variants"]:
        if not isinstance(item, dict) or set(item) != bss_keys:
            raise CandidateError("fresh BSS/build metadata row schema mismatch")
        key = (item["board"], item["nand_profile_id"], item["load_ecc_bits"], item["storage_ecc_bits"])
        if key in bss_by_key:
            raise CandidateError("duplicate fresh BSS/build metadata row")
        for field in ("elf_sha256", "defconfig_sha256", "packed_dtb_sha256", "build_config_sha256"):
            if not isinstance(item[field], str) or not re.fullmatch(r"[0-9a-f]{64}", item[field]):
                raise CandidateError("fresh BSS/build metadata digest malformed")
        if (not isinstance(item["dt_compatible"], str) or not item["dt_compatible"] or
                not re.fullmatch(r"[A-Za-z0-9,._+;-]{1,255}", item["dt_compatible"])):
            raise CandidateError("fresh packed-DTB compatible string is malformed")
        expected_env = {"A": (None, 0, 0), "B": (None, 0, 0),
                        "C": (None, 1, 0), "D": ("NX30_2i5x", 1, 1)}.get(item["board"])
        if (expected_env is None or
                (item["env_alias"], item["filter_env_root_keys"], item["allow_missing_env_machid"]) != expected_env):
            raise CandidateError("fresh archive ENV build policy differs from reviewed board behavior")
        if not re.fullmatch(r"[0-9a-f]{64}", item["dts_sha256"]):
            raise CandidateError("fresh source DTS digest malformed")
        if (item["bss_end"] <= item["bss_start"] or item["bss_bytes"] != item["bss_end"] - item["bss_start"] or
                item["bss_end"] > item["sdram_limit"] or item["method"] !=
                "cross-nm -n on exact unstripped release ELF; __bss_start/__bss_end symbols"):
            raise CandidateError("fresh BSS/build metadata bounds or method invalid")
        bss_by_key[key] = item
    if len(bss_by_key) != len(descriptors):
        raise CandidateError("fresh BSS metadata does not exactly cover candidate variants")
    for descriptor in descriptors:
        key = (descriptor["board"], descriptor["nand_profile_id"],
               descriptor["load_ecc_bits"], descriptor["storage_ecc_bits"])
        item = bss_by_key.get(key)
        elf_desc = descriptor["files"].get("appsbl.elf")
        if item is None or not isinstance(elf_desc, dict) or item["elf_sha256"] != elf_desc["sha256"]:
            raise CandidateError("fresh candidate ELF and BSS metadata do not match")
        descriptor["bss_metadata"] = {key: item[key] for key in (
            "board", "nand_profile_id", "load_ecc_bits", "storage_ecc_bits", "elf_sha256",
            "bss_start", "bss_end", "bss_bytes", "sdram_limit")}
        descriptor["build_profile"] = {key: item[key] for key in (
            "board", "target", "defconfig", "defconfig_sha256", "packed_dtb",
            "machid", "build_config_sha256", "dt_compatible", "dts_sha256",
            "env_alias", "filter_env_root_keys", "allow_missing_env_machid")}
    result = {"schema": 2, "archive_sha256": expected_archive_sha256,
            "archive_bytes": len(archive), "manifest_bytes": len(files["MANIFEST.json"]),
            "candidates_bytes": len(files["CANDIDATES.json"]),
            "manifest_sha256": expected_manifest_sha256,
            "candidates_sha256": expected_candidates_sha256,
            "source_snapshot_sha256": doc["source_snapshot_sha256"],
            "source_date_epoch": doc["source_date_epoch"],
            "installation_qualification": "NOT_GRANTED_BY_CANDIDATE_PARSER",
            "candidates": descriptors}
    # Cache validated payload members on disk. The in-memory archive/member
    # dicts are discarded after this function; later reads rehash these files.
    cache_root = _v2_cache_root() / expected_archive_sha256
    cache_root.mkdir(mode=0o700, exist_ok=True)
    cache_root.chmod(0o700)
    for descriptor in descriptors:
        for item in descriptor["files"].values():
            rel = item["path"]
            member_key = (expected_archive_sha256, rel)
            cache_file = cache_root / hashlib.sha256(rel.encode("utf-8")).hexdigest()
            raw = files[rel]
            with _V2_CACHE_LOCK:
                if member_key not in _V2_MEMBER_CACHE:
                    cache_file.write_bytes(raw)
                    cache_file.chmod(0o600)
                    _V2_MEMBER_CACHE[member_key] = (dict(item), cache_file)
                elif _V2_MEMBER_CACHE[member_key][0] != item:
                    raise CandidateError("validated candidate cache descriptor conflict")
    with _V2_CACHE_LOCK:
        _V2_PARSE_CACHE[cache_key] = copy.deepcopy(result)
    return result


def extract_candidate_member_v2(archive_path: Path, descriptor: dict[str, Any], name: str) -> bytes:
    """Re-read one archive member after checking the archive and descriptor pins."""
    path = Path(archive_path)
    try:
        archive_st = path.lstat()
    except OSError as exc:
        raise CandidateError("cannot inspect candidate archive before member extraction") from exc
    if not stat.S_ISREG(archive_st.st_mode) or path.is_symlink():
        raise CandidateError("candidate archive must be an ordinary non-symlink file")
    expected_archive_sha256 = descriptor.get("archive_sha256")
    if not isinstance(expected_archive_sha256, str) or _regular_file_sha256(path) != expected_archive_sha256:
        raise CandidateError("candidate archive changed after descriptor validation")
    if not isinstance(descriptor.get("candidate_id"), str) or not isinstance(descriptor.get("files"), dict):
        raise CandidateError("candidate descriptor is malformed")
    item = descriptor["files"].get(name)
    if not isinstance(item, dict) or set(item) != {"path", "bytes", "sha256"}:
        raise CandidateError("candidate member descriptor is missing")
    prefix = "h3c-uboot-candidates-v2/"
    member_path = item["path"]
    if not member_path.startswith("payloads/") or not _safe_relpath(member_path):
        raise CandidateError("candidate member path is outside v2 payload layout")
    cached_member = _V2_MEMBER_CACHE.get((descriptor["archive_sha256"], member_path))
    if cached_member is not None:
        expected_item, cache_path = cached_member
        try:
            flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
            cache_fd = os.open(cache_path, flags)
            with os.fdopen(cache_fd, "rb") as cache_stream:
                cache_st = os.fstat(cache_stream.fileno())
                payload = cache_stream.read()
        except OSError:
            cached_member = None
        else:
            if (expected_item != item or not stat.S_ISREG(cache_st.st_mode) or
                    stat.S_IMODE(cache_st.st_mode) != 0o600):
                raise CandidateError("cached candidate member type or descriptor mismatch")
            if len(payload) != item["bytes"] or sha256(payload) != item["sha256"]:
                raise CandidateError("cached candidate member digest mismatch")
            return payload
    archive = path.read_bytes()
    if sha256(archive) != expected_archive_sha256:
        raise CandidateError("candidate archive changed after descriptor validation")
    try:
        with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as tf:
            member = tf.getmember(prefix + member_path)
            if not member.isfile():
                raise CandidateError("candidate member is not a regular member")
            stream = tf.extractfile(member)
            if stream is None:
                raise CandidateError("cannot extract candidate member")
            payload = stream.read()
    except (tarfile.TarError, OSError, KeyError) as exc:
        raise CandidateError("candidate member is unavailable") from exc
    if len(payload) != item["bytes"] or sha256(payload) != item["sha256"]:
        raise CandidateError("candidate member changed after descriptor validation")
    return payload


def extract_candidate_partition_v2(archive_path: Path, descriptor: dict[str, Any]) -> bytes:
    return extract_candidate_member_v2(archive_path, descriptor, "appsbl.partition.bin")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True, help="explicit public ABCD candidate archive")
    parser.add_argument("--expected-archive-sha256", required=True,
                        help="reviewed outer SHA-256; must equal the pinned public archive")
    args = parser.parse_args(argv)
    try:
        result = read_public_candidates(args.archive, args.expected_archive_sha256)
    except (CandidateError, OSError) as exc:
        print(f"candidate-manifest: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
