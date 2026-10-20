#!/usr/bin/env python3
"""Build-only OEM slot rules from a SHA-pinned extracted official UBI.

The expected SHA must come from trusted adaptation rules, never from an adjacent
untrusted manifest. A matching SHA is integrity evidence, not an OEM signature.
This does not validate an outer upgrade BIN, kernel execution, boot records or
NAND physics. Output is internal build material, not modifier user output.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import sys
import tempfile
import vendor_ubi

GEOMETRY_KEYS = {'physical_offset', 'size_bytes', 'erase_bytes', 'page_bytes', 'oob_bytes', 'ecc_bits'}
NAMES = ('kernel', 'wifi_fw', 'bt_fw', 'ubi_rootfs', 'rootfs_data')

def safe_runtime():
    if not __debug__ or os.environ.get('PYTHONOPTIMIZE'):
        raise ValueError('optimized Python/PYTHONOPTIMIZE is forbidden: UBI parser uses assertions')

def checked_sha(value, label):
    if not isinstance(value, str) or not re.fullmatch(r'[0-9a-f]{64}', value) or value == '0' * 64:
        raise ValueError(label + ' must be a nonzero lowercase SHA256')
    return value

def geometry(value):
    if not isinstance(value, dict) or set(value) != GEOMETRY_KEYS:
        raise ValueError('geometry requires exactly ' + ','.join(sorted(GEOMETRY_KEYS)))
    if any(type(x) is not int or x < 0 for x in value.values()):
        raise ValueError('geometry fields must be nonnegative integers')
    if (value['physical_offset'] not in (0x900000, 0x3100000) or value['size_bytes'] != 41943040 or
        value['erase_bytes'] != 131072 or value['page_bytes'] != 2048 or
        (value['oob_bytes'], value['ecc_bits']) not in ((64, 4), (128, 4), (128, 8), (80, 8))):
        raise ValueError('unsupported exact physical slot/geometry')
    return dict(value)

def build_manifest(ubi_path, *, expected_ubi_sha256, candidate_sha256, observed_geometry):
    safe_runtime()
    expected = checked_sha(expected_ubi_sha256, 'expected UBI SHA')
    candidate = checked_sha(candidate_sha256, 'candidate SHA')
    geo = geometry(observed_geometry)
    path = Path(ubi_path)
    # Do not follow a symlink, open special files or touch any device node.
    if not stat.S_ISREG(path.lstat().st_mode) or str(path.resolve()).startswith('/dev/'):
        raise ValueError('ordinary regular UBI file required')
    if path.stat().st_size != geo['size_bytes']:
        raise ValueError('UBI must be exactly the 40MiB physical slot export')
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, 'rb') as source:
        if not stat.S_ISREG(os.fstat(source.fileno()).st_mode):
            raise ValueError('ordinary regular UBI file required')
        raw = source.read(geo['size_bytes'] + 1)
    if len(raw) != geo['size_bytes']:
        raise ValueError('UBI changed size during read')
    if hashlib.sha256(raw).hexdigest() != expected:
        raise ValueError('extracted UBI SHA mismatch')
    # Existing parser reopens its Path. Give it a private 0600 frozen copy of
    # the already-pinned bytes, never a second open of the caller's path.
    try:
        with tempfile.TemporaryDirectory(prefix='bootbridge-oem-rule-') as temp:
            frozen = Path(temp) / 'input.ubi'
            fd = os.open(frozen, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, 'wb') as output: output.write(raw)
            parsed, streams, _, tables = vendor_ubi.parse(frozen)
    except (AssertionError, UnicodeError, struct.error, IndexError, ValueError) as exc:
        raise ValueError('invalid/unsupported OEM UBI: ' + repr(exc)) from exc
    records = parsed['volume_table']
    if len(records) != 5 or tuple(r['name'] for r in records) != NAMES or records[4]['type'] != 'dynamic':
        raise ValueError('unsupported five-volume layout')
    if any(not 0 < r['reserved_pebs'] <= 320 for r in records) or 2 + sum(r['reserved_pebs'] for r in records) > 320:
        raise ValueError('invalid reserved capacity')
    # Supplemental rules mirror the C reader's deliberately narrow format;
    # vendor_ubi alone does not reject every padding/compatibility variant.
    for row in parsed['rows']:
        p = row['peb']; block = raw[p * 131072:(p + 1) * 131072]
        if row['kind'] == 'ALL_FF': continue
        if block[5:8] != bytes(3) or block[28:60] != bytes(32):
            raise ValueError('unsupported EC padding')
        if row['kind'] == 'EC_ONLY': continue
        v = block[2048:2112]
        if v[16:20] != bytes(4) or v[36:40] != bytes(4) or v[48:60] != bytes(12):
            raise ValueError('unsupported VID padding')
        vid = row['vol_id']
        if vid == vendor_ubi.LAYOUT:
            if v[7] != 5 or v[20:36] != bytes(16):
                raise ValueError('unsupported layout VID')
        else:
            if v[7] or row['lnum'] >= records[vid]['reserved_pebs']:
                raise ValueError('volume compatibility or logical range invalid')
            if row['vol_type'] == 1 and v[20:36] != bytes(16):
                raise ValueError('unsupported dynamic VID fields')
            if row['vol_type'] == 2 and (not row['data_size'] or row['lnum'] >= row['used_ebs'] or
                (row['lnum'] + 1 < row['used_ebs'] and row['data_size'] != 126976)):
                raise ValueError('invalid static logical size')
    table = tables[0]['raw']
    for i in range(5):
        r = table[i * 172:(i + 1) * 172]; n = len(NAMES[i])
        if r[16+n:144] != bytes(128-n) or r[145:168] != bytes(23):
            raise ValueError('unsupported volume record padding')
    immutable = []
    for i, name in enumerate(NAMES[:4]):
        data = streams[name]
        if not data:
            raise ValueError('missing immutable volume ' + name)
        immutable.append(dict(name=name, size_bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), type=2 if records[i]['type'] == 'static' else 1))
    return dict(schema='bootbridge-oem-slot-manifest-v1', expected_ubi_sha256=expected,
                geometry=geo, candidate_sha256=candidate, volume_table_sha256=hashlib.sha256(table).hexdigest(), immutable=immutable)

def c_header(manifest):
    """Render fixed identifier without injecting input strings into C source."""
    def arr(value):
        checked_sha(value, 'initializer SHA')
        return '{' + ','.join('0x' + value[i:i+2] for i in range(0,64,2)) + '}'
    g = geometry(manifest['geometry'])
    if set(manifest) != {'schema','expected_ubi_sha256','geometry','candidate_sha256','volume_table_sha256','immutable'} or manifest['schema'] != 'bootbridge-oem-slot-manifest-v1':
        raise ValueError('unknown manifest schema/fields')
    checked_sha(manifest['expected_ubi_sha256'], 'UBI SHA')
    if len(manifest['immutable']) != 4:
        raise ValueError('exactly four immutable volumes required')
    lines = ['/* Internal build rule; SHA pin is supplied by trusted adaptation. */', '#ifndef BOOTBRIDGE_COMPILED_OEM_MANIFEST_H', '#define BOOTBRIDGE_COMPILED_OEM_MANIFEST_H', '#include "bridge_oem_slot.h"', '#define BOOTBRIDGE_OEM_UBI_SHA256 "' + manifest['expected_ubi_sha256'] + '"', 'static const bos_manifest bootbridge_oem_manifest = {', '  {' + ','.join(str(g[k])+'u' for k in ('physical_offset','size_bytes','erase_bytes','page_bytes','oob_bytes','ecc_bits')) + '},', '  '+arr(manifest['candidate_sha256'])+',', '  '+arr(manifest['volume_table_sha256'])+',', '  {']
    for i,v in enumerate(manifest['immutable']):
        if set(v) != {'name','size_bytes','sha256','type'} or v['name'] != NAMES[i] or type(v['size_bytes']) is not int or not 0 < v['size_bytes'] <= 41943040 or type(v['type']) is not int or v['type'] not in (1,2):
            raise ValueError('invalid immutable volume rule')
        lines.append('    {'+str(v['size_bytes'])+'u,'+arr(v['sha256'])+','+str(v['type'])+'u},')
    return '\n'.join(lines + ['  }','};','#endif',''])

def c_header_set(manifests):
    """One pinned official content rule in several exact read-IO geometries.

    This does not certify those geometries on hardware or qualify installation.
    Different versions of OEM content need a separate, reviewed matching policy;
    this bounded set deliberately rejects ambiguous same-geometry entries.
    """
    if not isinstance(manifests, (list, tuple)) or not 1 <= len(manifests) <= 8:
        raise ValueError('OEM manifest set requires 1..8 exact geometry records')
    rendered = []
    seen = set()
    common = None
    for manifest in manifests:
        header = c_header(manifest)  # validates all fields before rendering C
        identity = {k: v for k, v in manifest.items() if k != 'geometry'}
        if common is not None and identity != common:
            raise ValueError('OEM manifest set must bind one exact official content and candidate')
        common = identity
        geo = geometry(manifest['geometry'])
        key = tuple(geo[k] for k in sorted(GEOMETRY_KEYS))
        if key in seen:
            raise ValueError('duplicate or ambiguous OEM manifest geometry')
        seen.add(key)
        prefix = 'static const bos_manifest bootbridge_oem_manifest = '
        rendered.append(header.split(prefix, 1)[1].split(';', 1)[0])
    return '\n'.join([
        '/* Internal pinned OEM logical rules; IO geometry is not installation qualification. */',
        '#ifndef BOOTBRIDGE_COMPILED_OEM_MANIFEST_H',
        '#define BOOTBRIDGE_COMPILED_OEM_MANIFEST_H',
        '#include "bridge_oem_slot.h"',
        '#define BOOTBRIDGE_OEM_UBI_SHA256 "' + common['expected_ubi_sha256'] + '"',
        'static const bos_manifest bootbridge_oem_manifests[] = {',
        ',\n'.join(rendered), '};',
        '#define BI_OEM_MANIFESTS bootbridge_oem_manifests',
        '#define BI_OEM_MANIFEST_COUNT (sizeof(bootbridge_oem_manifests)/sizeof(bootbridge_oem_manifests[0]))',
        '#endif', ''])

def c_pool_header(entries):
    """Internal compiled pool; caller must supply audited provenance/policy IDs.

    SHA/source-review fields identify trusted build rules, never independently
    authenticate input. No rule here registers a loader or hardware qualification.
    Revision covers the complete validated rules, provenance and candidate.
    """
    safe_runtime()
    keys = {'manifest','source_package_sha256','source_review_id','content_group',
            'exit_policy','product','alias','table_policy'}
    if not isinstance(entries, (list, tuple)) or not 1 <= len(entries) <= 64:
        raise ValueError('OEM pool requires 1..64 compiled rules')
    seen_alias = set(); groups = {}; contents = {}; rendered = []; checked = []; candidate = None
    for e in entries:
        if not isinstance(e, dict) or set(e) != keys:
            raise ValueError('unknown/missing OEM pool fields')
        m = e['manifest']; header = c_header(m)
        checked_sha(e['source_package_sha256'], 'source package SHA')
        if not isinstance(e['source_review_id'], str) or not re.fullmatch(r'[a-z0-9][a-z0-9._-]{0,95}', e['source_review_id']):
            raise ValueError('bounded reviewed provenance identifier required')
        for key in ('content_group','product','alias'):
            if type(e[key]) is not int or not 0 < e[key] <= 0xffffffff:
                raise ValueError('positive uint32 policy/group/product/alias required')
        if type(e['exit_policy']) is not int or not 0 <= e['exit_policy'] <= 0xffffffff:
            raise ValueError('exit policy must be uint32; zero means UNAVAILABLE')
        if type(e['table_policy']) is not int or e['table_policy'] not in (0,1,2):
            raise ValueError('table policy must be RAW_EXACT(0), AUTORESIZE_UNAVAILABLE(1), or POSTATTACH_AUTORESIZE(2)')
        if candidate is not None and candidate != m['candidate_sha256']:
            raise ValueError('compiled pool must bind one candidate')
        candidate = m['candidate_sha256']
        geo = tuple(m['geometry'][k] for k in sorted(GEOMETRY_KEYS))
        if (geo,e['alias']) in seen_alias: raise ValueError('duplicate alias/geometry rule')
        seen_alias.add((geo,e['alias']))
        content = json.dumps({k:m[k] for k in ('volume_table_sha256','immutable')},sort_keys=True,separators=(',',':'))
        equivalence = (e['content_group'],e['exit_policy'],e['product'])
        if e['content_group'] in groups and groups[e['content_group']] != (content,equivalence):
            raise ValueError('equivalence group has different content or exit policy/product')
        groups[e['content_group']] = (content,equivalence)
        if content in contents and contents[content] != equivalence:
            raise ValueError('same logical content has conflicting equivalence/exit policy/product')
        contents[content] = equivalence
        if sum(1 for x in checked if x['manifest']['geometry']==m['geometry'] and x['content_group']==e['content_group']) >= 8:
            raise ValueError('equivalence group exceeds eight aliases per geometry')
        prefix='static const bos_manifest bootbridge_oem_manifest = '
        initializer=header.split(prefix,1)[1].split(';',1)[0]
        rendered.append('{'+initializer+','+','.join(str(e[k])+'u' for k in ('content_group','exit_policy','product','alias','table_policy'))+'}')
        checked.append(e)
    revision = hashlib.sha256(json.dumps({'schema':'bootbridge-oem-pool-v2','rules':checked},sort_keys=True,separators=(',',':'),ensure_ascii=True).encode('ascii')).digest()
    array='{'+','.join(str(x) for x in revision)+'}'
    return '\n'.join(['/* Compiled audited logical rules; no loader/hardware qualification. */',
        '#ifndef BOOTBRIDGE_COMPILED_OEM_POOL_H','#define BOOTBRIDGE_COMPILED_OEM_POOL_H',
        '#include "bridge_oem_slot.h"','static const bos_pool_rule bootbridge_oem_pool[] = {',
        ',\n'.join(rendered),'};','static const uint8_t bootbridge_oem_pool_revision[32] = '+array+';',
        '#define BI_OEM_POOL bootbridge_oem_pool',
        '#define BI_OEM_POOL_COUNT (sizeof(bootbridge_oem_pool)/sizeof(bootbridge_oem_pool[0]))',
        '#define BI_OEM_POOL_REVISION bootbridge_oem_pool_revision','#endif',''])


def strict_json(text):
    def pairs(values):
        result = {}
        for k,v in values:
            if k in result: raise ValueError('duplicate JSON field: ' + k)
            result[k] = v
        return result
    return json.loads(text, object_pairs_hook=pairs)

def main():
    safe_runtime()
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--ubi', required=True)
    p.add_argument('--ubi-sha256', required=True)
    p.add_argument('--candidate-sha256', required=True)
    p.add_argument('--geometry-json', required=True, help='inline JSON with exact geometry fields')
    p.add_argument('--format', choices=('c','json'), default='c', help='internal build artifact only')
    p.add_argument('--output', required=True)
    a = p.parse_args()
    out = Path(a.output)
    if out.resolve() == Path(a.ubi).resolve(): raise ValueError('output must not overwrite UBI input')
    m = build_manifest(a.ubi, expected_ubi_sha256=a.ubi_sha256, candidate_sha256=a.candidate_sha256, observed_geometry=strict_json(a.geometry_json))
    text = c_header(m) if a.format == 'c' else json.dumps(m,sort_keys=True,indent=2)+'\n'
    # Exclusive creation preserves any existing original or build artifact.
    with out.open('x', encoding='utf-8') as f: f.write(text)

if __name__ == '__main__':
    try: main()
    except (ValueError, OSError) as exc:
        print('OEM manifest rejected: ' + str(exc), file=sys.stderr)
        raise SystemExit(1)
