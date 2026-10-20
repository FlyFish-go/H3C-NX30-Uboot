#!/usr/bin/env python3
"""Internal LAB-only compiler. Trusted expected pins come from reviewed build evidence.

No patch/recipe JSON is parsed. This tool creates compiled C engineering material,
never an end-user artifact, production catalog entry, or installation permission.
"""
import argparse
import dataclasses
import hashlib
import json
import os
from pathlib import Path
import re
import stat

INPUT_MAX = 64 * 1024 * 1024
OUTPUT_MAX = 128 * 1024 * 1024
MAX_OPS = 262144
ANCHOR = STRIDE = 64
BUCKET_MAX = 4
MAX_ANCHORS = INPUT_MAX // STRIDE
HASH_MASK = (1 << 64) - 1
BASE = 257
POWER = pow(BASE, ANCHOR - 1, 1 << 64)

@dataclasses.dataclass(frozen=True)
class Pins:
    input_size: int
    input_sha256: str
    output_size: int
    output_sha256: str

    def validate(self):
        for n, limit in ((self.input_size, INPUT_MAX), (self.output_size, OUTPUT_MAX)):
            if type(n) is not int or not 0 < n <= limit:
                raise ValueError('size outside fixed LAB limits')
        for digest in (self.input_sha256, self.output_sha256):
            if not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest) or digest == '0' * 64:
                raise ValueError('exact nonzero lowercase SHA256 pin required')

@dataclasses.dataclass(frozen=True)
class Op:
    kind: str
    offset: int
    size: int
    data_offset: int = 0


def anchor_hash(data):
    h = 0
    for byte in data:
        h = ((h * BASE) + byte) & HASH_MASK
    return h


def extend_match(source, target, source_pos, target_pos):
    length = ANCHOR
    limit = min(len(source) - source_pos, len(target) - target_pos)
    while length < limit:
        count = min(65536, limit - length)
        a = source[source_pos + length:source_pos + length + count]
        b = target[target_pos + length:target_pos + length + count]
        if a == b:
            length += count
        else:
            for x, y in zip(a, b):
                if x != y:
                    return length
                length += 1
    return length


def build_plan(source, target):
    if not 0 < len(source) <= INPUT_MAX or not 0 < len(target) <= OUTPUT_MAX:
        raise ValueError('input/output outside fixed limits')
    index = {}
    anchors = 0
    for pos in range(0, len(source) - ANCHOR + 1, STRIDE):
        anchors += 1
        if anchors > MAX_ANCHORS:
            raise ValueError('anchor limit exceeded')
        key = anchor_hash(source[pos:pos + ANCHOR])
        bucket = index.setdefault(key, [])
        if len(bucket) < BUCKET_MAX:
            bucket.append(pos)  # Ascending first four offsets, never hash iteration order.
    operations, payload = [], bytearray()

    def emit(kind, offset, size, data=b''):
        if not size:
            return
        if len(operations) == MAX_OPS or len(payload) + len(data) > OUTPUT_MAX:
            raise ValueError('compiled operation/data limit exceeded')
        if kind == 'COPY' and operations and operations[-1].kind == kind and operations[-1].offset + operations[-1].size == offset:
            old = operations.pop()
            operations.append(Op(kind, old.offset, old.size + size))
            return
        operations.append(Op(kind, offset, size, len(payload)))
        payload.extend(data)

    def unmatched(start, end):
        stop = min(end, len(source))
        if start < stop:
            a, b = source[start:stop], target[start:stop]
            if a == b:
                emit('COPY', start, stop - start)
            else:
                emit('XOR', start, stop - start, bytes(x ^ y for x, y in zip(a, b)))
        start = max(start, stop)
        if start < end:
            # Literals are allowed only at target coordinates beyond source EOF.
            emit('LITERAL', 0, end - start, target[start:end])

    pos = pending = 0
    rolling = anchor_hash(target[:ANCHOR]) if len(target) >= ANCHOR else 0
    while pos + ANCHOR <= len(target):
        match = None
        for candidate in index.get(rolling, ()):
            # Hashes only narrow the search; full anchor bytes are mandatory.
            if source[candidate:candidate + ANCHOR] == target[pos:pos + ANCHOR]:
                match = candidate
                break
        if match is not None:
            unmatched(pending, pos)
            length = extend_match(source, target, match, pos)
            emit('COPY', match, length)
            pos += length
            pending = pos
            if pos + ANCHOR <= len(target):
                rolling = anchor_hash(target[pos:pos + ANCHOR])
            continue
        if pos + ANCHOR < len(target):
            rolling = (((rolling - target[pos] * POWER) * BASE) + target[pos + ANCHOR]) & HASH_MASK
        pos += 1
    unmatched(pending, len(target))
    return operations, bytes(payload), {'anchors': anchors, 'distinct_anchor_hashes': len(index), 'bucket_limit': BUCKET_MAX}


