#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <rt3000_b_layout.h>
#include <nand.h>
#include <asm/arch-qca-common/smem.h>
#define ERASE 0x20000U
#define NAND_SIZE 0x08000000ULL
nand_info_t nand_info[2]; qca_smem_flash_info_t qca_smem_flash_info; static unsigned spi_nand_id;
static unsigned char raw[sizeof(uint32_t)*4+32*28];
uint32_t crc32(uint32_t c,const unsigned char*d,size_t n){size_t i; c=~c;for(i=0;i<n;i++){int b;c^=d[i];for(b=0;b<8;b++)c=(c>>1)^(0xedb88320U&-(int)(c&1));}return ~c;}
unsigned int is_spi_nand_available(void){return spi_nand_id;} uint32_t get_nand_block_size(uint8_t id){return nand_info[id].erasesize;}
const void *get_smem_ptable_addr(void){return raw;}
#include "../lib/rt3000_b_layout.c"
static const char *pn[]={"0:SBL1","0:MIBIB","0:BOOTCONFIG","0:BOOTCONFIG1","0:QSEE","0:QSEE_1","0:DEVCFG","0:DEVCFG_1","0:CDT","0:CDT_1","0:APPSBLENV","0:APPSBL","0:APPSBL_1","0:ART","0:TRAINING"};
static const uint32_t po[]={0,0x80000,0x100000,0x140000,0x180000,0x280000,0x380000,0x3c0000,0x400000,0x440000,0x480000,0x500000,0x640000,0x780000,0x880000};
static const uint32_t ps[]={0x80000,0x80000,0x40000,0x40000,0x100000,0x100000,0x40000,0x40000,0x40000,0x40000,0x80000,0x140000,0x140000,0x100000,0x80000};
static const char *sn[]={"pdt_data","pdt_data_1","exp","plugin"};
static const uint32_t so[]={0x5900000,0x5f00000,0x6500000,0x6600000},ss[]={0x600000,0x600000,0x100000,0x1700000};
static struct rt3_smem_table *T(void){return (void*)raw;}
static void P(unsigned i,const char*n,uint32_t o,uint32_t z){struct rt3_smem_part*p=&T()->parts[i];memset(p,0,sizeof(*p));snprintf(p->name,sizeof(p->name),"%s",n);p->start=o/ERASE;p->size=z/ERASE;p->attr=0xffff;}
static void fixture(int single){unsigned i,n=0;spi_nand_id=0;memset(raw,0,sizeof(raw));T()->magic[0]=RT3_SMEM_MAGIC1;T()->magic[1]=RT3_SMEM_MAGIC2;T()->version=4;for(i=0;i<15;i++,n++)P(n,pn[i],po[i],ps[i]);P(n++,"rootfs",0x900000,single?0x5000000:0x2800000);if(!single)P(n++,"rootfs_1",0x3100000,0x2800000);for(i=0;i<4;i++,n++)P(n,sn[i],so[i],ss[i]);T()->len=n;nand_info[0].size=NAND_SIZE;nand_info[0].erasesize=ERASE;nand_info[0].writesize=2048;nand_info[0].oobsize=64;qca_smem_flash_info.flash_type=11;qca_smem_flash_info.flash_block_size=ERASE;qca_smem_flash_info.flash_secondary_type=0;rt3_layout_invalidate_cache();}
static unsigned part_index(const char *name){unsigned i;for(i=0;i<T()->len;i++)if(!strcmp(T()->parts[i].name,name))return i;return T()->len;}
static void swap_starts(const char *a,const char *b){unsigned ia=part_index(a),ib=part_index(b);uint32_t t;if(ia>=T()->len||ib>=T()->len)return;t=T()->parts[ia].start;T()->parts[ia].start=T()->parts[ib].start;T()->parts[ib].start=t;}
static void screenshot_fixture(void){fixture(0);swap_starts("0:QSEE","0:QSEE_1");swap_starts("0:DEVCFG","0:DEVCFG_1");swap_starts("0:CDT","0:CDT_1");swap_starts("0:APPSBL","0:APPSBL_1");swap_starts("rootfs","rootfs_1");}
#define G_TYPE(type, secondary) ((struct rt3_geometry){.flash_type=(type),.flash_secondary_type=(secondary),.flash_block_size=ERASE,.nand_size=NAND_SIZE,.nand_erasesize=ERASE,.nand_writesize=2048,.nand_oobsize=64})
#define G G_TYPE(11,0)
#define C(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){struct rt3_layout l;const struct rt3_partition*p;struct rt3_smem_part tmp;
 fixture(0);C(rt3_layout_read(&l)==0);C(l.mode==RT3_LAYOUT_DUAL);C(rt3_resolve_table(T(),&G,&l)==0);C(l.mode==RT3_LAYOUT_DUAL&&l.count==2);C(l.parts[0].display_index==16&&l.parts[1].display_index==17);C(rt3_layout_find(&l,"rootfs_1",&p)==0&&p->offset==0x3100000);C(rt3_layout_find(&l,"mtd16",&p)==-EINVAL);tmp=T()->parts[15];T()->parts[15]=T()->parts[17];T()->parts[17]=tmp;C(rt3_layout_read(&l)==0);C(l.parts[0].display_index==18&&l.parts[1].display_index==17);C(rt3_layout_find(&l,"rootfs",&p)==0&&p->offset==0x900000);
 fixture(1);C(rt3_resolve_table(T(),&G,&l)==0);C(l.mode==RT3_LAYOUT_SINGLE&&l.count==1&&l.parts[0].size==0x5000000);
 fixture(1);T()->version=3;C(rt3_resolve_table(T(),&G,&l)==0);
 fixture(1);T()->version=2;C(rt3_resolve_table(T(),&G,&l)==-EINVAL);
 fixture(0);T()->parts[16].name[0]=120;C(rt3_resolve_table(T(),&G,&l)!=0);
 fixture(0);tmp=T()->parts[15];T()->parts[15]=T()->parts[17];T()->parts[17]=tmp;C(rt3_resolve_table(T(),&G,&l)==0);C(l.parts[0].display_index==18&&l.parts[1].display_index==17);
 fixture(0);T()->parts[18].start=T()->parts[17].start;C(rt3_resolve_table(T(),&G,&l)==-EEXIST);
 fixture(0);strcpy(T()->parts[16].name,"rootfs");C(rt3_resolve_table(T(),&G,&l)==-EEXIST);
 fixture(0);T()->parts[15].attr=0x02ffffff;C(rt3_resolve_table(T(),&G,&l)==-EINVAL);
 fixture(0);T()->parts[17].size=0xffffffffU;C(rt3_resolve_table(T(),&G,&l)==-EINVAL);
 fixture(0);T()->parts[20].size=0xffffffffU;C(rt3_resolve_table(T(),&G,&l)==-EINVAL);
 fixture(0);T()->parts[15].size=0x50000000U/ERASE;C(rt3_resolve_table(T(),&G,&l)==-ERANGE);
 fixture(0);C(rt3_resolve_table(T(),&(struct rt3_geometry){.flash_type=11,.flash_block_size=ERASE,.nand_size=NAND_SIZE,.nand_erasesize=0x10000,.nand_writesize=2048},&l)==-EINVAL);
 fixture(0);qca_smem_flash_info.flash_type=2;C(rt3_layout_read(&l)==0);C(rt3_resolve_table(T(),&G_TYPE(2,0),&l)==0);
 fixture(0);C(rt3_resolve_table(T(),&((struct rt3_geometry){.flash_type=2,.flash_block_size=ERASE*2,.nand_size=NAND_SIZE,.nand_erasesize=ERASE,.nand_writesize=2048}),&l)==-EINVAL);
 fixture(0);T()->parts[11].size++;C(rt3_resolve_table(T(),&G_TYPE(2,0),&l)!=0);
 fixture(0);C(rt3_resolve_table(T(),&G_TYPE(SMEM_BOOT_SPI_FLASH,0),&l)==-EINVAL);C(rt3_resolve_table(T(),&G_TYPE(SMEM_BOOT_NORPLUSNAND,0),&l)==-EINVAL);
 fixture(0);T()->parts[15].size=0xffffffffU;C(rt3_resolve_table(T(),&G_TYPE(2,0),&l)==-EINVAL);C(rt3_resolve_table(T(),&G_TYPE(11,0),&l)==-EINVAL);
 fixture(0);T()->parts[15].attr=0x0100ffffU;C(rt3_layout_read(&l)==0);spi_nand_id=1;C(rt3_layout_read(&l)==-ENODEV);spi_nand_id=0;
 fixture(0);C(rt3_layout_read(&l)==0);{u32 fp=l.fingerprint;qca_smem_flash_info.flash_secondary_type=6U;C(rt3_layout_read(&l)==0);C(l.fingerprint!=fp);}
 fixture(0);C(rt3_layout_read(&l)==0);C(rt3_layout_default_target(&l,0,&p)==0&&!strcmp(p->name,"rootfs")&&p->offset==0x900000);C(rt3_layout_default_target(&l,1,&p)==0&&!strcmp(p->name,"rootfs_1")&&p->offset==0x3100000);
 screenshot_fixture();C(rt3_layout_read(&l)==0);C(l.parts[0].offset==0x3100000&&l.parts[1].offset==0x900000);C(rt3_layout_find(&l,"rootfs",&p)==0&&p->offset==0x3100000);C(rt3_layout_default_target(&l,0,&p)==0&&!strcmp(p->name,"rootfs_1")&&p->offset==0x900000);C(rt3_layout_default_target(&l,1,&p)==0&&!strcmp(p->name,"rootfs")&&p->offset==0x3100000);C(rt3_layout_default_target(&l,2,&p)==-EINVAL&&p==NULL);
 fixture(0);swap_starts("0:QSEE","0:QSEE_1");swap_starts("0:CDT","0:CDT_1");swap_starts("rootfs","rootfs_1");C(rt3_layout_read(&l)==0);C(rt3_layout_default_target(&l,0,&p)==0&&!strcmp(p->name,"rootfs_1"));
 fixture(0);T()->parts[part_index("0:QSEE")].start=T()->parts[part_index("0:QSEE_1")].start;C(rt3_layout_read(&l)!=0);
 fixture(0);T()->parts[part_index("0:QSEE")].size--;C(rt3_layout_read(&l)!=0);
 fixture(0);T()->parts[part_index("0:APPSBL")].start++;C(rt3_layout_read(&l)!=0);
 fixture(0);strcpy(T()->parts[part_index("0:QSEE_1")].name,"QSEE_BAD");C(rt3_layout_read(&l)!=0);
 fixture(0);T()->parts[15].start=T()->parts[16].start;C(rt3_layout_read(&l)!=0);
 fixture(1);C(rt3_resolve_table(T(),&G,&l)==0);C(rt3_layout_find(&l,"rootfs_1",&p)==-ENOENT);C(rt3_layout_default_target(&l,0,&p)==0&&!strcmp(p->name,"rootfs")&&p->offset==0x900000);C(rt3_layout_default_target(&l,1,&p)==-EINVAL&&p==NULL);
 fixture(1);T()->parts[15].start=0x3100000U/ERASE;C(rt3_resolve_table(T(),&G,&l)!=0);
 puts("RT3000-B layout host tests: PASS");return 0;}
