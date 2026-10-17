// SPDX-License-Identifier: GPL-2.0+
/* NX30 HW-D NAND service. This profile is independent from the NOR MIBIB. */
#include <common.h>
#include <errno.h>
#include <u-boot/crc.h>
#include <linux/mtd/mtd.h>
#include <nand.h>
#include <asm/arch-qca-common/smem.h>
#include <bootconfig.h>
#include <environment.h>
#include <nx30_d_service.h>
#include <nx30_d_env_core.h>
#include <nx30_d_bc_core.h>
#include <nx30_d_read_core.h>
#include <nx30_nor_nand.h>
#include <h3c_nand_ecc.h>

#define NX30_D_SMEM_MAGIC1 0x55ee73aaU
#define NX30_D_SMEM_MAGIC2 0xe35ebddbU
#define NX30_D_SMEM_PARTS 32U
#define NX30_D_TABLE_COUNT 21U
#define NX30_D_NAND_SIZE 0x08000000U
#define NX30_D_ERASE 0x00020000U
#define NX30_D_PAGE 0x00000800U
#define NX30_D_ATTR 0x0000ffffU
static int nx30_d_default_original_read(unsigned int copy,u64 offset,void *buffer,size_t length,size_t *read);
static int nx30_d_mtd_read_window(void *ctx,u64 offset,size_t length,
                                  size_t *read_length,u8 *buffer);

struct nx30_d_smem_part { char name[16]; u32 start, size, attr; } __attribute__((packed));
struct nx30_d_smem_table {
 u32 magic[2], version, len;
 struct nx30_d_smem_part parts[NX30_D_SMEM_PARTS];
} __attribute__((packed));
struct nx30_d_expected { const char *name; u32 start, size; };
static const struct nx30_d_expected nx30_d_expected_parts[] = {
 {"0:SBL1",0x000,0x004}, {"0:MIBIB",0x004,0x004},
 {"0:BOOTCONFIG",0x008,0x002}, {"0:BOOTCONFIG1",0x00a,0x002},
 {"0:QSEE",0x00c,0x008}, {"0:QSEE_1",0x014,0x008},
 {"0:DEVCFG",0x01c,0x002}, {"0:DEVCFG_1",0x01e,0x002},
 {"0:CDT",0x020,0x002}, {"0:CDT_1",0x022,0x002},
 {"0:APPSBLENV",0x024,0x004}, {"0:APPSBL",0x028,0x00a},
 {"0:APPSBL_1",0x032,0x00a}, {"0:ART",0x03c,0x008},
 {"0:TRAINING",0x044,0x004}, {"rootfs",0x048,0x140},
 {"rootfs_1",0x188,0x140}, {"pdt_data",0x2c8,0x030},
 {"pdt_data_1",0x2f8,0x030}, {"exp",0x328,0x008},
 {"plugin",0x330,0x0b8},
};

static int nx30_d_peer_index(unsigned int index)
{
 switch (index) { case 4:return 5; case 5:return 4; case 6:return 7;
 case 7:return 6; case 8:return 9; case 9:return 8; case 11:return 12;
 case 12:return 11; default:return -1; }
}
static int nx30_d_find_expected(const struct nx30_d_smem_table *t,
                                const char *name, u32 *start, u32 *size)
{
 unsigned int i;
 for (i=0;i<t->len;i++) {
  if (!memchr(t->parts[i].name, '\0', sizeof(t->parts[i].name))) return -EINVAL;
  if (!strcmp(t->parts[i].name,name)) {
   if (start) *start=t->parts[i].start;
   if (size) *size=t->parts[i].size;
   return 0;
  }
 }
 return -ENOENT;
}

static int nx30_d_extent_named(const struct nx30_d_smem_table *t,
                               const char *name,struct nx30_d_extent *e)
{
 u32 start,size;
 if (!e || nx30_d_find_expected(t,name,&start,&size)) return -EINVAL;
 e->offset=(u64)start*NX30_D_ERASE; e->size=(u64)size*NX30_D_ERASE;
 return 0;
}

