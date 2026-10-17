from pathlib import Path
import subprocess,tempfile
R=Path(__file__).resolve().parents[2]
s=(R/'u-boot-2016/fs/ubifs/ubifs.c').read_text()
a=s.index('static int read_block('); b=s.index('static int do_readpage(',a); rb=s[a:b]
k=(R/'u-boot-2016/fs/ubifs/key.h').read_text(); a=k.index('static inline void data_key_init('); b=k.index('\n}',a)+2; dk=k[a:b]
c=r"""
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef unsigned long ino_t;
#define UBIFS_BLOCK_SIZE 4096
#define UBIFS_DATA_NODE_SZ 48
#define UBIFS_MAX_DATA_NODE_SZ 4144
#define UBIFS_S_KEY_BLOCK_MASK 0x1fffffffU
#define UBIFS_S_KEY_BLOCK_BITS 29
#define UBIFS_DATA_KEY 1
#define ubifs_assert(x) ((void)(x))
#define le16_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#define le64_to_cpu(x) (x)
struct ubifs_info { int x; }; union ubifs_key { u32 u32[2]; };
struct ubifs_ch { u64 sqnum; u32 len; };
struct ubifs_data_node { struct ubifs_ch ch; u32 size; u16 compr_type; u8 data[4096]; };
struct ubifs_inode_info { u64 creat_sqnum; };
struct super_block { struct ubifs_info *s_fs_info; };
struct inode { struct super_block *i_sb; unsigned long i_ino; };
static struct ubifs_inode_info ii;
#define ubifs_inode(i) (&ii)
static unsigned want; static int lookup_rc;
static int ubifs_tnc_lookup(struct ubifs_info*c,const union ubifs_key*k,void*n) {
 (void)c;(void)n; return k->u32[0]==65 && k->u32[1]==0x20000000U+want ? lookup_rc : -999;
}
static int ubifs_decompress(const struct ubifs_info*c,const void*in,int n,void*out,int*olen,int type) {
 (void)c;if(type||n<0||n>4096)return -EINVAL;memcpy(out,in,n);*olen=n;return 0;
}
#define ubifs_err(c,fmt,...) ((void)0)
""" + dk + rb + r"""
int main(void){
 struct ubifs_info c={0};struct super_block sb={&c};struct inode in={&sb,65};
 struct ubifs_data_node dn;u8 out[4096];unsigned b;
 memset(&dn,0,sizeof(dn));ii.creat_sqnum=1;dn.ch.sqnum=2;
 dn.ch.len=4144;dn.size=4096;for(b=0;b<4096;b++)dn.data[b]=b;
 for(b=0;b<2;b++){want=b;lookup_rc=0;if(read_block(&in,out,b,&dn)||memcmp(out,dn.data,4096))return 1;}
 want=1;lookup_rc=-EINVAL;if(read_block(&in,out,1,&dn)!=-EINVAL)return 2;
 lookup_rc=0;dn.ch.len=47;if(read_block(&in,out,1,&dn)!=-EINVAL)return 3;
 puts("PASS: production read_block/data_key_init selects indexed keys 0/1 and rejects TNC/node-length failures");return 0;
}
"""
with tempfile.TemporaryDirectory() as d:
 f=Path(d)/'t.c';x=Path(d)/'t';f.write_text(c)
 subprocess.run(['cc','-std=gnu99','-Wall','-Wextra','-Werror','-Wno-unused-parameter',str(f),'-o',str(x)],check=True)
 subprocess.run([str(x)],check=True)
