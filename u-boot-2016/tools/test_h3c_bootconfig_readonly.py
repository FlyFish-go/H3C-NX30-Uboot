#!/usr/bin/env python3
from pathlib import Path
import subprocess,hashlib,json
import argparse
parser=argparse.ArgumentParser(description='Compile actual release BC getter/adapter with mock read transport')
parser.add_argument('--output-dir',required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2];ev=Path(args.output_dir).resolve();ev.mkdir(parents=True,exist_ok=True)
s=(root/'u-boot-2016/lib/bootconfig.c').read_text()
a=s.index('#if defined(CONFIG_TARGET_IPQ5018_H3C_HW_D) && defined(CONFIG_H3C_RELEASE_SYSTEM_DEFAULT)');b=s.index('\n#endif',a)+len('\n#endif')
adapter=s[a:b].replace('#include <nand.h>','')
a=s.index('int get_bootconfig(const char *part_name)');b=s.index('\n}\n',a)+2
getter=s[a:b]
pre=r'''
#define CONFIG_TARGET_IPQ5018_H3C_HW_D 1
#define CONFIG_H3C_RELEASE_SYSTEM_DEFAULT 1
#define CONFIG_NAND_FLASH_INFO_IDX 0
#define BOOTCONFIG_PART_NAME "0:BOOTCONFIG"
#define BOOTCONFIG_BACKUP_PART_NAME "0:BOOTCONFIG1"
#define SMEM_BOOT_NAND_FLASH 2
#define SMEM_BOOT_QSPI_NAND_FLASH 11
#include <stdint.h>
#include <errno.h>
#define main core_main
#include "CORE_TEST_PATH"
#undef main
typedef struct { unsigned long long size; unsigned int writesize,erasesize; } nand_info_t;
nand_info_t nand_info[1];
struct flash_info {int flash_type;};static struct flash_info flash;
static const struct flash_info *sfi=&flash;
static struct fixture io;
static int present,bad[2],map_error[2];
static unsigned int offsets[2],sizes[2],map_reads;
static int has_nand(void){return present;}
static int getpart_offset_size(const char *name,uint32_t *off,uint32_t *size)
{unsigned int c=strcmp(name,BOOTCONFIG_PART_NAME)?1:0;++map_reads;*off=offsets[c];*size=sizes[c];return map_error[c];}
static int nand_block_isbad(nand_info_t *n,unsigned long off)
{assert(n==nand_info);assert(off==0x100000||off==0x140000);return bad[off==0x140000];}
static int nand_read(nand_info_t *n,unsigned long off,size_t *len,unsigned char *p)
{unsigned int actual;int ret;assert(n==nand_info&&*len==336);ret=read_copy(&io,off==0x140000,p,336,&actual);*len=actual;return ret;}
'''.replace('CORE_TEST_PATH',str(root/'u-boot-2016/tools/h3c_bootconfig_readonly_test.c'))
post=r'''
static void runtime_setup(void)
{setup(&io);present=1;flash.flash_type=2;nand_info[0].size=0x8000000ULL;nand_info[0].writesize=2048;nand_info[0].erasesize=0x20000;bad[0]=bad[1]=map_error[0]=map_error[1]=0;offsets[0]=0x100000;offsets[1]=0x140000;sizes[0]=sizes[1]=0x40000;map_reads=0;}
static void failed_before_read(void){assert(get_bootconfig("rootfs")<0);assert(io.reads==0&&io.writes==0&&io.erases==0);}
int main(void)
{
 assert(core_main()==0);
 runtime_setup();assert(get_bootconfig("rootfs")==1&&io.reads==4);
 runtime_setup();mutate_word(&io,0,0,0);assert(get_bootconfig("rootfs")==1&&io.reads==4);
 runtime_setup();bad[0]=1;assert(get_bootconfig("rootfs")==1&&io.reads==2);
 runtime_setup();bad[0]=bad[1]=1;assert(get_bootconfig("rootfs")<0&&io.reads==0);
 runtime_setup();mutate_word(&io,1,4,8);assert(get_bootconfig("rootfs")<0&&io.reads==4);
 runtime_setup();for(unsigned int p=0;p<2;++p)memcpy(io.data[0][p]+32,"rootfs",7);assert(get_bootconfig("rootfs")<0);
 runtime_setup();io.actual[0][0]=io.actual[0][1]=335;assert(get_bootconfig("rootfs")==1);
 runtime_setup();for(unsigned int c=0;c<2;++c)for(unsigned int p=0;p<2;++p)io.status[c][p]=-74;assert(get_bootconfig("rootfs")<0);
 runtime_setup();io.data[0][1][4]^=1;assert(get_bootconfig("rootfs")<0);
 runtime_setup();present=0;failed_before_read();
 runtime_setup();flash.flash_type=99;failed_before_read();
 runtime_setup();nand_info[0].size=0x10000000;failed_before_read();
 runtime_setup();nand_info[0].writesize=4096;failed_before_read();
 runtime_setup();nand_info[0].erasesize=0x40000;failed_before_read();
 runtime_setup();offsets[0]=0x140000;failed_before_read();
 runtime_setup();offsets[1]=0x100000;failed_before_read();
 runtime_setup();sizes[0]=0x20000;failed_before_read();
 runtime_setup();sizes[1]=0xffffffffU;failed_before_read();
 runtime_setup();map_error[0]=-1;failed_before_read();
 runtime_setup();map_error[1]=-1;failed_before_read();
 assert(io.writes==0&&io.erases==0);
 puts("actual release getter + adapter host contracts PASS; no write API in selected call path");return 0;
}
'''
p=ev/'actual-runtime-test.c';p.write_text(pre+adapter+'\n'+getter+'\n'+post)
cmd=['cc','-std=c99','-Wall','-Wextra','-Werror','-pedantic','-O2','-idirafter',str(root/'u-boot-2016/include'),str(p),'-o',str(ev/'actual-runtime-test')]
subprocess.run(cmd,check=True);subprocess.run([str(ev/'actual-runtime-test')],check=True)
cmd[cmd.index('-O2')]='-O1';cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer'];cmd[-1]=str(ev/'actual-runtime-san')
subprocess.run(cmd,check=True)
import os
env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0';subprocess.run([str(ev/'actual-runtime-san')],check=True,env=env)
(ev/'HOST-RESULT.json').write_text(json.dumps({'status':'PASS_HOST_ONLY','runtime_source_extraction':True,'source_sha256':hashlib.sha256(s.encode()).hexdigest(),'strict_c':True,'asan_ubsan':True,'scope':'IO mocked; production getter/adapter and core actual source; no device','test_sha256':hashlib.sha256(p.read_bytes()).hexdigest()},indent=2)+'\n')