static int nx30_d_resolve(struct nx30_d_layout *l,
                          const struct nx30_d_smem_table *t)
{
 unsigned int i,j;
 u32 start,size,rs,rz,r1s,r1z;
 const struct nx30_d_expected *e;
 u32 crc;
 if (!l || !t || t->magic[0]!=NX30_D_SMEM_MAGIC1 ||
     t->magic[1]!=NX30_D_SMEM_MAGIC2 || t->version<3 || t->version>4 ||
     t->len!=NX30_D_TABLE_COUNT) return -EINVAL;
 for (i=0;i<t->len;i++) {
  const struct nx30_d_smem_part *p=&t->parts[i];
  if (!memchr(p->name,'\0',sizeof(p->name)) || p->attr!=NX30_D_ATTR ||
      !p->size || p->start>=NX30_D_NAND_SIZE/NX30_D_ERASE ||
      p->size>NX30_D_NAND_SIZE/NX30_D_ERASE-p->start) return -EINVAL;
  for (j=0;j<i;j++) {
   const struct nx30_d_smem_part *q=&t->parts[j];
   if (!strcmp(p->name,q->name) ||
       (p->start<q->start+q->size && q->start<p->start+p->size)) return -EINVAL;
  }
 }
 for (i=0;i<ARRAY_SIZE(nx30_d_expected_parts);i++) {
  e=&nx30_d_expected_parts[i];
  if (nx30_d_find_expected(t,e->name,&start,&size)) return -EINVAL;
  if (!strcmp(e->name,"rootfs") || !strcmp(e->name,"rootfs_1")) {
   if (size!=e->size) return -EINVAL;
   continue;
  }
  if (size!=e->size) return -EINVAL;
  if (start!=e->start) {
   int peer=nx30_d_peer_index(i);
   if (peer<0 || start!=nx30_d_expected_parts[peer].start) return -EINVAL;
  }
 }
 if (nx30_d_find_expected(t,"rootfs",&rs,&rz) ||
     nx30_d_find_expected(t,"rootfs_1",&r1s,&r1z) || rz!=0x140 || r1z!=0x140 ||
     !((rs==0x048 && r1s==0x188)||(rs==0x188 && r1s==0x048))) return -EINVAL;
 memset(l,0,sizeof(*l));
 l->nand_size=NX30_D_NAND_SIZE; l->erase_size=NX30_D_ERASE;
 l->write_size=NX30_D_PAGE; l->ecc_bits=0;
 l->slot_a.offset=0x00900000ULL; l->slot_a.size=0x02800000ULL;
 l->slot_b.offset=0x03100000ULL; l->slot_b.size=0x02800000ULL;
 l->env.offset=0x00480000ULL; l->env.size=0x00080000ULL;
 l->bc0.offset=0x00100000ULL; l->bc0.size=0x00040000ULL;
 l->bc1.offset=0x00140000ULL; l->bc1.size=0x00040000ULL;
 l->appsbl_low.offset=0x00500000ULL; l->appsbl_low.size=0x00140000ULL;
 l->appsbl_high.offset=0x00640000ULL; l->appsbl_high.size=0x00140000ULL;
 l->art.offset=0x00780000ULL; l->art.size=0x00100000ULL;
 l->training.offset=0x00880000ULL; l->training.size=0x00080000ULL;
 if (nx30_d_extent_named(t,"0:APPSBL",&l->appsbl_active) ||
     nx30_d_extent_named(t,"0:SBL1",&l->sbl1) ||
     nx30_d_extent_named(t,"0:MIBIB",&l->mibib) ||
     nx30_d_extent_named(t,"0:QSEE",&l->qsee) ||
     nx30_d_extent_named(t,"0:QSEE_1",&l->qsee_1) ||
     nx30_d_extent_named(t,"0:DEVCFG",&l->devcfg) ||
     nx30_d_extent_named(t,"0:DEVCFG_1",&l->devcfg_1) ||
     nx30_d_extent_named(t,"0:CDT",&l->cdt) ||
     nx30_d_extent_named(t,"0:CDT_1",&l->cdt_1) ||
     nx30_d_extent_named(t,"pdt_data",&l->pdt_data) ||
     nx30_d_extent_named(t,"pdt_data_1",&l->pdt_data_1) ||
     nx30_d_extent_named(t,"exp",&l->exp) ||
     nx30_d_extent_named(t,"plugin",&l->plugin)) return -EINVAL;
 crc=crc32(0,(const unsigned char *)t,offsetof(struct nx30_d_smem_table,parts)+t->len*sizeof(t->parts[0]));
 {
  const u32 geometry[4]={NX30_D_NAND_SIZE,NX30_D_ERASE,NX30_D_PAGE,4};
  l->generation=crc32(crc,(const unsigned char *)geometry,sizeof(geometry));
 }
 return 0;
}

int nx30_d_layout_get(struct nx30_d_layout *layout)
{
 const struct nx30_d_smem_table *t=get_smem_ptable_addr();
 struct mtd_info *mtd;
 int ret;
 if (!layout || !t) return -EINVAL;
 mtd=&nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX];
 if (mtd->size!=NX30_D_NAND_SIZE || mtd->erasesize!=NX30_D_ERASE ||
     mtd->writesize!=NX30_D_PAGE) return -ENODEV;
 ret=nx30_d_resolve(layout,t);
 if (ret) return ret;
 layout->ecc_bits=h3c_nand_ecc_io_mode();
 if (layout->ecc_bits!=4U && layout->ecc_bits!=8U) return -ENODEV;
 return 0;
}

static int nx30_d_parse_bc(const u8 *raw, struct bootconfig_info *out,
                           u32 *selector)
{
 unsigned int i, found=0;
 if (!raw || !out || !selector) return -EINVAL;
 memcpy(out,raw,sizeof(*out));
 if ((out->magic_start!=BOOTCONFIG_MAGIC_START &&
      out->magic_start!=BOOTCONFIG_MAGIC_START_TRYMODE) ||
     out->magic_end!=BOOTCONFIG_MAGIC_END || !out->numaltpart ||
     out->numaltpart>NUM_ALT_PARTITION) return -EINVAL;
 for (i=0;i<out->numaltpart;i++) {
  const char *name=out->per_part_entry[i].name;
  if (!memchr(name,'\0',ALT_PART_NAME_LENGTH)) return -EINVAL;
  if (!strcmp(name,"rootfs")) {
   if (found++ || out->per_part_entry[i].primaryboot>1) return -EINVAL;
   *selector=out->per_part_entry[i].primaryboot;
  }
 }
 return found==1?0:-EINVAL;
}

