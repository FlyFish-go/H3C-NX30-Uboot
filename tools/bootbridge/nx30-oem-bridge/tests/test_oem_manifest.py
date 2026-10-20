"""SHA-pinned manifest generation, C interoperability, and UBSan execution.

Run: python3 tests/test_oem_manifest.py
This executes the original 13 C verifier cases under UBSan (no recovery), in
addition to generator tests. ASan ctypes loading needs libasan preloaded before
Python starts; UBSan needs no preload. No ASan validation is claimed here.
"""
import ctypes as C
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import test_oem_slot as slot
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import build_oem_manifest as gen

def build_library(path, extra=()):
    subprocess.run(['cc','-std=c99','-O1','-g','-Wall','-Wextra','-Werror','-pedantic',*extra,'-shared','-fPIC',str(ROOT/'src/bridge_oem_slot.c'),'-o',str(path)],check=True)
    lib=C.CDLL(str(path));lib.bos_verify.argtypes=[slot.READ,C.c_void_p,C.POINTER(slot.Geometry),C.POINTER(slot.Manifest),C.POINTER(C.c_uint8),C.POINTER(slot.Result)];lib.bos_verify.restype=C.c_int
    return lib

class SanitizedOemSlotTests(slot.OemSlotTests):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='oem-slot-ubsan-')
        cls.lib=build_library(Path(cls.tmp.name)/'slot.so',('-fsanitize=undefined','-fno-sanitize-recover=undefined'))

class ManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='oem-manifest-synthetic-')
        cls.lib=build_library(Path(cls.tmp.name)/'slot.so')
        cls.verify=slot.OemSlotTests.verify
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def setUp(self):
        self.case=tempfile.TemporaryDirectory(dir=self.tmp.name)
        self.addCleanup(self.case.cleanup)
        self.path=Path(self.case.name)/'synthetic.ubi'
        self.g,self.original,self.blocks=slot.fixture()
        # Original package permits an empty writable volume. Sparse runtime
        # writable metadata belongs to verifier tests, not the official parser.
        del self.blocks[6]
        self.write()
        self.geo={k:getattr(self.g,k) for k in gen.GEOMETRY_KEYS}
    def write(self):
        with self.path.open('wb') as f:
            erased=b'\xff'*slot.PEB
            for p in range(slot.N):f.write(self.blocks.get(p,erased))
        self.sha=hashlib.sha256(self.path.read_bytes()).hexdigest()
    def manifest(self, **kw):
        args=dict(expected_ubi_sha256=self.sha,candidate_sha256=bytes(self.original.candidate_sha256).hex(),observed_geometry=self.geo)
        args.update(kw)
        return gen.build_manifest(self.path,**args)
    def as_c(self,m):
        c=slot.Manifest();c.geometry=slot.Geometry(**m['geometry'])
        c.candidate_sha256=slot.U8.from_buffer_copy(bytes.fromhex(m['candidate_sha256']))
        c.volume_table_sha256=slot.U8.from_buffer_copy(bytes.fromhex(m['volume_table_sha256']))
        for i,v in enumerate(m['immutable']):c.immutable[i]=slot.Volume(v['size_bytes'],slot.U8.from_buffer_copy(bytes.fromhex(v['sha256'])),v['type'])
        return c
    def test_generated_manifest_passes_actual_c_and_changed_content_fails(self):
        m=self.manifest();self.assertEqual(self.verify(self.g,self.as_c(m),self.blocks).code,0)
        self.blocks[5][4200]^=1
        self.assertEqual(self.verify(self.g,self.as_c(m),self.blocks).code,10)
    def test_compiled_initializer(self):
        m=self.manifest();d=Path(self.case.name)
        (d/'rule.h').write_text(gen.c_header(m))
        (d/'test.c').write_text('#include "rule.h"\nint main(void){return bootbridge_oem_manifest.geometry.size_bytes != 41943040u;}\n')
        exe=d/'test'
        subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-pedantic','-I',str(ROOT/'src'),str(d/'test.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    def test_exact_geometry_set_compiles_and_rejects_ambiguity_or_content_changes(self):
        base=self.manifest();records=[]
        for offset in (0x900000,0x3100000):
            for oob,ecc in ((64,4),(128,4),(128,8)):
                record=copy.deepcopy(base)
                record['geometry'].update(physical_offset=offset,oob_bytes=oob,ecc_bits=ecc)
                records.append(record)
        d=Path(self.case.name);(d/'set.h').write_text(gen.c_header_set(records))
        (d/'set.c').write_text('#include "set.h"\nint main(void){return BI_OEM_MANIFEST_COUNT != 6 || BI_OEM_MANIFESTS[5].geometry.ecc_bits != 8;}\n')
        subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-pedantic','-I',str(ROOT/'src'),str(d/'set.c'),'-o',str(d/'set')],check=True)
        subprocess.run([str(d/'set')],check=True)
        wrong=copy.deepcopy(records);wrong[-1]['candidate_sha256']='1'*64
        other=copy.deepcopy(records);other[-1]['immutable'][0]['sha256']='2'*64
        for invalid in ([],records+[records[0]],wrong,other,[base]*9,None):
            with self.subTest(invalid=type(invalid).__name__),self.assertRaises(ValueError):gen.c_header_set(invalid)

    def test_pool_header_actual_c_and_provenance_revision(self):
        m=self.manifest();e=dict(manifest=m,source_package_sha256='a'*64,
            source_review_id='explicit-synthetic-rule-v1',content_group=1,exit_policy=1,
            product=1,alias=10012,table_policy=0)
        d=Path(self.case.name);header=gen.c_pool_header([e]);(d/'pool.h').write_text(header)
        # Compile and actually measure the synthetic UBI using generated C rules.
        source='''#include "pool.h"
#include <stdio.h>
static int rd(void*c,uint64_t off,void*b,size_t n){FILE*f=c;return fseek(f,(long)off,SEEK_SET)||fread(b,1,n,f)!=n;}
int main(int argc,char**argv){bos_measurement m;bos_pool_match r;FILE*f;int rc;if(argc!=2)return 3;f=fopen(argv[1],"rb");if(!f)return 4;rc=bos_measure(rd,f,&BI_OEM_POOL[0].content.geometry,&m);fclose(f);if(rc)return 5;return bos_pool_select(&m,BI_OEM_POOL,BI_OEM_POOL_COUNT,BI_OEM_POOL[0].content.candidate_sha256,BI_OEM_POOL_REVISION,&r)!=BOS_OK||!r.logical_content_verified;}
'''
        (d/'pool.c').write_text(source)
        subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-pedantic','-I'+str(ROOT/'src'),str(d/'pool.c'),str(ROOT/'src/bridge_oem_slot.c'),'-o',str(d/'pool')],check=True)
        subprocess.run([str(d/'pool'),str(self.path)],check=True)
        changed=copy.deepcopy(e);changed['source_package_sha256']='b'*64
        self.assertNotEqual(header,gen.c_pool_header([changed]))
        changed=copy.deepcopy(e);changed['source_review_id']='different-reviewed-rule'
        self.assertNotEqual(header,gen.c_pool_header([changed]))
        unavailable=copy.deepcopy(e);unavailable['exit_policy']=0
        self.assertIn('bootbridge_oem_pool',gen.c_pool_header([unavailable]))
        # Source-bound dynamic pools use policy 2; 3 remains outside the ABI.
        for key,value in [('exit_policy',True),('exit_policy',-1),('alias',0),('product',True),('source_package_sha256',''),('source_review_id','unsafe"code'),('table_policy',3)]:
            bad=copy.deepcopy(e);bad[key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):gen.c_pool_header([bad])
        for key in ('content_group','exit_policy','product'):
            conflict=copy.deepcopy(e);conflict['alias']=10013;conflict[key]+=1
            with self.subTest(conflict=key),self.assertRaises(ValueError):gen.c_pool_header([e,conflict])
        with self.assertRaises(ValueError):gen.c_pool_header([e,e])
        bad=copy.deepcopy(e);bad['manifest']['candidate_sha256']='c'*64;bad['alias']+=1
        with self.assertRaises(ValueError):gen.c_pool_header([e,bad])
        bad=copy.deepcopy(e);bad['extra']=1
        with self.assertRaises(ValueError):gen.c_pool_header([bad])

    def test_bad_sha_empty_binding_and_exact_geometry(self):
        for kw in [dict(expected_ubi_sha256='1'*64),dict(expected_ubi_sha256=''),dict(candidate_sha256='0'*64),dict(observed_geometry=dict(self.geo,unknown=1)),dict(observed_geometry=dict(self.geo,size_bytes=1)),dict(observed_geometry=dict(self.geo,ecc_bits=True))]:
            with self.subTest(kw=kw),self.assertRaises(ValueError):self.manifest(**kw)
    def test_duplicate_json_keys_and_unknown_manifest_fields(self):
        with self.assertRaises(ValueError):gen.strict_json('{"size_bytes":1,"size_bytes":2}')
        m=self.manifest();m['bootable']=True
        with self.assertRaises(ValueError):gen.c_header(m)
    def test_crc_duplicate_missing_volume_and_invalid_padding(self):
        for mode in ('crc','duplicate','missing','padding'):
            with self.subTest(mode=mode):
                self.g,self.original,self.blocks=slot.fixture();del self.blocks[6]
                if mode=='crc':self.blocks[2][4100]^=1
                elif mode=='duplicate':self.blocks[8]=self.blocks[2][:]
                elif mode=='missing':del self.blocks[2]
                else:self.blocks[2][2048+48]=1;slot.fixcrc(self.blocks[2],2048)
                self.write()
                with self.assertRaises(ValueError):self.manifest()
    def test_symlink_nonregular_and_wrong_size(self):
        link=Path(self.case.name)/'link';link.symlink_to(self.path)
        for path in (link,Path(self.case.name)):
            with self.assertRaises(ValueError):gen.build_manifest(path,expected_ubi_sha256=self.sha,candidate_sha256='1'*64,observed_geometry=self.geo)
        self.path.write_bytes(b'short')
        with self.assertRaises(ValueError):self.manifest()
    def test_cli_and_optimization_rejection(self):
        out=Path(self.case.name)/'rule.h'
        args=[str(ROOT/'tools/build_oem_manifest.py'),'--ubi',str(self.path),'--ubi-sha256',self.sha,'--candidate-sha256','1'*64,'--geometry-json',json.dumps(self.geo),'--output',str(out)]
        subprocess.run([sys.executable,*args],check=True,capture_output=True)
        self.assertTrue(out.read_text().startswith('/* Internal build rule'))
        for flags,env in [(['-O'],None),([],dict(os.environ,PYTHONOPTIMIZE='0'))]:
            p=subprocess.run([sys.executable,*flags,*args],env=env,capture_output=True,text=True)
            self.assertNotEqual(p.returncode,0);self.assertIn('optimized Python',p.stderr)
        p=subprocess.run([sys.executable,*args],capture_output=True,text=True)
        self.assertNotEqual(p.returncode,0)  # no overwrite

if __name__=='__main__':unittest.main()
