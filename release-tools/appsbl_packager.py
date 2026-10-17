#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the normalized Qualcomm MBN-v3 APPSBL container from a stripped ELF.

The byte layout in this module is independently implemented from the public
GPL rt3 metadata-core contract. It does not call Qualcomm's proprietary
elftombn/mbn_tools utilities. The command writes an MBN and an optional
0xff-padded 0x140000-byte APPSBL partition image.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import stat
import struct
import sys

ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
ELF_PHDR = struct.Struct("<IIIIIIII")
ELF32_HEADER_SIZE = 52
ELF32_PHDR_SIZE = 32
ET_EXEC = 2
ET_DYN = 3
EM_ARM = 40
PT_LOAD = 1
PT_GNU_STACK = 0x6474E551
LOAD_ADDRESS = 0x4A920000
HASH_OFFSET = 0x1000
HASH_SIZE = 0xA8
LOAD_OFFSET = 0x3000
PARTITION_SIZE = 0x140000
HASH_WORDS = struct.Struct("<10I")


class PackError(ValueError):
    """Input ELF violates the supported APPSBL contract."""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _u32(value: int) -> bytes:
    return struct.pack("<I", value)


def _parse_stripped_elf(data: bytes) -> tuple[bytes, int]:
    if len(data) < ELF32_HEADER_SIZE:
        raise PackError("ELF is shorter than its ELF32 header")
    fields = ELF_HEADER.unpack_from(data)
    ident, e_type, machine, version, entry, phoff, shoff, flags, ehsize, phentsize, phnum, shentsize, shnum, shstrndx = fields
    if ident[:7] != b"\x7fELF\x01\x01\x01":
        raise PackError("expected little-endian ELF32, current ELF version")
    if e_type not in (ET_EXEC, ET_DYN) or machine != EM_ARM or version != 1:
        raise PackError("expected ARM ELF32 ET_EXEC or ET_DYN")
    if entry != LOAD_ADDRESS:
        raise PackError(f"entry point 0x{entry:08x} is not 0x{LOAD_ADDRESS:08x}")
    if ehsize != ELF32_HEADER_SIZE or phoff != ELF32_HEADER_SIZE or phentsize != ELF32_PHDR_SIZE:
        raise PackError("unsupported ELF header/program-header geometry")
    if phnum != 2:
        raise PackError("expected exactly two program headers")
    if shoff == 0:
        if shnum != 0 or shstrndx != 0 or shentsize not in (0, 40):
            raise PackError("inconsistent absent section-header table")
    elif shentsize != 40 or shnum == 0 or shstrndx >= shnum or shoff > len(data) or shnum > (len(data) - shoff) // shentsize:
        raise PackError("malformed section-header table")
    ph_end = phoff + phnum * phentsize
    if ph_end > len(data):
        raise PackError("program-header table extends beyond ELF")
    rows = [ELF_PHDR.unpack_from(data, phoff + i * phentsize) for i in range(phnum)]
    load_rows = [row for row in rows if row[0] == PT_LOAD]
    stack_rows = [row for row in rows if row[0] == PT_GNU_STACK]
    if len(load_rows) != 1 or len(stack_rows) != 1:
        raise PackError("expected exactly one LOAD and one GNU_STACK program header")
    p_type, p_offset, vaddr, paddr, filesz, memsz, p_flags, p_align = load_rows[0]
    if vaddr != LOAD_ADDRESS or paddr != LOAD_ADDRESS or not filesz or filesz != memsz or p_flags != 7:
        raise PackError("LOAD segment must be nonempty, identity-mapped, fully file-backed, and RWX")
    if p_offset < ph_end or p_offset > len(data) or filesz > len(data) - p_offset:
        raise PackError("LOAD segment is out of bounds or overlaps ELF headers")
    if p_align not in (0, 1) and (p_align & (p_align - 1) or ((vaddr - p_offset) & (p_align - 1))):
        raise PackError("LOAD segment alignment is invalid")
    stack = stack_rows[0]
    if stack != (PT_GNU_STACK, 0, 0, 0, 0, 0, 7, 0x10):
        raise PackError("GNU_STACK row differs from supported empty executable-stack record")
    if LOAD_ADDRESS + filesz > 0x4AA00000:
        raise PackError("LOAD segment exceeds the audited APPSBL SDRAM window")
    if filesz > PARTITION_SIZE - LOAD_OFFSET:
        raise PackError("LOAD segment cannot fit without truncation in APPSBL partition")
    return data[p_offset:p_offset + filesz], entry