int nx30_d_slots_get(struct nx30_d_slots *slots)
{
 struct nx30_d_layout l; struct bootconfig_info bc0,bc1;
 const struct nx30_d_smem_table *t=get_smem_ptable_addr();
 struct mtd_info *mtd; u8 *raw0,*raw1; u32 rs,selector;
 int ret;
 if (!slots) return -EINVAL;
 ret=nx30_d_layout_get(&l); if (ret) return ret;
 ret=h3c_nand_ecc_select_mode(l.ecc_bits); if (ret) return ret;
 ret=nx30_d_find_expected(t,"rootfs",&rs,NULL); if (ret) return ret;
 mtd=&nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX]; raw0=malloc(l.bc0.size); raw1=malloc(l.bc1.size);
 if (!raw0 || !raw1) { ret=-ENOMEM; goto out; }
 ret=nx30_d_read_extent(nx30_d_mtd_read_window,mtd,l.bc0.offset,l.bc0.size,raw0);
 if (ret) goto out;
 ret=nx30_d_read_extent(nx30_d_mtd_read_window,mtd,l.bc1.offset,l.bc1.size,raw1);
 if (ret) goto out;
 if (memcmp(raw0,raw1,l.bc0.size)) { ret=-EUCLEAN; goto out; }
 ret=nx30_d_parse_bc(raw0,&bc0,&selector); if (ret) goto out;
 ret=nx30_d_parse_bc(raw1,&bc1,&selector); if (ret) goto out;
 memset(slots,0,sizeof(*slots));
 slots->generation=l.generation; slots->selector=selector; slots->age=bc0.age; slots->ecc_bits=l.ecc_bits;
 slots->active=selector?NX30_D_SLOT_B:NX30_D_SLOT_A;
 slots->inactive=selector?NX30_D_SLOT_A:NX30_D_SLOT_B;
 strcpy(slots->logical_a,rs==0x048?"rootfs":"rootfs_1");
 strcpy(slots->logical_b,rs==0x188?"rootfs":"rootfs_1");
 if (selector==0) strcpy(slots->active_logical,rs==0x048?"rootfs":"rootfs_1");
 else strcpy(slots->active_logical,rs==0x188?"rootfs":"rootfs_1");
 ret=0;
out:
 free(raw0); free(raw1); return ret;
}


int nx30_d_object_extent(enum nx30_d_object object,struct nx30_d_extent *extent)
{
 struct nx30_d_layout l; struct nx30_d_extent e; int ret;
 if (!extent) return -EINVAL;
 ret=nx30_d_layout_get(&l); if (ret) return ret;
 switch(object) {
 case NX30_D_OBJ_SLOT_A:e=l.slot_a;break;
 case NX30_D_OBJ_SLOT_B:e=l.slot_b;break;
 case NX30_D_OBJ_ENV:e=l.env;break;
 case NX30_D_OBJ_BC_ORIGINAL0:
 case NX30_D_OBJ_BC0:e=l.bc0;break;
 case NX30_D_OBJ_BC_ORIGINAL1:
 case NX30_D_OBJ_BC1:e=l.bc1;break;
 case NX30_D_OBJ_APPSBL_HIGH:e=l.appsbl_high;break;
 case NX30_D_OBJ_APPSBL_LOW:e=l.appsbl_low;break;
 case NX30_D_OBJ_ART:e=l.art;break;
 case NX30_D_OBJ_TRAINING:e=l.training;break;
 case NX30_D_OBJ_SBL1:e=l.sbl1;break;
 case NX30_D_OBJ_MIBIB:e=l.mibib;break;
 case NX30_D_OBJ_QSEE:e=l.qsee;break;
 case NX30_D_OBJ_QSEE_1:e=l.qsee_1;break;
 case NX30_D_OBJ_DEVCFG:e=l.devcfg;break;
 case NX30_D_OBJ_DEVCFG_1:e=l.devcfg_1;break;
 case NX30_D_OBJ_CDT:e=l.cdt;break;
 case NX30_D_OBJ_CDT_1:e=l.cdt_1;break;
 case NX30_D_OBJ_PDT_DATA:e=l.pdt_data;break;
 case NX30_D_OBJ_PDT_DATA_1:e=l.pdt_data_1;break;
 case NX30_D_OBJ_EXP:e=l.exp;break;
 case NX30_D_OBJ_PLUGIN:e=l.plugin;break;
 default:return -EINVAL;
 }
 *extent=e; return 0;
}

int nx30_d_read_object(enum nx30_d_object object, u64 offset,
                       void *buffer,size_t length,size_t *read)
{
 struct nx30_d_extent e; struct mtd_info *mtd;
 size_t done=0; int ret;
 if (read) *read=0;
 if (!buffer || !length || !read) return -EINVAL;
 if (length>NX30_D_READ_WINDOW_BYTES) return -E2BIG;
 if (object==NX30_D_OBJ_BC_ORIGINAL0 || object==NX30_D_OBJ_BC_ORIGINAL1)
  return nx30_d_default_original_read(object==NX30_D_OBJ_BC_ORIGINAL1,offset,buffer,length,read);
 ret=nx30_d_object_extent(object,&e); if (ret) return ret;

 if (offset>e.size || length>e.size-offset) return -ERANGE;
 ret=h3c_nand_ecc_select_mode(h3c_nand_ecc_io_mode());
 if (ret) return ret;
 mtd=&nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX];
 ret=mtd_read(mtd,(loff_t)(e.offset+offset),length,&done,buffer);
 *read=done;
 if (ret==-EUCLEAN && done==length) return 0;
 if (!ret && done!=length) return -EIO;
 return ret;
}

int nx30_d_boot_slot(enum nx30_d_slot slot)
{
 struct nx30_d_slots s; int ret;
 ret=nx30_d_slots_get(&s); if (ret) return ret;
 if (slot!=NX30_D_SLOT_A && slot!=NX30_D_SLOT_B) return -EINVAL;
 if (slot==NX30_D_SLOT_A) return run_command(!strcmp(s.logical_a,"rootfs") ? "bootipq rootfs":"bootipq rootfs_1",0);
 return run_command(!strcmp(s.logical_b,"rootfs") ? "bootipq rootfs":"bootipq rootfs_1",0);
}

