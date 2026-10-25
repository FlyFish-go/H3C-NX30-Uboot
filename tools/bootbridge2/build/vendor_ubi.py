#!/usr/bin/env python3
"""Host-only pre-attach UBI readback verifier. Accepts ordinary export files only."""
from pathlib import Path
import struct,zlib,json,hashlib,collections,sys,stat
PEB=131072;LEB=126976;LAYOUT=0x7fffefff
def digest(b):
    return hashlib.sha256(b).hexdigest()

def crc(b):
    return (zlib.crc32(b) ^ 4294967295) & 4294967295

def parse(path, bad=()):
    b = Path(path).read_bytes()
    assert len(b) % PEB == 0
    rows = []
    tables = []
    payloads = {}
    seqs = set()
    ec_counts = collections.Counter()
    ff = []
    empty = []
    for i in range(len(b) // PEB):
        x = b[i * PEB:(i + 1) * PEB]
        if i in bad:
            rows.append(dict(peb=i, kind='BAD_FIXTURE_SKIPPED'))
            continue
        if x == b'\xff' * PEB:
            ff.append(i)
            rows.append(dict(peb=i, kind='ALL_FF'))
            continue
        assert x[:4] == b'UBI#' and x[4] == 1, ('EC magic/version', path, i)
        assert crc(x[:60]) == struct.unpack_from('>I', x, 60)[0]
        ec = struct.unpack_from('>Q', x, 8)[0]
        (vo, do, seq) = struct.unpack_from('>III', x, 16)
        assert (vo, do) == (2048, 4096)
        ec_counts[ec] += 1
        seqs.add(seq)
        v = x[vo:vo + 64]
        row = dict(peb=i, ec=ec, image_seq=seq, vid_offset=vo, data_offset=do, kind='EC_ONLY')
        if v == b'\xff' * 64:
            assert x[vo:] == b'\xff' * (PEB - vo)
            empty.append(i)
            rows.append(row)
            continue
        assert v[:4] == b'UBI!' and v[4] == 1 and (crc(v[:60]) == struct.unpack_from('>I', v, 60)[0])
        typ = v[5]
        copy = v[6]
        (vol, ln) = struct.unpack_from('>II', v, 8)
        (ds, num, pad, data_crc) = struct.unpack_from('>IIII', v, 20)
        sq = struct.unpack_from('>Q', v, 40)[0]
        assert typ in (1, 2) and copy == 0 and (pad == 0)
        row.update(kind='EC_VID', vol_id=vol, lnum=ln, vol_type=typ, data_size=ds, used_ebs=num, sqnum=sq)
        if vol == LAYOUT:
            assert typ == 1 and ln in (0, 1)
            raw = x[do:do + 128 * 172]
            rec = []
            for j in range(128):
                r = raw[j * 172:(j + 1) * 172]
                assert crc(r[:168]) == struct.unpack_from('>I', r, 168)[0]
                (reserved, align, dp) = struct.unpack_from('>III', r)
                if not reserved:
                    assert r[:168] == b'\x00' * 168
                    continue
                nn = struct.unpack_from('>H', r, 14)[0]
                assert align == 1 and dp == 0 and (0 < nn <= 127) and (r[13] == 0) and (r[12] in (1, 2)) and (r[144] in (0, 1))
                rec.append(dict(id=j, name=r[16:16 + nn].decode(), reserved_pebs=reserved, capacity=reserved * LEB, type='static' if r[12] == 2 else 'dynamic', autoresize=bool(r[144] & 1), flags=r[144]))
            tables.append(dict(peb=i, lnum=ln, records=rec, raw=raw))
        else:
            assert vol in range(5)
            data = x[do:do + ds] if typ == 2 else x[do:]
            if typ == 2:
                assert ds <= LEB and num > 0 and (crc(data) == data_crc)
            payloads.setdefault(vol, []).append((ln, data, typ, num))
        rows.append(row)
    assert len(seqs) == 1 and len(tables) == 2 and (sorted((t['lnum'] for t in tables)) == [0, 1])
    assert tables[0]['raw'] == tables[1]['raw']
    records = tables[0]['records']
    assert [(r['id'], r['name']) for r in records] == list(enumerate(['kernel', 'wifi_fw', 'bt_fw', 'ubi_rootfs', 'rootfs_data']))
    streams = {}
    vol_manifest = []
    for r in records:
        arr = sorted(payloads.get(r['id'], []))
        assert len(arr) <= r['reserved_pebs']
        assert [n for (n, _, _, _) in arr] == list(range(len(arr))), ('duplicate/gap/overlap', r)
        if r['type'] == 'static':
            assert arr and all((typ == 2 and num == len(arr) for (_, _, typ, num) in arr))
        else:
            assert all((typ == 1 for (_, _, typ, _) in arr))
        data = b''.join((v for (_, v, _, _) in arr))
        streams[r['name']] = data
        vol_manifest.append(dict(name=r['name'], size=len(data), sha256=digest(data), allocated_ebs=len(arr), reserved_ebs=r['reserved_pebs'], type=r['type'], autoresize=r['autoresize']))
    result = dict(path=str(path), size=len(b), raw_sha256=digest(b), total_pebs=len(b) // PEB, bad_fixture_pebs=list(bad), good_pebs=len(b) // PEB - len(bad), ec_pebs=sum(ec_counts.values()), ec_vid_pebs=sum((r['kind'] == 'EC_VID' for r in rows)), ec_only_pebs=len(empty), all_ff_pebs=len(ff), all_ff_indices=ff, all_ff_tail_count=sum((i in ff for i in range(len(b) // PEB - 1, -1, -1))) if ff == list(range(len(b) // PEB - len(ff), len(b) // PEB)) else 0, erase_counter_histogram=dict(ec_counts), image_sequences=sorted(seqs), volume_table=records, volume_table_raw_sha256=digest(tables[0]['raw']), volume_manifest=vol_manifest, all_ec_vid_table_static_data_crc_valid=True, no_duplicate_lnums_or_peb_overlap=True, rows=rows)
    return (result, streams, b, tables)
def verify(export,oracle):
 for p in (Path(export),Path(oracle)):
  if not stat.S_ISREG(p.stat().st_mode) or str(p.resolve()).startswith('/dev/'):
   raise ValueError('Only ordinary offline export files allowed')
 actual,streams,_,_=parse(export); expected,expected_streams,_,_=parse(oracle)
 assert actual['size']==41943040 and actual['total_pebs']==320
 assert actual['ec_pebs']==320 and actual['all_ff_pebs']==0
 assert actual['volume_table']==expected['volume_table']
 assert streams==expected_streams
 return dict(result='PASS_PRE_ATTACH_LOGICAL_CONTENT_ONLY',raw_sha256=actual['raw_sha256'],image_sequences=actual['image_sequences'],volume_manifest=actual['volume_manifest'],target_pebs=320,all_crc_valid=True,nand_ecc_physics_verified=False,device_readback_performed_here=False)
if __name__=='__main__':
 if len(sys.argv)!=3:raise SystemExit('Usage: verify_ubi_readback.py ORDINARY_EXPORT_FILE SEALED_ORACLE_UBI_FILE')
 try:print(json.dumps(verify(*sys.argv[1:]),ensure_ascii=False,indent=2))
 except (AssertionError,ValueError,OSError) as e:print('FAIL',repr(e));raise SystemExit(1)