def reconstruct(source, operations, data, output_size):
    if not operations or len(operations) > MAX_OPS or output_size > OUTPUT_MAX:
        raise ValueError('bad plan bounds')
    result = bytearray()
    for op in operations:
        if op.kind not in ('COPY', 'XOR', 'LITERAL') or op.size <= 0 or op.size > output_size - len(result):
            raise ValueError('bad operation')
        if op.kind != 'LITERAL' and (op.offset < 0 or op.offset > len(source) or op.size > len(source) - op.offset):
            raise ValueError('source range out of bounds')
        if op.kind != 'COPY' and (op.data_offset < 0 or op.data_offset > len(data) or op.size > len(data) - op.data_offset):
            raise ValueError('data range out of bounds')
        if op.kind == 'COPY':
            result.extend(source[op.offset:op.offset + op.size])
        elif op.kind == 'XOR':
            result.extend(x ^ y for x, y in zip(source[op.offset:op.offset + op.size], data[op.data_offset:op.data_offset + op.size]))
        else:
            if op.offset or len(result) < len(source):
                raise ValueError('literal before source EOF')
            result.extend(data[op.data_offset:op.data_offset + op.size])
    if len(result) != output_size:
        raise ValueError('incomplete output')
    return bytes(result)


def read_pinned(path, expected_size, expected_hash):
    if not stat.S_ISREG(os.lstat(path).st_mode):
        raise ValueError('ordinary file required before open')
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_size != expected_size:
            raise ValueError('expected ordinary file with exact size')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            snapshot = stream.read(expected_size + 1)
            if len(snapshot) != expected_size or hashlib.sha256(snapshot).hexdigest() != expected_hash:
                raise ValueError('trusted file pin mismatch')
            stream.seek(0)
            h = hashlib.sha256()
            total = 0
            while True:
                block = stream.read(min(1024 * 1024, expected_size - total + 1))
                if not block:
                    break
                total += len(block)
                if total > expected_size:
                    raise ValueError('file grew during second read')
                h.update(block)
            check = h.hexdigest()
            after = os.fstat(fd)
            if check != expected_hash or after.st_size != expected_size:
                raise ValueError('file changed during read')
        return snapshot
    finally:
        os.close(fd)


def _array(values):
    return '{' + ','.join('0x' + values[i:i + 2] for i in range(0, len(values), 2)) + '}'