#define NX30_D_ENV_DATA_BYTES (256U << 10)
#define NX30_D_ENV_PART_BYTES (512U << 10)
#define NX30_D_ENV_CRC_BYTES sizeof(u32)
#define NX30_D_ENV_CRC_DATA_BYTES (NX30_D_ENV_DATA_BYTES - NX30_D_ENV_CRC_BYTES)
static int nx30_d_mtd_read_window(void *ctx,u64 offset,size_t length,
                                  size_t *read_length,u8 *buffer)
{
 return mtd_read((struct mtd_info *)ctx,(loff_t)offset,length,read_length,buffer);
}
static const char * const nx30_d_env_keys[NX30_D_ENV_KEY_COUNT] = {
 "custom_network", "ipaddr", "netmask", "serverip", "ethact", "ethprime"
};
static struct {
 int valid;
 u32 layout_generation, env_generation;
 char token[NX30_D_TOKEN_LEN];
 u8 *original;
 u8 *candidate;
} nx30_d_env_pending;
static u32 nx30_d_env_nonce;

static int nx30_d_env_key_index(const char *name)
{
 unsigned int i;
 if (!name || !memchr(name,'\0',NX30_D_ENV_NAME_LEN)) return -1;
 for (i=0;i<NX30_D_ENV_KEY_COUNT;i++)
  if (!strcmp(name,nx30_d_env_keys[i])) return i;
 return -1;
}
static int nx30_d_env_read_all(u8 *raw)
{
 struct nx30_d_layout l; struct mtd_info *mtd=&nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX];
 int ret;
 if (!raw) return -EINVAL;
 ret=nx30_d_layout_get(&l); if (ret) return ret;
 if (l.env.offset!=0x480000 || l.env.size!=NX30_D_ENV_PART_BYTES ||
     ENV_HEADER_SIZE!=NX30_D_ENV_CRC_BYTES ||
     CONFIG_ENV_SIZE_MAX!=NX30_D_ENV_DATA_BYTES)
  return -EROFS;
 ret=h3c_nand_ecc_select_mode(l.ecc_bits); if (ret) return ret;
 ret=nx30_d_read_extent(nx30_d_mtd_read_window,mtd,l.env.offset,
                         NX30_D_ENV_PART_BYTES,raw);
 return ret;
}

static int nx30_d_env_crc_ok(const u8 *raw)
{
 u32 stored;
 if (!raw) return 0;
 memcpy(&stored,raw,sizeof(stored));
 return stored==crc32(0,raw+ENV_HEADER_SIZE,NX30_D_ENV_CRC_DATA_BYTES);
}

static int nx30_d_ipv4_valid(const char *value)
{
 unsigned int octet=0,parts=0,digits=0; const unsigned char *p=(const unsigned char *)value;
 if (!value || !*value) return 0;
 for (;*p;p++) {
  if (*p>='0' && *p<='9') {
   if (++digits>3) return 0;
   octet=octet*10+(*p-'0'); if (octet>255) return 0;
  } else if (*p=='.' && digits) {
   if (++parts>=4) return 0;
   octet=0; digits=0;
  } else return 0;
 }
 return parts==3 && digits>0;
}

static int nx30_d_env_value_valid(const char *name,const char *value)
{
 size_t i,n;
 if (!name || !value) return 0;
 n=strnlen(value,NX30_D_ENV_VALUE_LEN);
 if (n>=NX30_D_ENV_VALUE_LEN) return 0;
 if (!strcmp(name,"custom_network")) return !strcmp(value,"0") || !strcmp(value,"1");
 if (!strcmp(name,"ipaddr") || !strcmp(name,"netmask") || !strcmp(name,"serverip"))
  return nx30_d_ipv4_valid(value);
 if (!strcmp(name,"ethact") || !strcmp(name,"ethprime")) {
  if (!n || n>31) return 0;
  for (i=0;i<n;i++)
   if (!((value[i]>='a'&&value[i]<='z') || (value[i]>='A'&&value[i]<='Z') ||
         (value[i]>='0'&&value[i]<='9') || value[i]=='_' || value[i]=='-' || value[i]=='.'))
    return 0;
  return 1;
 }
 return 0;
}


/* Shared pure helper preserves non-target raw entries and post-terminator bytes. */
static int nx30_d_env_rewrite(const u8 *input,u8 *output,
                              const struct nx30_d_env_change *changes,size_t count,
                              struct nx30_d_env_snapshot *snapshot)
{
 const char *names[NX30_D_ENV_MAX_CHANGES],*values[NX30_D_ENV_MAX_CHANGES];
 unsigned char unset[NX30_D_ENV_MAX_CHANGES];
 int ret; size_t i;
 for (i=0;i<count;i++) {
  names[i]=changes[i].name; values[i]=changes[i].value; unset[i]=changes[i].unset;
 }
 memset(snapshot,0,sizeof(*snapshot));
 ret=nx30_d_env_core_rewrite(input,output,ENV_HEADER_SIZE,NX30_D_ENV_CRC_DATA_BYTES,
      nx30_d_env_keys,NX30_D_ENV_KEY_COUNT,names,values,unset,count,
      NX30_D_ENV_VALUE_LEN,(char *)snapshot->values);
 if (ret) return ret==-2?-EEXIST:ret==-3?-E2BIG:ret==-4?-ENOSPC:-EINVAL;
 /* Preserve any bytes outside the exact OEM CRC-covered payload. */
 memcpy(output+ENV_HEADER_SIZE+NX30_D_ENV_CRC_DATA_BYTES,
        input+ENV_HEADER_SIZE+NX30_D_ENV_CRC_DATA_BYTES,
        CONFIG_ENV_SIZE_MAX-ENV_HEADER_SIZE-NX30_D_ENV_CRC_DATA_BYTES);
 {
  u32 crc=crc32(0,output+ENV_HEADER_SIZE,NX30_D_ENV_CRC_DATA_BYTES);
  memcpy(output,&crc,sizeof(crc));
 }
 return 0;
}

