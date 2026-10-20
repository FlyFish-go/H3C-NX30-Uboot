"""Synthetic only: no official packages, device paths or private backups."""
import ctypes as C
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
PEB, LEB, N = 131072, 126976, 320
U8 = C.c_uint8 * 32
class Geometry(C.Structure):
    _fields_ = [(x, C.c_uint64) for x in ('physical_offset', 'size_bytes')] + [(x, C.c_uint32) for x in ('erase_bytes', 'page_bytes', 'oob_bytes', 'ecc_bits')]
class Volume(C.Structure):
    _fields_ = [('size_bytes', C.c_uint64), ('sha256', U8), ('type', C.c_uint32)]
class Manifest(C.Structure):
    _fields_ = [('geometry', Geometry), ('candidate_sha256', U8), ('volume_table_sha256', U8), ('immutable', Volume * 4)]
class Result(C.Structure):
    _fields_ = [('code', C.c_int), ('failure_peb', C.c_uint32), ('scanned_pebs', C.c_uint32), ('rootfs_data_lebs', C.c_uint32), ('logical_content_verified', C.c_int), ('snapshot_sha256', U8)]
class Measurement(C.Structure):
    _fields_ = [('geometry', Geometry), ('volume_table_sha256', U8), ('snapshot_sha256', U8), ('immutable', Volume * 4), ('scanned_pebs', C.c_uint32), ('rootfs_data_lebs', C.c_uint32), ('normalized_volume_table_sha256', U8), ('normalized_volume_table_valid', C.c_uint32)]
READ = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint64, C.c_void_p, C.c_size_t)
def digest(b): return U8.from_buffer_copy(hashlib.sha256(b).digest())
def crc(b): return zlib.crc32(b) ^ 0xffffffff
def fixcrc(b, start=0): struct.pack_into('>I', b, start + 60, crc(b[start:start+60]))
def record(name, typ, reserved):
    b = bytearray(172)
    if name:
        struct.pack_into('>III', b, 0, reserved, 1, 0)
        b[12] = typ
        struct.pack_into('>H', b, 14, len(name))
        b[16:16+len(name)] = name.encode()
    struct.pack_into('>I', b, 168, crc(b[:168]))
    return b

def block(vol, ln, data, typ=1, num=0):
    b = bytearray(b'\xff' * PEB)
    b[:64] = bytes(64)
    b[:5] = b'UBI#\x01'
    struct.pack_into('>III', b, 16, 2048, 4096, 7)
    fixcrc(b)
    v = bytearray(64)
    v[:5] = b'UBI!\x01'
    v[5] = typ
    v[7] = 5 if vol == 0x7fffefff else 0
    struct.pack_into('>II', v, 8, vol, ln)
    if typ == 2:
        struct.pack_into('>II', v, 20, len(data), num)
        struct.pack_into('>I', v, 32, crc(data))
    fixcrc(v)
    b[2048:2112] = v
    b[4096:4096+len(data)] = data
    return b

def mark_observed_copy(b, size, compat):
    v = memoryview(b)[2048:2112]
    v[6] = 1
    v[7] = compat
    struct.pack_into('>III', v, 20, size, 0, 0)
    struct.pack_into('>I', v, 32, crc(bytes(b[4096:4096+size])))
    fixcrc(b, 2048)

def fixture(oob=64, ecc=4):
    table = b''.join(record(name, typ, 8) for name,typ in [('kernel',2),('wifi_fw',2),('bt_fw',2),('ubi_rootfs',1),('rootfs_data',1)])
    table += bytes(record('', 0, 0)) * 123
    blocks = {0:block(0x7fffefff,0,table),1:block(0x7fffefff,1,table)}
    g = Geometry(0x900000, N*PEB, PEB, 2048, oob, ecc)
    m = Manifest(); m.geometry = g; m.candidate_sha256 = digest(b'synthetic candidate'); m.volume_table_sha256 = digest(table)
    for i in range(4):
        typ = 1 if i == 3 else 2
        data = bytes([i+31]) * (LEB if typ == 1 else 99+i)
        blocks[i+2] = block(i,0,data,typ,1 if typ == 2 else 0)
        m.immutable[i] = Volume(len(data), digest(data), typ)
    blocks[6] = block(4,4,b'arbitrary writable filesystem')
    return g,m,blocks

class OemSlotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='oem-slot-synthetic-')
        so = Path(cls.tmp.name)/'slot.so'
        subprocess.run(['cc','-std=c99','-O2','-Wall','-Wextra','-Werror','-pedantic','-shared','-fPIC',str(ROOT/'src/bridge_oem_slot.c'),'-o',str(so)],check=True)
        cls.lib = C.CDLL(str(so))
        cls.lib.bos_verify.argtypes = [READ,C.c_void_p,C.POINTER(Geometry),C.POINTER(Manifest),C.POINTER(C.c_uint8),C.POINTER(Result)]
        cls.lib.bos_verify.restype = C.c_int
        cls.lib.bos_measure.argtypes = [READ,C.c_void_p,C.POINTER(Geometry),C.POINTER(Measurement)]
        cls.lib.bos_measure.restype = C.c_int
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def reader(self, g, blocks, io=False, change=False):
        reads = {}; erased = bytes(b'\xff'*PEB)
        @READ
        def read(_, off, dst, size):
            self.assertEqual(size, PEB)
            self.assertLessEqual(off+size, g.size_bytes)
            self.assertEqual(off % PEB, 0)
            p = off//PEB
            reads[p] = reads.get(p,0)+1
            if io and p==3: return -1
            b = bytes(blocks.get(p, erased))
            if change and p==2 and reads[p]>1: b=b[:4096]+bytes([b[4096]^1])+b[4097:]
            C.memmove(dst,b,size)
            return 0
        return read
    def verify(self, g,m,blocks, candidate=None, io=False, change=False):
        reads = {}; erased = bytes(b'\xff'*PEB)
        @READ
        def read(_, off, dst, size):
            self.assertEqual(size, PEB)
            self.assertLessEqual(off+size, g.size_bytes)
            self.assertEqual(off % PEB, 0)
            p = off//PEB
            reads[p] = reads.get(p,0)+1
            if io and p==3: return -1
            b = bytes(blocks.get(p, erased))
            if change and p==2 and reads[p]>1: b=b[:4096]+bytes([b[4096]^1])+b[4097:]
            C.memmove(dst,b,size)
            return 0
        r = Result()
        code = self.lib.bos_verify(read,None,C.byref(g),C.byref(m),candidate or m.candidate_sha256,C.byref(r))
        self.assertEqual(code,r.code)
        self.assertEqual(r.logical_content_verified, code==0)
        return r
    def measure(self,g,blocks):
        read=self.reader(g,blocks);m=Measurement()
        code=self.lib.bos_measure(read,None,C.byref(g),C.byref(m))
        return code,m
    def test_full_scan_static_and_dynamic_sparse(self):
        g,m,b = fixture(); r=self.verify(g,m,b)
        self.assertEqual(r.code,0); self.assertEqual(r.scanned_pebs,320); self.assertEqual(r.rootfs_data_lebs,1)
    def test_empty_writable_volume_is_valid(self):
        g,m,b=fixture(); del b[6]; self.assertEqual(self.verify(g,m,b).code,0)
    def test_changed_writable_data_is_valid(self):
        g,m,b=fixture(); b[6][4200] ^= 0x3f; self.assertEqual(self.verify(g,m,b).code,0)
    def test_missing_immutable_volume(self):
        g,m,b=fixture(); del b[2]; self.assertEqual(self.verify(g,m,b).code,8)
    def test_duplicate_leb(self):
        g,m,b=fixture(); b[9]=b[2][:]; self.assertEqual(self.verify(g,m,b).code,8)
    def test_static_crc(self):
        g,m,b=fixture(); b[2][4100]^=1; self.assertEqual(self.verify(g,m,b).code,9)
    def test_dynamic_immutable_digest(self):
        g,m,b=fixture(); b[5][4100]^=1; self.assertEqual(self.verify(g,m,b).code,10)
    def test_table_copies_and_missing(self):
        for mode in ('mismatch','missing'):
            g,m,b=fixture()
            if mode=='mismatch': b[1][4096]^=1
            else: del b[1]
            self.assertEqual(self.verify(g,m,b).code,7)
    def test_table_crc_even_if_manifest_hash_matches(self):
        g,m,b=fixture()
        for p in (0,1): b[p][4096+168]^=1
        m.volume_table_sha256 = digest(b[0][4096:4096+128*172])
        self.assertEqual(self.verify(g,m,b).code,7)
    def test_binding_range_geometry_and_unknown(self):
        g,m,b=fixture(); self.assertEqual(self.verify(g,m,b,digest(b'wrong candidate')).code,2)
        g.physical_offset=0x640000; self.assertEqual(self.verify(g,m,b).code,3)
        g,m,b=fixture(); g.ecc_bits=8; self.assertEqual(self.verify(g,m,b).code,3)
        for oob,ecc in ((80,8),(128,8)):
            g,m,b=fixture(oob,ecc)
            self.assertEqual(self.verify(g,m,b).code,0)
            self.assertEqual(self.measure(g,b)[0],0)
        for oob,ecc in ((64,8),(80,4)):
            g,m,b=fixture(oob,ecc)
            self.assertNotEqual(self.measure(g,b)[0],0)
        g,m,b=fixture(); b[8]=block(5,0,b'unknown'); self.assertEqual(self.verify(g,m,b).code,8)
    def test_ec_vid_io_and_snapshot_change(self):
        for offset,expected in [(60,5),(2048+60,6)]:
            g,m,b=fixture(); b[2][offset]^=1; self.assertEqual(self.verify(g,m,b).code,expected)
        g,m,b=fixture(); self.assertEqual(self.verify(g,m,b,io=True).code,4)
        self.assertEqual(self.verify(g,m,b,change=True).code,11)
    def test_immutable_gaps_and_static_used_ebs(self):
        for mode in ('gap','used'):
            g,m,b=fixture(); v=b[2]
            struct.pack_into('>I',v,2048+(12 if mode=='gap' else 24),2)
            fixcrc(v,2048)
            self.assertEqual(self.verify(g,m,b).code,9 if mode=='gap' else 8)
    def test_unsupported_copy_and_sequence(self):
        g,m,b=fixture(); b[2][2048+6]=1; fixcrc(b[2],2048); self.assertNotEqual(self.verify(g,m,b).code,0)
        g,m,b=fixture(); struct.pack_into('>I',b[2],24,8); fixcrc(b[2]); self.assertEqual(self.verify(g,m,b).code,5)

    def test_exact_observed_layout_and_rootfs_data_copies(self):
        g,m,b=fixture()
        for p in (0,1): mark_observed_copy(b[p],22528,5)
        b[6]=block(4,3,b'R'*2048)
        b[6][4096+2048]=0x23  # tail may retain bytes from the prior PEB contents
        mark_observed_copy(b[6],1024,0)
        self.assertEqual(self.verify(g,m,b).code,0)
        code,measured=self.measure(g,b)
        self.assertEqual(code,0)
        self.assertEqual(measured.rootfs_data_lebs,1)

    def test_observed_copy_crc_size_duplicate_and_scope_rejections(self):
        g,m,b=fixture()
        for p in (0,1): mark_observed_copy(b[p],22528,5)
        b[6]=block(4,3,b'R'*2048);mark_observed_copy(b[6],2048,0)
        b[6][4096+17]^=1
        fixcrc(b[6],2048)  # header valid, payload CRC intentionally stale
        self.assertNotEqual(self.verify(g,m,b).code,0)
        self.assertNotEqual(self.measure(g,b)[0],0)

        g,m,b=fixture()
        for p in (0,1): mark_observed_copy(b[p],22528,5)
        b[6]=block(4,3,b'R'*2048);mark_observed_copy(b[6],2048,0)
        b[7]=b[6][:]
        self.assertNotEqual(self.verify(g,m,b).code,0)
        self.assertNotEqual(self.measure(g,b)[0],0)

        g,m,b=fixture()
        for p in (0,1): mark_observed_copy(b[p],22528,5)
        b[6]=block(4,3,b'R'*2048);mark_observed_copy(b[6],2048,0)
        b[2][2048+6]=1;fixcrc(b[2],2048)
        self.assertNotEqual(self.verify(g,m,b).code,0)
        self.assertNotEqual(self.measure(g,b)[0],0)

        for bad_size in (0, LEB+1):
            g,m,b=fixture()
            for p in (0,1): mark_observed_copy(b[p],22528,5)
            b[6]=block(4,3,b'R'*2048);mark_observed_copy(b[6],bad_size,0)
            self.assertNotEqual(self.verify(g,m,b).code,0)
            self.assertNotEqual(self.measure(g,b)[0],0)

        for bad_field,value in (('used_ebs',1),('compat',1)):
            g,m,b=fixture()
            for p in (0,1): mark_observed_copy(b[p],22528,5)
            b[6]=block(4,3,b'R'*2048);mark_observed_copy(b[6],2048,0)
            if bad_field=='used_ebs': struct.pack_into('>I',b[6],2048+24,value)
            else: b[6][2048+7]=value
            fixcrc(b[6],2048)
            self.assertNotEqual(self.verify(g,m,b).code,0)
            self.assertNotEqual(self.measure(g,b)[0],0)

        # The unique copied LEB must remain inside rootfs_data's reservation.
        g,m,b=fixture()
        for p in (0,1): mark_observed_copy(b[p],22528,5)
        b[6]=block(4,8,b'R'*2048);mark_observed_copy(b[6],2048,0)
        self.assertNotEqual(self.verify(g,m,b).code,0)
        self.assertNotEqual(self.measure(g,b)[0],0)

    def test_layout_copy_requires_ff_padding(self):
        g,m,b=fixture()
        for p in (0,1): mark_observed_copy(b[p],22528,5)
        b[0][4096+128*172+7]=0
        struct.pack_into('>I',b[0],2048+32,crc(bytes(b[0][4096:4096+22528])))
        fixcrc(b[0],2048)
        self.assertNotEqual(self.verify(g,m,b).code,0)
        self.assertNotEqual(self.measure(g,b)[0],0)

if __name__ == '__main__': unittest.main()
