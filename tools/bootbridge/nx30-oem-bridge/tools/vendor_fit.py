#!/usr/bin/env python3
"""Extract exact FIT kernel subimages and embedded IKCONFIG from staged NX30 artifacts."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,lzma,os,struct,zlib
B,E_N,P,N,E=1,2,3,4,9

def sha(b): return hashlib.sha256(b).hexdigest()
def fdt_props(blob):
    magic,total,so,stro,_,_,_,_,ss,sz=struct.unpack_from('>10I',blob,0)
    if magic!=0xd00dfeed or total!=len(blob): raise ValueError('invalid FDT/FIT header')
    st=blob[so:so+sz]; strings=blob[stro:stro+ss]; pos=0; stack=[]; nodes={}
    while pos<len(st):
        tok=struct.unpack_from('>I',st,pos)[0]; pos+=4
        if tok==B:
            end=st.index(0,pos); stack.append(st[pos:end].decode('ascii','replace')); pos=(end+4)&~3
            nodes['/'+('/'.join(x for x in stack if x))]={}
        elif tok==E_N: stack.pop()
        elif tok==P:
            n,o=struct.unpack_from('>II',st,pos); pos+=8; value=st[pos:pos+n]; pos=(pos+n+3)&~3
            end=strings.index(0,o); key=strings[o:end].decode('ascii','replace')
            nodes['/'+('/'.join(x for x in stack if x))][key]=value
        elif tok==N: continue
        elif tok==E: break
        else: raise ValueError(f'unknown FDT token {tok}')
    return nodes

def parse_fit(blob):
    nodes=fdt_props(blob); kernel=nodes['/images/kernel@1']
    comp=kernel['compression'].rstrip(b'\0').decode(); data=kernel['data']
    if comp=='lzma': image=lzma.decompress(data,format=lzma.FORMAT_ALONE)
    elif comp=='none': image=data
    else: raise ValueError(f'unsupported FIT compression: {comp}')
    selected=nodes['/configurations/config@mp02.1']
    fdt_name=selected['fdt'].rstrip(b'\0').decode()
    cfg=None; cfg_info=None; marker=b'IKCFG_ST'; start=image.find(marker)
    if start>=0:
        dec=zlib.decompressobj(16+zlib.MAX_WBITS)
        cfg=dec.decompress(image[start+len(marker):])+dec.flush()
        if not dec.eof or not dec.unused_data.startswith(b'IKCFG_ED'):
            raise ValueError('embedded IKCONFIG stream/end marker invalid')
        cfg_info={'marker_offset':start,'config_bytes':len(cfg),'config_sha256':sha(cfg),'gzip_eof':dec.eof,'trailing_bytes':len(dec.unused_data)}
    props={}
    for k,v in kernel.items():
        if k=='data': continue
        props[k]=hex(struct.unpack('>I',v)[0]) if len(v)==4 else (v.rstrip(b'\0').decode('ascii','replace') if v.endswith(b'\0') else v.hex())
    dt=nodes['/images/'+fdt_name]['data']
    return image,cfg,{'kernel_properties':props,'fit_image_bytes':len(blob),'fit_image_sha256':sha(blob),'kernel_data_bytes':len(data),'kernel_data_sha256':sha(data),'uncompressed_image_bytes':len(image),'uncompressed_image_sha256':sha(image),'selected_config':'config@mp02.1','selected_fdt':fdt_name,'selected_fdt_bytes':len(dt),'selected_fdt_sha256':sha(dt),'ikconfig':cfg_info}

def relevant_config(cfg):
    out={}
    prefixes=('CONFIG_MTD','CONFIG_NAND','CONFIG_QCOM','CONFIG_QPIC','CONFIG_SPI_NAND','CONFIG_ECC','CONFIG_UBI','CONFIG_UBIFS','CONFIG_SQUASHFS','CONFIG_JFFS2','CONFIG_CMDLINE','CONFIG_ROOTFS','CONFIG_PAGE_OFFSET','CONFIG_PHYS_OFFSET','CONFIG_VMSPLIT','CONFIG_KALLSYMS')
    for line in cfg.decode('utf-8','replace').splitlines():
        if line.startswith('# CONFIG_') and line.endswith(' is not set'):
            key=line.split()[1]
            if key.startswith(prefixes): out[key]='n'
        elif '=' in line and line.startswith('CONFIG_'):
            key,value=line.split('=',1)
            if key.startswith(prefixes): out[key]=value
    return out

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--r004',type=Path,required=True,help='ordinary R004 recovery kernel FIT')
    ap.add_argument('--r007-ubi',type=Path,required=True,help='ordinary R007 UBI image')
    ap.add_argument('--out',type=Path,required=True,help='output directory')
    a=ap.parse_args()
    from vendor_ubi import parse as parse_ubi
    _,streams,_,_=parse_ubi(a.r007_ubi)
    fits={'R004':a.r004.read_bytes(),'R007':streams['kernel']}; report={'extraction':'PASS','images':{},'relevant_config_differences':{}}
    configs={}
    for name,fit in fits.items():
        image,cfg,meta=parse_fit(fit); a.out.mkdir(parents=True,exist_ok=True,mode=0o700)
        ip=a.out/f'{name}-kernel.image'; ip.write_bytes(image); os.chmod(ip,0o600)
        fp=a.out/f'{name}-kernel.fit'; fp.write_bytes(fit); os.chmod(fp,0o600)
        configs[name]=relevant_config(cfg) if cfg else {}
        meta['image_path']=str(ip); meta['fit_path']=str(fp); meta['relevant_config']=configs[name]
        report['images'][name]=meta
    keys=sorted(set(configs['R004'])|set(configs['R007']))
    report['relevant_config_differences']={k:{n:configs[n].get(k,'<unset>') for n in ('R004','R007')} for k in keys if configs['R004'].get(k)!=configs['R007'].get(k)}
    op=a.out/'kernel-extraction.json'; op.write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n'); os.chmod(op,0o600)
    print(json.dumps({'report':str(op),'images':{n:{k:v for k,v in m.items() if k!='relevant_config'} for n,m in report['images'].items()},'relevant_config_differences':report['relevant_config_differences']},indent=2,ensure_ascii=False))
if __name__=='__main__': main()