int nx30_d_env_get(struct nx30_d_env_snapshot *snapshot)
{
 struct nx30_d_layout l; u8 *raw,*scratch; int ret;
 if (!snapshot) return -EINVAL;
 raw=memalign(ARCH_DMA_MINALIGN,NX30_D_ENV_PART_BYTES);
 scratch=memalign(ARCH_DMA_MINALIGN,NX30_D_ENV_DATA_BYTES);
 if (!raw || !scratch) { free(raw); free(scratch); return -ENOMEM; }
 ret=nx30_d_env_read_all(raw);
 if (!ret && !nx30_d_env_crc_ok(raw)) ret=-EBADMSG;
 if (!ret) {
  struct nx30_d_env_snapshot values; u32 crc;
  ret=nx30_d_env_rewrite(raw,scratch,NULL,0,&values);
  if (!ret) {
   ret=nx30_d_layout_get(&l);
   if (!ret) {
    crc=crc32(0,raw,NX30_D_ENV_PART_BYTES);
    values.generation=crc; *snapshot=values;
   }
  }
 }
 free(scratch); free(raw); return ret;
}

int nx30_d_env_prepare(const struct nx30_d_env_change *changes,size_t count,
                       u32 expected_generation,char token[NX30_D_TOKEN_LEN],
                       struct nx30_d_result *result)
{
 struct nx30_d_layout l; struct nx30_d_env_snapshot values; u8 *raw=NULL,*candidate=NULL;
 size_t i,j; u32 generation,nonce,hi,lo; int ret;
 if (result) { memset(result,0,sizeof(*result)); result->stage=NX30_D_STAGE_PREFLIGHT; }
 if (!changes || !count || count>NX30_D_ENV_MAX_CHANGES || !token || nx30_d_env_pending.valid) {
  ret=nx30_d_env_pending.valid?-EBUSY:-EINVAL; if (result) result->status=ret; return ret;
 }
 for (i=0;i<count;i++) {
  int index=nx30_d_env_key_index(changes[i].name);
  if (index<0 || !memchr(changes[i].value,'\0',NX30_D_ENV_VALUE_LEN) ||
      changes[i].unset>1 || (changes[i].unset && changes[i].value[0]) ||
      (!changes[i].unset && !nx30_d_env_value_valid(changes[i].name,changes[i].value))) { ret=-EINVAL; if (result) result->status=ret; return ret; }
  for (j=0;j<i;j++) if (!strcmp(changes[i].name,changes[j].name)) { ret=-EEXIST; if (result) result->status=ret; return ret; }
 }
 raw=memalign(ARCH_DMA_MINALIGN,NX30_D_ENV_PART_BYTES);
 candidate=memalign(ARCH_DMA_MINALIGN,NX30_D_ENV_DATA_BYTES);
 if (!raw || !candidate) { ret=-ENOMEM; goto out; }
 ret=nx30_d_env_read_all(raw); if (ret) goto out;
 if (!nx30_d_env_crc_ok(raw)) { ret=-EBADMSG; goto out; }
 generation=crc32(0,raw,NX30_D_ENV_PART_BYTES);
 if (generation!=expected_generation) { ret=-ESTALE; goto out; }
 ret=nx30_d_env_rewrite(raw,candidate,changes,count,&values); if (ret) goto out;
 ret=nx30_d_layout_get(&l); if (ret) goto out;
 nx30_d_env_pending.original=raw; raw=NULL;
 nx30_d_env_pending.candidate=candidate; candidate=NULL;
 nx30_d_env_pending.layout_generation=l.generation;
 nx30_d_env_pending.env_generation=generation;
 nonce=++nx30_d_env_nonce;
 hi=crc32(generation,(const u8 *)&nonce,sizeof(nonce)) ^ (u32)get_timer(0);
 lo=crc32(hi,(const u8 *)&l.generation,sizeof(l.generation)) ^ nonce;
 snprintf(nx30_d_env_pending.token,sizeof(nx30_d_env_pending.token),"%08x%08x",hi,lo);
 nx30_d_env_pending.valid=1; strcpy(token,nx30_d_env_pending.token);
 if (result) { result->generation=generation; result->crc_before=*(u32 *)nx30_d_env_pending.original; result->crc_after=*(u32 *)nx30_d_env_pending.candidate; }
 ret=0;
out:
 if (result) result->status=ret;
 free(raw); free(candidate); return ret;
}

static void nx30_d_env_pending_clear(void)
{
 free(nx30_d_env_pending.original); free(nx30_d_env_pending.candidate);
 memset(&nx30_d_env_pending,0,sizeof(nx30_d_env_pending));
}