def package_elf(data: bytes) -> bytes:
    """Return normalized MBN bytes, preserving the ELF LOAD payload verbatim."""
    load, entry = _parse_stripped_elf(data)
    size = LOAD_OFFSET + len(load)
    hash_address = (LOAD_ADDRESS + len(load) + 0xFFF) & ~0xFFF
    if size > PARTITION_SIZE:
        raise PackError("normalized MBN exceeds APPSBL partition size")

    out = bytearray(size)  # MBN header/hash area padding is zero-filled by contract.
    out[:ELF32_HEADER_SIZE] = data[:ELF32_HEADER_SIZE]
    struct.pack_into("<I", out, 24, entry)
    struct.pack_into("<I", out, 28, ELF32_HEADER_SIZE)
    struct.pack_into("<I", out, 32, 0)
    struct.pack_into("<HHHHHH", out, 40, ELF32_HEADER_SIZE, ELF32_PHDR_SIZE, 4, 40, 0, 0)

    phdrs = (
        (0, 0, 0, 0, 0xB4, 0, 0x07000000, 0),
        (0, HASH_OFFSET, hash_address, hash_address, HASH_SIZE, 0x1000, 0x02200000, 0x1000),
        (PT_LOAD, LOAD_OFFSET, LOAD_ADDRESS, LOAD_ADDRESS, len(load), len(load), 7, 0x1000),
        (PT_GNU_STACK, 0x2000, 0, 0, 0, 0, 7, 0x10),
    )
    for i, row in enumerate(phdrs):
        ELF_PHDR.pack_into(out, ELF32_HEADER_SIZE + i * ELF32_PHDR_SIZE, *row)

    hash_table = (
        21, 3, 0,
        hash_address + HASH_OFFSET + 40,
        128, 128,
        hash_address + HASH_OFFSET + HASH_SIZE,
        0,
        hash_address + HASH_OFFSET + HASH_SIZE,
        0,
    )
    HASH_WORDS.pack_into(out, HASH_OFFSET, *hash_table)
    out[HASH_OFFSET + 40:HASH_OFFSET + 72] = hashlib.sha256(out[:0xB4]).digest()
    out[LOAD_OFFSET:LOAD_OFFSET + len(load)] = load
    out[HASH_OFFSET + 104:HASH_OFFSET + 136] = hashlib.sha256(load).digest()
    return bytes(out)


def padded_partition(mbn: bytes) -> bytes:
    if not mbn or len(mbn) > PARTITION_SIZE:
        raise PackError("MBN size is empty or exceeds 0x140000 APPSBL partition")
    return mbn + b"\xff" * (PARTITION_SIZE - len(mbn))


def _preflight_paths(input_path: pathlib.Path, outputs: list[pathlib.Path]) -> None:
    try:
        input_stat = input_path.lstat()
    except OSError as exc:
        raise PackError(f"cannot inspect input path: {exc}") from exc
    if stat.S_ISLNK(input_stat.st_mode) or not stat.S_ISREG(input_stat.st_mode):
        raise PackError("input must be a regular, non-symlink file")
    input_real = input_path.resolve(strict=True)
    keys = []
    for output in outputs:
        if output.is_symlink():
            raise PackError(f"refusing symlink output path: {output}")
        output_real = output.resolve(strict=False)
        if output_real == input_real:
            raise PackError("output path resolves to the input file")
        keys.append(output_real)
        if output.exists():
            raise PackError(f"refusing to overwrite existing output: {output}")
    if len(set(keys)) != len(keys):
        raise PackError("output paths resolve to the same destination")


def _exclusive_write(path: pathlib.Path, data: bytes) -> tuple[pathlib.Path, int, int]:
    path.parent.mkdir(parents=True, exist_ok=True)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    fd = os.open(path, flags, 0o644)
    st = os.fstat(fd)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
    except BaseException:
        # fdopen owns fd after construction; the cleanup below removes only
        # the file this call created, even if another process replaced it.
        _remove_if_same_inode(path, st.st_dev, st.st_ino)
        raise
    return path, st.st_dev, st.st_ino


def _remove_if_same_inode(path: pathlib.Path, device: int, inode: int) -> None:
    try:
        current = path.lstat()
        if current.st_dev == device and current.st_ino == inode:
            path.unlink()
    except FileNotFoundError:
        pass


def write_outputs(input_path: pathlib.Path, output_mbn: pathlib.Path,
                  output_partition: pathlib.Path | None = None) -> tuple[bytes, bytes | None]:
    """Validate paths, package fully in memory, then create outputs exclusively.

    If creating/writing a later output fails, only files created by this call
    are removed. Existing files and symlink targets are never opened for write.
    """
    outputs = [output_mbn] + ([output_partition] if output_partition else [])
    _preflight_paths(input_path, outputs)
    elf = input_path.read_bytes()
    mbn = package_elf(elf)
    partition = padded_partition(mbn) if output_partition else None
    created: list[tuple[pathlib.Path, int, int]] = []
    try:
        created.append(_exclusive_write(output_mbn, mbn))
        if output_partition is not None and partition is not None:
            created.append(_exclusive_write(output_partition, partition))
    except BaseException:
        for path, device, inode in reversed(created):
            _remove_if_same_inode(path, device, inode)
        raise
    return mbn, partition


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input_elf", type=pathlib.Path)
    parser.add_argument("output_mbn", type=pathlib.Path)
    parser.add_argument("--partition", type=pathlib.Path, help="also write a 0x140000-byte FF-padded partition image")
    parser.add_argument("--board", choices=("A", "B", "C", "D"), required=True,
                        help="release label recorded for the caller; board identity must be verified from build provenance")
    args = parser.parse_args(argv)
    try:
        mbn, partition = write_outputs(args.input_elf, args.output_mbn, args.partition)
        elf_sha = sha256(args.input_elf.read_bytes())
        print(json.dumps({"board_label": args.board, "input": str(args.input_elf),
                          "input_sha256": elf_sha, "mbn": str(args.output_mbn),
                          "mbn_size": len(mbn), "mbn_sha256": sha256(mbn),
                          "partition": str(args.partition) if args.partition else None,
                          "partition_sha256": sha256(partition) if partition else None,
                          "load_offset": f"0x{LOAD_OFFSET:x}", "partition_size": f"0x{PARTITION_SIZE:x}"},
                         sort_keys=True))
        return 0
    except (OSError, PackError) as exc:
        print(f"appsbl-packager: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
