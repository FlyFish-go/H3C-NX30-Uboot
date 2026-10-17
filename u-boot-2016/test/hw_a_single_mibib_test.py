#!/usr/bin/env python3
import importlib.util, pathlib, struct, unittest
TOOL=pathlib.Path(__file__).resolve().parents[1]/"tools"/"h3c_hw_a_single_mibib.py"
spec=importlib.util.spec_from_file_location("mibib",TOOL); m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
def fixture():
    b=bytearray(b"\xff"*m.SIZE); rows=[]
    for n,o,z in m.BASE: rows.append(struct.pack(m.EF,n.encode().ljust(16,b"\0"),o//m.ERASE,z//m.ERASE,*m.ATTR))
    table=struct.pack(m.TF,*m.TM,len(rows))+b"".join(rows)
    for cp,age in zip(m.COPIES,(0,1)):
        struct.pack_into(m.HF,b,cp,*m.HM,age); at=cp+m.PAGE; b[at:at+len(table)]=table
    return bytes(b)
class Tests(unittest.TestCase):
    def test_merge_and_preserve(self):
        src=fixture(); out=m.make(src,False); report=m.validate(src,out,False)
        self.assertEqual((report["mode"],report["entries"],report["ages_preserved"]),("single",20,[0,1]))
        for cp in m.COPIES:
            p=m.parse(out,cp); self.assertEqual(p["m"][3],20)
            self.assertEqual(p["rows"][15][1:3],(0x48,0x280))
            self.assertFalse(any(r[0]=="rootfs_1" for r in p["rows"]))
            self.assertEqual(p["rows"][16][0],"pdt_data")
            self.assertEqual(p["rows"][19][0],"plugin")
    def test_reject_unapproved_byte_change(self):
        src=fixture(); out=bytearray(m.make(src,False)); out[0x100]^=1
        with self.assertRaises(m.MibibError): m.validate(src,bytes(out),False)
    def test_reject_changed_protected_extent(self):
        src=bytearray(fixture()); cp=m.COPIES[0]; at=cp+m.PAGE+16+3*m.ES
        n,s,z,*a=struct.unpack_from(m.EF,src,at); struct.pack_into(m.EF,src,at,n,s+1,z,*a)
        with self.assertRaises(m.MibibError): m.make(bytes(src),False)
    def test_source_hash_pin(self):
        with self.assertRaises(m.MibibError): m.make(fixture())
    def test_only_known_image_sizes(self):
        for z in (m.SIZE,0x900000,0x8000000): self.assertEqual(len(m.extract(bytes(z))),m.SIZE)
        with self.assertRaises(m.MibibError): m.extract(bytes(m.SIZE+1))
if __name__=="__main__": unittest.main()