int nx30_d_env_commit(const char *token,struct nx30_d_result *result)
{
 struct nx30_d_layout l; struct mtd_info *mtd=&nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX];
 struct erase_info erase; u8 *current=NULL,*verify=NULL; size_t written=0;
 int ret,opened=0,close_error;
 if (result) { memset(result,0,sizeof(*result)); result->stage=NX30_D_STAGE_REVALIDATE; }
 if (!token || !nx30_d_env_pending.valid || strcmp(token,nx30_d_env_pending.token)) { if (result) result->status=-EINVAL; return -EINVAL; }
 current=memalign(ARCH_DMA_MINALIGN,NX30_D_ENV_PART_BYTES);
 verify=memalign(ARCH_DMA_MINALIGN,NX30_D_ENV_PART_BYTES);
 if (!current || !verify) { ret=-ENOMEM; goto out; }
 ret=nx30_d_layout_get(&l); if (ret) goto out;
 if (l.generation!=nx30_d_env_pending.layout_generation) { ret=-ESTALE; goto out; }
 ret=nx30_d_env_read_all(current); if (ret) goto out;
 if (memcmp(current,nx30_d_env_pending.original,NX30_D_ENV_PART_BYTES) ||
     crc32(0,current,NX30_D_ENV_PART_BYTES)!=nx30_d_env_pending.env_generation ||
     !nx30_d_env_crc_ok(current)) { ret=-ESTALE; goto out; }
 if (result) result->stage=NX30_D_STAGE_WINDOW_OPEN;
 ret=nx30_d_qpic_write_window_open(mtd,l.env.offset,NX30_D_ENV_DATA_BYTES,4);
 if (ret) goto out;
 opened=1;
 memset(&erase,0,sizeof(erase)); erase.mtd=mtd; erase.addr=l.env.offset; erase.len=NX30_D_ENV_DATA_BYTES;
 if (result) result->stage=NX30_D_STAGE_ERASE;
 ret=mtd_erase(mtd,&erase); if (ret) goto out;
 if (result) result->stage=NX30_D_STAGE_WRITE;
 ret=mtd_write(mtd,l.env.offset,NX30_D_ENV_DATA_BYTES,&written,nx30_d_env_pending.candidate);
 if (ret || written!=NX30_D_ENV_DATA_BYTES) { if (!ret) ret=-EIO; goto out; }
 if (result) result->stage=NX30_D_STAGE_READBACK;
 ret=nx30_d_read_extent(nx30_d_mtd_read_window,mtd,l.env.offset,
                         NX30_D_ENV_PART_BYTES,verify);
 if (ret) goto out;
 if (memcmp(verify,nx30_d_env_pending.candidate,NX30_D_ENV_DATA_BYTES) ||
     memcmp(verify+NX30_D_ENV_DATA_BYTES,nx30_d_env_pending.original+NX30_D_ENV_DATA_BYTES,
            NX30_D_ENV_PART_BYTES-NX30_D_ENV_DATA_BYTES) ||
     !nx30_d_env_crc_ok(verify)) { ret=-EIO; goto out; }
 if (result) { result->bytes_verified=NX30_D_ENV_PART_BYTES; result->crc_after=*(u32 *)verify; result->generation=crc32(0,verify,NX30_D_ENV_PART_BYTES); result->stage=NX30_D_STAGE_COMPLETE; }
 ret=0; goto out;
out:
 if (opened) { nx30_d_qpic_write_window_close(); close_error=nx30_d_qpic_write_window_close_error(); if (close_error) ret=close_error; }
 if (result) result->status=ret;
 free(current); free(verify); nx30_d_env_pending_clear(); return ret;
}

void nx30_d_env_abort(const char *token)
{
 if (nx30_d_env_pending.valid && token && !strcmp(token,nx30_d_env_pending.token))
  nx30_d_env_pending_clear();
}


#define NX30_D_BC_BYTES (256U << 10)
static struct {
 int valid, recovery;
 struct nx30_d_result failure;
 u32 failed_copy;
 u32 layout_generation;
 char token[NX30_D_TOKEN_LEN];
 enum nx30_d_slot target;
 u32 old_selector;
 u8 *original[2];
 u8 *candidate;
} nx30_d_default_pending;
static u32 nx30_d_default_nonce;

static void nx30_d_default_pending_clear(void)
{
 free(nx30_d_default_pending.original[0]);
 free(nx30_d_default_pending.original[1]);
 free(nx30_d_default_pending.candidate);
 memset(&nx30_d_default_pending,0,sizeof(nx30_d_default_pending));
}

/* Failed transactions remain pinned for read-only export until reboot. Neither
 * abort nor a new prepare can discard these sole pre-write backups. */
int nx30_d_default_failure_get(struct nx30_d_result *result,u32 *copy)
{
 if (!result || !copy) return -EINVAL;
 if (!nx30_d_default_pending.recovery) return -ENOENT;
 *result=nx30_d_default_pending.failure;
 *copy=nx30_d_default_pending.failed_copy;
 return 0;
}
int nx30_d_default_original_get(unsigned int copy,const void **bytes,size_t *length)
{
 if (!bytes || !length || copy>1) return -EINVAL;
 if (!nx30_d_default_pending.recovery || !nx30_d_default_pending.original[copy]) return -ENOENT;
 *bytes=nx30_d_default_pending.original[copy];
 *length=NX30_D_BC_BYTES;
 return 0;
}
static int nx30_d_default_original_read(unsigned int copy,u64 offset,
                                        void *buffer,size_t length,size_t *read)
{
 if (!nx30_d_default_pending.recovery || copy>1 ||
     !nx30_d_default_pending.original[copy]) return -ENOENT;
 if (offset>NX30_D_BC_BYTES || length>NX30_D_BC_BYTES-offset) return -ERANGE;
 memcpy(buffer,nx30_d_default_pending.original[copy]+offset,length);
 *read=length;
 return 0;
}

int nx30_d_default_abort(const char *token)
{
 if (!token || !nx30_d_default_pending.valid ||
     strnlen(token,NX30_D_TOKEN_LEN)!=NX30_D_TOKEN_LEN-1 ||
     strcmp(token,nx30_d_default_pending.token)) return -EINVAL;
 nx30_d_default_pending_clear();
 return 0;
}