def compile_files(source_path, target_path, output_directory, pins):
    pins.validate()
    source = read_pinned(source_path, pins.input_size, pins.input_sha256)
    target = read_pinned(target_path, pins.output_size, pins.output_sha256)
    operations, data, index_stats = build_plan(source, target)
    if reconstruct(source, operations, data, pins.output_size) != target:
        raise ValueError('internal reconstruction mismatch')
    # Source handles are checked twice above; check paths again before publishing
    # compiled material so a changed/replaced original does not go unnoticed.
    read_pinned(source_path, pins.input_size, pins.input_sha256)
    read_pinned(target_path, pins.output_size, pins.output_sha256)
    out = Path(output_directory)
    out.mkdir(mode=0o700, parents=False, exist_ok=False)
    recipe_id = 'LAB-NOT-QUALIFIED-' + pins.input_sha256[:16] + '-' + pins.output_sha256[:16]
    with (out / 'recipe.c').open('x', encoding='ascii', newline='\n') as stream:
        stream.write('/* Generated internal LAB_NOT_QUALIFIED engineering material. Never a production registry. */\n#include "bb_export_plan.h"\n')
        if data:
            stream.write('static const unsigned char bb_lab_data[] = {\n')
            for pos in range(0, len(data), 24):
                stream.write(','.join('0x%02x' % b for b in data[pos:pos + 24]) + ',\n')
            stream.write('};\n')
        stream.write('static const bb_plan_op bb_lab_ops[] = {\n')
        for op in operations:
            pointer = 'NULL' if op.kind == 'COPY' else 'bb_lab_data+%du' % op.data_offset
            stream.write('{BB_PLAN_%s,%du,%du,%s},\n' % (op.kind, op.offset, op.size, pointer))
        stream.write('};\nconst bb_plan_recipe bb_lab_recipe = {\n')
        stream.write('"%s",%du,%s,%du,%s,bb_lab_ops,%du\n};\n' % (recipe_id, pins.input_size, _array(pins.input_sha256), pins.output_size, _array(pins.output_sha256), len(operations)))
    with (out / 'recipe.h').open('x', encoding='ascii') as stream:
        stream.write('#ifndef BB_LAB_COMPILED_RECIPE_H\n#define BB_LAB_COMPILED_RECIPE_H\n#include "bb_export_plan.h"\nextern const bb_plan_recipe bb_lab_recipe;\n#endif\n')
    totals = {kind: sum(op.size for op in operations if op.kind == kind) for kind in ('COPY', 'XOR', 'LITERAL')}
    native = Path(__file__).resolve().parents[1] / 'src'
    report = {'schema': 1, 'qualification': 'LAB_NOT_QUALIFIED', 'production_install_available': False,
              'pins': dataclasses.asdict(pins), 'recipe_id': recipe_id, 'operation_count': len(operations),
              'operation_bytes': totals, 'compiled_data_bytes': len(data), 'compiled_data_sha256': hashlib.sha256(data).hexdigest(),
              'strategy': '64B source anchors at stride64; rolling64 hash; first confirmed candidate among first4 per bucket; bytewise target scan; copy extension; same-offset XOR remainder; EOF-only literal',
              'limits': {'input': INPUT_MAX, 'output': OUTPUT_MAX, 'operations': MAX_OPS, 'anchors': MAX_ANCHORS},
              'index': index_stats, 'reconstruction_equal': True,
              'provenance': {'compiler_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                             'native_source_sha256': {name: hashlib.sha256((native / name).read_bytes()).hexdigest() for name in ('bb_export.c', 'bb_export_plan.h', 'bb_export.h')},
                             'input_metadata_source': 'explicit trusted expected pin arguments; never self-described patch JSON',
                             'literal_semantics': 'only target positions beyond source EOF; bytes from independently pinned audited LAB candidate; no installation qualification'},
              'generated_sha256': {name: hashlib.sha256((out / name).read_bytes()).hexdigest() for name in ('recipe.c', 'recipe.h')}}
    with (out / 'provenance.json').open('x', encoding='ascii') as stream:
        json.dump(report, stream, indent=2, sort_keys=True)
        stream.write('\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True)
    parser.add_argument('--target', required=True)
    parser.add_argument('--new-output-directory', required=True)
    parser.add_argument('--expected-source-size', required=True, type=int)
    parser.add_argument('--expected-source-sha256', required=True)
    parser.add_argument('--expected-target-size', required=True, type=int)
    parser.add_argument('--expected-target-sha256', required=True)
    a = parser.parse_args()
    report = compile_files(a.source, a.target, a.new_output_directory,
                           Pins(a.expected_source_size, a.expected_source_sha256, a.expected_target_size, a.expected_target_sha256))
    print(json.dumps(report, indent=2, sort_keys=True))

if __name__ == '__main__':
    main()
