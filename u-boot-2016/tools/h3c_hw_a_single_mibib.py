#!/usr/bin/env python3
"""Offline, hash-pinned HW-A MIBIB single-rootfs candidate generator; no flash I/O."""
import argparse,hashlib,json,struct,sys
from pathlib import Path
SIZE,OFF,COPY,PAGE,ERASE=0x80000,0x80000,0x20000,0x800,0x20000
COPIES=(0,0x20000); HF,TF,EF="<4I","<4I","<16sLLBBBB"; ES=struct.calcsize(EF)
HM=(0xFE569FAC,0xCD7F127A,4); TM=(0x55EE73AA,0xE35EBDDB,4)
SOURCE_SHA="0b403b917823b51560a52a8b84cc8922e774dbd4530c47989b935dadfbe2e44a"
ATTR=(255,255,0,0)
BASE=(("0:SBL1",0,0x80000),("0:MIBIB",0x80000,0x80000),("0:BOOTCONFIG",0x100000,0x40000),("0:BOOTCONFIG1",0x140000,0x40000),("0:QSEE",0x180000,0x100000),("0:QSEE_1",0x280000,0x100000),("0:DEVCFG",0x380000,0x40000),("0:DEVCFG_1",0x3c0000,0x40000),("0:CDT",0x400000,0x40000),("0:CDT_1",0x440000,0x40000),("0:APPSBLENV",0x480000,0x80000),("0:APPSBL",0x500000,0x140000),("0:APPSBL_1",0x640000,0x140000),("0:ART",0x780000,0x100000),("0:TRAINING",0x880000,0x80000),("rootfs",0x900000,0x2800000),("rootfs_1",0x3100000,0x2800000),("pdt_data",0x5900000,0x600000),("pdt_data_1",0x5f00000,0x600000),("exp",0x6500000,0x100000),("plugin",0x6600000,0x1700000))
class MibibError(ValueError): pass
def digest(b): return hashlib.sha256(b).hexdigest()
def extract(b):
    if len(b)==SIZE:return b
    if len(b) in (0x900000,0x8000000):return b[OFF:OFF+SIZE]
    raise MibibError("input size must be 512 KiB MIBIB, 9 MiB bootarea, or 128 MiB NAND dump")
def parse(b,cp):
    if len(b)!=SIZE or cp not in COPIES:raise MibibError("invalid MIBIB region/copy")
    h=struct.unpack_from(HF,b,cp)
    if h[:3]!=HM:raise MibibError("bad MIBIB header")
    at=cp+PAGE;m=struct.unpack_from(TF,b,at)
    if m[:3]!=TM or not 1<=m[3]<=32 or at+16+m[3]*ES>cp+COPY:raise MibibError("bad table header/count")
    raw=[];rows=[]
    for i in range(m[3]):
        x=at+16+i*ES;r=b[x:x+ES];n,s,z,*a=struct.unpack(EF,r)
        if b"\0" not in n:raise MibibError("unterminated name")
        try:name=n.split(b"\0",1)[0].decode("ascii")
        except UnicodeDecodeError as e:raise MibibError("non-ASCII name") from e
        if not name or any(ord(c)<32 or ord(c)>126 for c in name):raise MibibError("invalid name")
        raw.append(r);rows.append((name,s,z,tuple(a)))
    return {"h":h,"m":m,"at":at,"raw":raw,"rows":rows,"table":b[at:at+16+m[3]*ES]}
def source(b,pin=True):
    if len(b)!=SIZE:raise MibibError("source MIBIB must be 512 KiB")
    if pin and digest(b)!=SOURCE_SHA:raise MibibError("source hash does not match RC-A2-512 baseline")
    cs=[parse(b,c) for c in COPIES]
    if [c["h"][3] for c in cs]!=[0,1] or cs[0]["table"]!=cs[1]["table"]:raise MibibError("source copies/ages mismatch")
    if len(cs[0]["rows"])!=len(BASE):raise MibibError("source row count mismatch")
    for i,((name,off,size),r) in enumerate(zip(BASE,cs[0]["rows"])):
        n,s,z,a=r
        if (n,s*ERASE,z*ERASE,a)!=(name,off,size,ATTR):raise MibibError("wrong OEM extent/attribute at row %d"%i)
    return cs
def table_for(c):
    rr=c["raw"];n,s,z,*a=struct.unpack(EF,rr[15])
    if n.split(b"\0",1)[0]!=b"rootfs" or s!=0x48 or z!=0x140:raise MibibError("unexpected rootfs source extent")
    rows=rr[:15]+[struct.pack(EF,n,s,0x280,*a)]+rr[17:]
    return struct.pack(TF,*TM,len(rows))+b"".join(rows)
def make(b,pin=True):
    cs=source(b,pin);o=bytearray(b)
    for c in cs:
        t=table_for(c);a=c["at"];o[a:a+len(t)]=t
        o[a+16+20*ES:a+16+21*ES]=bytes(ES)
    return bytes(o)
def validate(src,out,pin=True):
    cs=source(src,pin)
    if len(out)!=SIZE:raise MibibError("candidate must be 512 KiB")
    exp=bytearray(src)
    for c in cs:
        t=table_for(c);a=c["at"];exp[a:a+len(t)]=t;exp[a+16+20*ES:a+16+21*ES]=bytes(ES)
    if out!=bytes(exp):raise MibibError("candidate changes bytes outside approved table transformation")
    rs=[parse(out,c) for c in COPIES]
    if [r["h"] for r in rs]!=[r["h"] for r in cs] or rs[0]["table"]!=rs[1]["table"]:raise MibibError("changed header or inconsistent copies")
    want=BASE[:15]+(("rootfs",0x900000,0x5000000),)+BASE[17:]
    if len(rs[0]["rows"])!=20:raise MibibError("expected 20 table entries")
    for i,((name,off,size),r) in enumerate(zip(want,rs[0]["rows"])):
        n,s,z,a=r
        if (n,s*ERASE,z*ERASE,a)!=(name,off,size,ATTR):raise MibibError("wrong single-layout row %d"%i)
    if any(r[0]=="rootfs_1" for r in rs[0]["rows"]):raise MibibError("rootfs_1 remains")
    return {"status":"valid","source_mibib_sha256":digest(src),"candidate_sha256":digest(out),"copies":2,"entries":20,"mode":"single","rootfs_offset":"0x00900000","rootfs_size":"0x05000000","rootfs_1":"removed","ages_preserved":[0,1],"persistent_write_enabled":False,"stored_crc_field":False}
def readsrc(p):
    b=Path(p).read_bytes()
    return b if len(b)==SIZE else extract(b)
def main():
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest="cmd",required=True)
    g=s.add_parser("generate");g.add_argument("--input",required=True);g.add_argument("--output",required=True)
    v=s.add_parser("validate");v.add_argument("--source",required=True);v.add_argument("--candidate",required=True)
    a=p.parse_args()
    try:
        if a.cmd=="generate":
            src=readsrc(a.input);out=make(src);dst=Path(a.output)
            if dst.resolve()==Path(a.input).resolve():raise MibibError("output must be separate from source")
            dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(out)
        else:src=readsrc(a.source);out=Path(a.candidate).read_bytes()
        print(json.dumps(validate(src,out),sort_keys=True,indent=2));return 0
    except (OSError,MibibError) as e:print("error: %s"%e,file=sys.stderr);return 2
if __name__=="__main__":sys.exit(main())