static int nx30_d_bc_read(struct mtd_info *mtd,struct nx30_d_extent e,u8 *raw)
{
 struct nx30_d_layout l;
 int ret=nx30_d_layout_get(&l);
 if (ret) return ret;
 ret=h3c_nand_ecc_select_mode(l.ecc_bits);
 if (ret) return ret;
 return nx30_d_read_extent(nx30_d_mtd_read_window,mtd,e.offset,e.size,raw);
}

static int nx30_d_bc_update_selector(u8 *raw,u32 selector)
{
 struct bootconfig_info *bc=(struct bootconfig_info *)raw;
 unsigned int i,found=0;
 if (!raw || selector>1 ||
     (bc->magic_start!=BOOTCONFIG_MAGIC_START &&
      bc->magic_start!=BOOTCONFIG_MAGIC_START_TRYMODE) ||
     bc->magic_end!=BOOTCONFIG_MAGIC_END || !bc->numaltpart ||
     bc->numaltpart>NUM_ALT_PARTITION) return -EINVAL;
 for (i=0;i<bc->numaltpart;i++) {
  struct bootconfig_part_entry *e=&bc->per_part_entry[i];
  if (!memchr(e->name,'\0',sizeof(e->name))) return -EINVAL;
  if (!strcmp(e->name,"rootfs")) {
   if (found++ || e->primaryboot>1) return -EINVAL;
   e->primaryboot=selector;
  }
 }
 return found==1?0:-EINVAL;
}

int nx30_d_default_prepare(enum nx30_d_slot target,char token[NX30_D_TOKEN_LEN],
                           struct nx30_d_result *result)
{
 struct nx30_d_layout l; struct nx30_d_slots slots;
 struct mtd_info *mtd=&nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX];
 struct bootconfig_info parsed; u32 selector,nonce,hi,lo; size_t root_index; unsigned int i; int ret;
 if (result) { memset(result,0,sizeof(*result)); result->stage=NX30_D_STAGE_PREFLIGHT; }
 if (!token || (target!=NX30_D_SLOT_A && target!=NX30_D_SLOT_B) ||
     (nx30_d_default_pending.valid || nx30_d_default_pending.recovery)) {
  ret=(nx30_d_default_pending.valid || nx30_d_default_pending.recovery)?-EBUSY:-EINVAL; goto out; }
 ret=nx30_d_layout_get(&l); if (ret) goto out;
 if (l.ecc_bits!=4U) { ret=-EOPNOTSUPP; goto out; }
 ret=nx30_d_slots_get(&slots); if (ret) goto out;
 if (slots.ecc_bits!=4U) { ret=-EOPNOTSUPP; goto out; }
 if (slots.generation!=l.generation || l.bc0.size!=NX30_D_BC_BYTES ||
     l.bc1.size!=NX30_D_BC_BYTES) { ret=-ESTALE; goto out; }
 nx30_d_default_pending.original[0]=memalign(ARCH_DMA_MINALIGN,NX30_D_BC_BYTES);
 nx30_d_default_pending.original[1]=memalign(ARCH_DMA_MINALIGN,NX30_D_BC_BYTES);
 nx30_d_default_pending.candidate=memalign(ARCH_DMA_MINALIGN,NX30_D_BC_BYTES);
 if (!nx30_d_default_pending.original[0] || !nx30_d_default_pending.original[1] ||
     !nx30_d_default_pending.candidate) { ret=-ENOMEM; goto fail_pending; }
 ret=nx30_d_bc_read(mtd,l.bc0,nx30_d_default_pending.original[0]); if (ret) goto fail_pending;
 ret=nx30_d_bc_read(mtd,l.bc1,nx30_d_default_pending.original[1]); if (ret) goto fail_pending;
 if (memcmp(nx30_d_default_pending.original[0],nx30_d_default_pending.original[1],NX30_D_BC_BYTES)) { ret=-EUCLEAN; goto fail_pending; }
 memcpy(&parsed,nx30_d_default_pending.original[0],sizeof(parsed));
 ret=nx30_d_parse_bc(nx30_d_default_pending.original[0],&parsed,&selector); if (ret) goto fail_pending;
 nx30_d_default_pending.old_selector=selector;
 memcpy(nx30_d_default_pending.candidate,nx30_d_default_pending.original[0],NX30_D_BC_BYTES);
 ret=nx30_d_bc_update_selector(nx30_d_default_pending.candidate,target==NX30_D_SLOT_B?1:0); if (ret) goto fail_pending;
 /* The sole allowed difference is the rootfs primaryboot u32; AGE and all
  * opaque bytes remain exactly as backed up in both copies. */
 root_index=offsetof(struct bootconfig_info,per_part_entry);
 {
  unsigned int i; for (i=0;i<parsed.numaltpart;i++)
   if (!strcmp(parsed.per_part_entry[i].name,"rootfs")) {
    root_index+=i*sizeof(struct bootconfig_part_entry)+offsetof(struct bootconfig_part_entry,primaryboot); break;
   }
 }
 for (i=0;i<NX30_D_BC_BYTES;i++)
  if ((i<root_index || i>=root_index+sizeof(u32)) &&
      nx30_d_default_pending.candidate[i]!=nx30_d_default_pending.original[0][i]) { ret=-EUCLEAN; goto fail_pending; }
 nx30_d_default_pending.layout_generation=l.generation;
 nx30_d_default_pending.target=target; nonce=++nx30_d_default_nonce;
 hi=crc32(slots.generation,nx30_d_default_pending.original[0],NX30_D_BC_BYTES)^nonce^get_timer(0);
 lo=crc32(hi,nx30_d_default_pending.candidate,NX30_D_BC_BYTES)^slots.selector;
 snprintf(nx30_d_default_pending.token,sizeof(nx30_d_default_pending.token),"%08x%08x",hi,lo);
 nx30_d_default_pending.valid=1; strcpy(token,nx30_d_default_pending.token);
 if (result) { result->generation=l.generation; result->selector=selector; result->age=slots.age; result->crc_before=crc32(0,nx30_d_default_pending.original[0],NX30_D_BC_BYTES); result->crc_after=crc32(0,nx30_d_default_pending.candidate,NX30_D_BC_BYTES); result->target_offset=l.bc1.offset; result->target_size=2ULL*NX30_D_BC_BYTES; }
 ret=0; goto out;
fail_pending:
 nx30_d_default_pending_clear();
out:
 if (result) result->status=ret;
 return ret;
}

static int nx30_d_bc_program(struct mtd_info *mtd,struct nx30_d_extent e,
                             const u8 *data,struct nx30_d_result *result)
{
 struct erase_info erase; u8 *verify; size_t written=0; int ret,opened=0,close_error;
 verify=memalign(ARCH_DMA_MINALIGN,e.size); if (!verify) return -ENOMEM;
 ret=nx30_d_qpic_write_window_open(mtd,e.offset,e.size,4); if (ret) goto out;
 opened=1; memset(&erase,0,sizeof(erase)); erase.mtd=mtd; erase.addr=e.offset; erase.len=e.size;
 if (result) result->stage=NX30_D_STAGE_ERASE;
 ret=mtd_erase(mtd,&erase); if (ret) goto out;
 if (result) result->stage=NX30_D_STAGE_WRITE;
 ret=mtd_write(mtd,e.offset,e.size,&written,data);
 if (ret || written!=e.size) { if (!ret) ret=-EIO; goto out; }
 if (result) result->stage=NX30_D_STAGE_READBACK;
 ret=nx30_d_read_extent(nx30_d_mtd_read_window,mtd,e.offset,e.size,verify);
 if (ret) goto out;
 if (memcmp(verify,data,e.size)) { ret=-EIO; goto out; }
 if (result) result->bytes_verified+=e.size;
 ret=0;
out:
 if (opened) { nx30_d_qpic_write_window_close(); close_error=nx30_d_qpic_write_window_close_error(); if (close_error) ret=close_error; }
 free(verify); return ret;
}

struct nx30_d_bc_commit_ctx {
 struct mtd_info *mtd;
 struct nx30_d_extent extent[2];
 struct nx30_d_result *result;
};
static int nx30_d_bc_program_callback(void *cookie,unsigned int copy,const void *bytes)
{
 struct nx30_d_bc_commit_ctx *ctx=cookie;
 if (!ctx || copy>1) return -EINVAL;
 nx30_d_default_pending.failed_copy=copy;
 return nx30_d_bc_program(ctx->mtd,ctx->extent[copy],bytes,ctx->result);
}

int nx30_d_default_commit(const char *token,struct nx30_d_result *result)
{
 struct nx30_d_layout l; struct mtd_info *mtd=&nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX];
 struct nx30_d_bc_commit_ctx ctx;
 u8 *now0=NULL,*now1=NULL; int ret, attempted=0;
 struct nx30_d_result local_result;
 if (!result) result=&local_result;
 if (result) { memset(result,0,sizeof(*result)); result->stage=NX30_D_STAGE_REVALIDATE; }
 if (!token || !nx30_d_default_pending.valid || strcmp(token,nx30_d_default_pending.token)) return -EINVAL;
 now0=memalign(ARCH_DMA_MINALIGN,NX30_D_BC_BYTES); now1=memalign(ARCH_DMA_MINALIGN,NX30_D_BC_BYTES);
 if (!now0 || !now1) { ret=-ENOMEM; goto out; }
 ret=nx30_d_layout_get(&l); if (ret) goto out;
 if (l.generation!=nx30_d_default_pending.layout_generation) { ret=-ESTALE; goto out; }
 ret=nx30_d_bc_read(mtd,l.bc0,now0); if (ret) goto out;
 ret=nx30_d_bc_read(mtd,l.bc1,now1); if (ret) goto out;
 if (memcmp(now0,nx30_d_default_pending.original[0],NX30_D_BC_BYTES) ||
     memcmp(now1,nx30_d_default_pending.original[1],NX30_D_BC_BYTES) ||
     memcmp(now0,now1,NX30_D_BC_BYTES)) { ret=-ESTALE; goto out; }
 /* Full backups of both peers are retained until both new copies verify. */
 ctx.mtd=mtd; ctx.extent[0]=l.bc0; ctx.extent[1]=l.bc1; ctx.result=result;
 attempted=1;
 ret=nx30_d_bc_core_commit_pair(&ctx,nx30_d_bc_program_callback,
      nx30_d_default_pending.candidate);
 if (ret) goto out;
 if (result) { result->bytes_verified=2*NX30_D_BC_BYTES; result->crc_after=crc32(0,nx30_d_default_pending.candidate,NX30_D_BC_BYTES); result->generation=l.generation; result->selector=nx30_d_default_pending.target==NX30_D_SLOT_B?1:0; result->age=*(u32 *)(nx30_d_default_pending.candidate+offsetof(struct bootconfig_info,age)); result->stage=NX30_D_STAGE_COMPLETE; }
out:
 if (result) result->status=ret;
 free(now0); free(now1);
 if (ret && attempted) {
  nx30_d_default_pending.valid=0;
  memset(nx30_d_default_pending.token,0,sizeof(nx30_d_default_pending.token));
  nx30_d_default_pending.recovery=1;
  nx30_d_default_pending.failure=*result;
 } else nx30_d_default_pending_clear();
 return ret;
}

