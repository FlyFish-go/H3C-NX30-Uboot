/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bridge_first_install.h"
#include "bridge_sha256.h"
#include <string.h>

static uint32_t word(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static int hash_is(const uint8_t *p,size_t n,const char *expected)
{
    bi_sha256_ctx ctx;uint8_t d[32];char s[65];
    bi_sha256_init(&ctx);bi_sha256_update(&ctx,p,n);bi_sha256_final(&ctx,d);
    bi_sha256_hex(d,s);return !strcmp(s,expected);
}
int bfi_d_env_bootargs(const uint8_t *p,size_t n)
{
    /* Exact value in the protected ENV used for the recorded 5f OEM-B boot.
     * This profile forces bootipq and abortboot(3), with CONFIG_PREBOOT absent.
     * Existing bootcmd/bootdelay strings therefore are not admission inputs. */
    static const char approved[]="console=ttyMSM0,115200n8 cnss2.bdf_integrated=0x10 cnss2.bdf_pci0=0x60 cnss2.bdf_pci1=0x60 cnss2.skip_radio_bmap=4 loglevel=3";
    uint32_t crc=0xffffffffu;unsigned args=0;
    if(!p||n!=0x80000u)return 0;
    for(size_t i=4;i<0x40000u;i++){
        crc^=p[i];for(unsigned j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&-(int32_t)(crc&1));
    }
    if(~crc!=word(p))return 0;
    for(size_t at=4;at<0x40000u;){
        size_t end=at;while(end<0x40000u&&p[end])end++;
        if(end==0x40000u)return 0;
        if(end==at)return args==1;
        if(end-at>=9&&!memcmp(p+at,"bootargs=",9)){
            if(++args!=1||end-at-9!=sizeof(approved)-1||memcmp(p+at+9,approved,sizeof(approved)-1))return 0;
        }
        at=end+1;
    }
    return 0;
}
static const bfi_d_candidate_record d_candidates[] = {
    { BFI_D_CANDIDATE_CANONICAL_5F, BFI_D_RECORD,
      BFI_D_CANDIDATE_SHA256,
      "7545523a9b52ce2cd469599a20804bcacdc6c602c97d5423c6433ec232505ff0",
      "1b371bc0129ab5d7337123c1ee50b8c3041f4b68acce5c55306dd82eac4367a1",
      0x140000u, 0x3000u, 628058u, 0x4a920000u, 0x4a9fd94cu, 4u, 1u },
    { BFI_D_CANDIDATE_PUBLIC_91E9_TRIAL, BFI_D_PUBLIC_RECORD,
      BFI_D_PUBLIC_CANDIDATE_SHA256, BFI_D_PUBLIC_MBN_SHA256,
      BFI_D_PUBLIC_LOAD_SHA256,
      0x140000u, 0x3000u, 628250u, 0x4a920000u, 0x4a9fda0cu, 4u, 0u }
};
static const bfi_candidate_record *const d_candidate_refs[]={
    &d_candidates[0],&d_candidates[1]
};

/* Public ABC payloads are exact offline candidates and remain controlled
 * trials. These values are derived from the reviewed descriptor JSON and
 * public candidate archive. In particular, B's unknown historical sample
 * JEDEC/SBL values stay unknown; official-package source identity is bound
 * separately by the build and is not inferred from this board descriptor. */
static const bfi_candidate_record abc_candidates[] = {
    { BFI_CANDIDATE_CONTROLLED_TRIAL, "hw-a-public-8ed8-controlled-trial-v1",
      "8ed8b32594d0ebbe3d5f23a8de1853b9ed3e7492c391361ffaf6f4cf7b1b4abb",
      "521218120228dea2d4ed9d6a6de2f46ab41897eef3ddc3c2240447017f931072",
      "8c9b2f1759d6f6330b0cf380522c5c479c3a548919b414b26c63ba6138aa1045",
      0x140000u, 0x3000u, 613058u, 0x4a920000u, 0x4a9fa3f0u, 4u, 0u },
    { BFI_CANDIDATE_CONTROLLED_TRIAL, "hw-b-public-9121-controlled-trial-v1",
      "912159169a8b61adb40e572b5366a21da68b65d48fcd47f113e65de44c908589",
      "3865053f17583406f5b67606333c76cd969a6eaff96f1314b7e3a470a2ad9e7b",
      "f5790f3279fbd607ac107ed63794ab846ac47c9eb82189579b34ea71b93c4a2e",
      0x140000u, 0x3000u, 620754u, 0x4a920000u, 0x4a9fc1d4u, 8u, 0u },
    { BFI_CANDIDATE_CONTROLLED_TRIAL, "hw-c-public-57d1-controlled-trial-v1",
      "57d13d1dad2a7b7854799b7cc69bddec638dd61df70f1b75a4fd94650fc96d8e",
      "52fa1ef5ec22b4ea00de2c31a760497907f6342e3b373cb596ac8b1d3f39cfc9",
      "001eaac09aee4d23b2a232465be4fd30ba0483b6c1a097ed58f9fead87192ed4",
      0x140000u, 0x3000u, 623346u, 0x4a920000u, 0x4a9fd3bcu, 4u, 0u }
};
static const bfi_candidate_record *const a_candidate_refs[]={&abc_candidates[0]};
static const bfi_candidate_record *const b_candidate_refs[]={&abc_candidates[1]};
static const bfi_candidate_record *const c_candidate_refs[]={&abc_candidates[2]};

static const bfi_candidate_profile candidate_profiles[] = {
    { BFI_BOARD_A, "hw-a-rc-a2-512-public-trial", "RC-A2-512 (one modified sample)",
      "CONFIG_TARGET_IPQ5018_H3C_HW_A=y",
      "d8235d9cef8f1814c8a52037447741132fe703a093ceb9fc96cf7aaf6bf86fa0",
      "ec228f32f8c6ec89007e2aeec4436dbb0f8d45e46d3a394421759525334571b8",
      "h3c,hw-a;qca,ipq5018", 0x08040001u, "W25N01GWZEIG",
      2u, 0xefu, 0xbau, 128u, 2048u, 131072u, 64u, 64u, 4u,
      BFI_D_SBL_SHA256, 178176u,
      "842f8853ec46e3f69c7ce4528001117b9c2c78864de254dc99dc617f59ea5e21",
      a_candidate_refs, 1u, 0u, 0u },
    /* Expected OEM Linux profile for C8:41: the NAND source table advertises
     * 128-byte spare and the OEM Linux NAND identification path exposes that
     * table size as MTD OOB. U-Boot's QPIC BCH8 path normalizes its own OOB to
     * 80 bytes; that is not a Linux runtime observation. Live MTD facts still
     * have to match this profile before qualification. */
    { BFI_BOARD_B, "hw-b-rt-b01-public-trial", "RT-B01 (one historical sample)",
      "CONFIG_TARGET_IPQ5018_H3C_RT3000_B=y",
      "5300cbdeea537f4318f92bc700c6c5a940740e3e41139e7398449298933dcd11",
      "d78a7a179ad80822ffcee96c7268546c3b5cf4fc9866120e65df3208c7efedf6",
      "h3c,rt3000-b;qca,ipq5018", 0x08040000u, "GD5F1GQ5REYIG",
      0u, 0u, 0u, 128u, 2048u, 131072u, 128u, 128u, 8u,
      NULL, 0u, NULL, b_candidate_refs, 1u, 0u, 0u },
    { BFI_BOARD_C, "hw-c-rt-c01-public-trial", "RT-C01 (one historical sample)",
      "CONFIG_TARGET_IPQ5018_H3C_HW_C=y",
      "e14fb7727e34bff79ffc08f94d3a51992716a6c9b63356db2600482b0cecdcee",
      "24a16c8d6ace90128f947ed86a035c66e1dc2125603cd19bf429bfdd3a3ec2c7",
      "h3c,hw-c;qca,ipq5018", 0x08040101u, "W25N01GW",
      2u, 0xefu, 0xbau, 128u, 2048u, 131072u, 64u, 64u, 4u,
      BFI_D_SBL_SHA256, 178176u,
      "842f8853ec46e3f69c7ce4528001117b9c2c78864de254dc99dc617f59ea5e21",
      c_candidate_refs, 1u, 0u, 0u },
    { BFI_BOARD_D, "hw-d-r012-efba-sp64-ecc4-first-install", "HW-D resistor-repair sample",
      "CONFIG_TARGET_IPQ5018_H3C_HW_D=y",
      "02f2249e94f83704bcfccf95504ce0b257d753b9af00b0b59c9070f9117134cd",
      "a94435f10095be986aea38f587ac6ff05858a3e3b6db22b95634584dd5bdd580",
      "h3c,nx30-d;qca,ipq5018", 0x08040000u, "W25N01GWZEIG",
      2u, 0xefu, 0xbau, 128u, 2048u, 131072u, 64u, 64u, 4u,
      BFI_D_SBL_SHA256, 178176u,
      "842f8853ec46e3f69c7ce4528001117b9c2c78864de254dc99dc617f59ea5e21",
      d_candidate_refs, 2u, 0u, 0u }
};

static const bfi_install_profile d_install_profile = {
    .profile_id="hw-d-r012-efba-sp64-ecc4-first-install",.model="W25N01GWZEIG",
    .manufacturer=0xefu,.chip_id=0xbau,.capacity_mib=128u,.raw_spare=64u,.mtd_oob=64u,.linux_ecc=4u,
    .page=2048u,.erase=131072u,.sbl_offset=0u,.sbl_partition_bytes=0x80000u,
    .sbl_sha256=BFI_D_SBL_SHA256,.sbl_bytes=178176u,
    .sbl_tail_ff=1u,.active_oem_sha256=BFI_D_OEM_SHA256,
    .bc_appsbl=1u,.bc_current_rootfs=0u,.bc_equal_valid=1u,
    .active_offset=0x640000u,.target_offset=0x500000u,.target_size=0x140000u,
    .startup_bytes=0x900000u,.bridge_offset=0x900000u,.bridge_bytes=0x2800000u,
    .oem_offset=0x3100000u,.oem_selector=1u,.oem_bytes=0x2800000u,
    .load_ecc_bits=4u,.storage_ecc_bits=4u,
    .candidates=d_candidate_refs,.candidate_count=sizeof(d_candidate_refs)/sizeof(d_candidate_refs[0])
};

#ifdef BFI_PROFILE_RECORD_HEADER
#define BFI_PROFILE_RECORD_IMPLEMENTATION 1
#include BFI_PROFILE_RECORD_HEADER
#undef BFI_PROFILE_RECORD_IMPLEMENTATION
#endif

static int valid_hash(const char *s)
{
    if(!s||strlen(s)!=64)return 0;
    for(size_t i=0;i<64;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;
    return 1;
}

static unsigned mtd_oob_mask_for_bytes(unsigned bytes)
{
    if(bytes==64u)return BFI_MTD_OOB_64_MASK;
    if(bytes==128u)return BFI_MTD_OOB_128_MASK;
    if(bytes==256u)return BFI_MTD_OOB_256_MASK;
    return 0u;
}

int bfi_board_profile_accepts_mtd_oob(const bfi_board_record *p,unsigned observed_oob)
{
    unsigned bit=mtd_oob_mask_for_bytes(observed_oob);
    if(!p||!bit)return 0;
    return p->accepted_mtd_oob_mask?
        ((p->accepted_mtd_oob_mask&~BFI_MTD_OOB_MASK_ALL)==0u&&
         (p->accepted_mtd_oob_mask&bit)!=0u&&
         (p->accepted_mtd_oob_mask&mtd_oob_mask_for_bytes(p->mtd_oob))!=0u):
        observed_oob==p->mtd_oob;
}

int bfi_install_profile_accepts_mtd_oob(const bfi_install_profile *p,unsigned observed_oob)
{
    unsigned bit=mtd_oob_mask_for_bytes(observed_oob);
    if(!p||!bit)return 0;
    return p->accepted_mtd_oob_mask?
        ((p->accepted_mtd_oob_mask&~BFI_MTD_OOB_MASK_ALL)==0u&&
         (p->accepted_mtd_oob_mask&bit)!=0u&&
         (p->accepted_mtd_oob_mask&mtd_oob_mask_for_bytes(p->mtd_oob))!=0u):
        observed_oob==p->mtd_oob;
}

static int candidate_descriptor_valid(const bfi_candidate_record *c)
{
    return c&&(c->id==BFI_CANDIDATE_NATIVE_QUALIFIED||c->id==BFI_CANDIDATE_CONTROLLED_TRIAL)&&
        c->name&&c->name[0]&&
        valid_hash(c->partition_sha256)&&valid_hash(c->mbn_sha256)&&valid_hash(c->load_sha256)&&
        c->partition_bytes&&c->load_offset<c->partition_bytes&&c->load_bytes&&
        c->load_bytes<=c->partition_bytes-c->load_offset&&c->entry&&c->bss_end>c->entry&&
        (c->payload_ecc_bits==4u||c->payload_ecc_bits==8u)&&c->native_qualified<=1u&&
        c->native_qualified==(unsigned)(c->id==BFI_CANDIDATE_NATIVE_QUALIFIED);
}

static int candidate_profile_valid(const bfi_candidate_profile *p)
{
    if(!p||p->board_class<BFI_BOARD_A||p->board_class>BFI_BOARD_D||
       !p->profile_id||!p->profile_id[0]||!p->sample_scope||!p->sample_scope[0]||
       !p->target||!p->target[0]||!valid_hash(p->defconfig_sha256)||
       !valid_hash(p->dts_sha256)||!p->dt_compatible||!p->dt_compatible[0]||
       !p->machid||!p->nand_model||!p->nand_model[0]||p->jedec_bytes_observed>2u||
       (p->jedec_bytes_observed==0u&&(p->manufacturer||p->chip_id))||
       (p->jedec_bytes_observed==1u&&(!p->manufacturer||p->chip_id))||
       (p->jedec_bytes_observed==2u&&(!p->manufacturer||!p->chip_id))||
       !p->capacity_mib||!p->page||!p->erase||p->erase%p->page||
       !p->raw_spare||!p->mtd_oob||p->mtd_oob>p->raw_spare||
       (p->observed_linux_ecc!=4u&&p->observed_linux_ecc!=8u)||
       (p->accepted_mtd_oob_mask&~BFI_MTD_OOB_MASK_ALL)!=0u||
       (p->accepted_mtd_oob_mask&&
        (!(p->accepted_mtd_oob_mask&mtd_oob_mask_for_bytes(p->mtd_oob))||
         ((p->accepted_mtd_oob_mask&BFI_MTD_OOB_128_MASK)&&p->raw_spare<128u)||
         ((p->accepted_mtd_oob_mask&BFI_MTD_OOB_256_MASK)&&p->raw_spare<256u)))||
       (p->candidate_load_ecc_bits&&p->candidate_load_ecc_bits!=4u&&
        p->candidate_load_ecc_bits!=8u)||
       p->candidate_count==0u||p->candidate_count>8u||!p->candidates)
        return 0;
    if((p->historical_sbl_sha256==NULL)!=(p->historical_sbl_valid_bytes==0u) ||
       (p->historical_sbl_sha256==NULL)!=(p->historical_sbl_extent_sha256==NULL))return 0;
    if(p->historical_sbl_sha256&&(!valid_hash(p->historical_sbl_sha256)||
       !valid_hash(p->historical_sbl_extent_sha256)))return 0;
    for(size_t i=0;i<p->candidate_count;i++){
        const bfi_candidate_record *c=p->candidates[i];
        if(!candidate_descriptor_valid(c)||
           (p->board_class!=BFI_BOARD_D&&c->id!=BFI_CANDIDATE_CONTROLLED_TRIAL)||
           c->payload_ecc_bits!=(p->candidate_load_ecc_bits?
               p->candidate_load_ecc_bits:p->observed_linux_ecc))return 0;
        for(size_t j=0;j<i;j++)if(c==p->candidates[j]||
            !strcmp(c->partition_sha256,p->candidates[j]->partition_sha256))return 0;
    }
    return 1;
}

static int range_valid(uint64_t offset,uint64_t bytes,uint64_t capacity,uint64_t erase)
{
    return bytes&&offset%erase==0&&bytes%erase==0&&offset<=capacity&&bytes<=capacity-offset;
}

static int ranges_overlap(uint64_t a,uint64_t an,uint64_t b,uint64_t bn)
{
    return a < b+bn && b < a+an;
}

const bfi_candidate_profile *bfi_candidate_profile_select(bfi_board_class board_class)
{
#ifdef BFI_PROFILE_RECORD_HEADER
    if(board_class!=BFI_GENERATED_BOARD_CLASS)return NULL;
    return candidate_profile_valid(&bfi_generated_profile)?&bfi_generated_profile:NULL;
#else
    if(board_class<BFI_BOARD_A||board_class>BFI_BOARD_D)return NULL;
    const bfi_candidate_profile *p=&candidate_profiles[(unsigned)board_class-1u];
    return candidate_profile_valid(p)?p:NULL;
#endif
}

static bfi_board_class board_class_from_string(const char *s)
{
    if(!s||s[1]!='\0')return BFI_BOARD_UNSPECIFIED;
    if(s[0]=='A')return BFI_BOARD_A;
    if(s[0]=='B')return BFI_BOARD_B;
    if(s[0]=='C')return BFI_BOARD_C;
    if(s[0]=='D')return BFI_BOARD_D;
    return BFI_BOARD_UNSPECIFIED;
}

const bfi_board_record *bfi_board_candidate_record(const char *board_class)
{
    return bfi_candidate_profile_select(board_class_from_string(board_class));
}

const bfi_candidate_profile *bfi_board_profile(const char *board_class)
{
    return bfi_board_candidate_record(board_class);
}

int bfi_candidate_profile_accepts(const bfi_candidate_profile *p,
                                  const bfi_candidate_record *candidate)
{
    if(!candidate_profile_valid(p)||!candidate_descriptor_valid(candidate))return 0;
    for(size_t i=0;i<p->candidate_count;i++)if(p->candidates[i]==candidate)return 1;
    return 0;
}

static const bfi_candidate_record *candidate_select_from(
    const bfi_candidate_profile *p,const uint8_t *bytes,size_t n)
{
    if(!candidate_profile_valid(p)||!bytes||n!=0x140000u)return NULL;
    for(size_t i=0;i<p->candidate_count;i++){
        const bfi_candidate_record *r=p->candidates[i];
        if(hash_is(bytes,n,r->partition_sha256)&&
           bfi_candidate_profile_accepts(p,r)){
            int fresh_profile=0;
#ifdef BFI_PROFILE_RECORD_HEADER
            fresh_profile=(p==&bfi_generated_profile);
#endif
            unsigned elf_type=(unsigned)bytes[16]|((unsigned)bytes[17]<<8);
            uint32_t elf_flags=word(bytes+36);
            int paired_header=(elf_type==3u&&elf_flags==0x05000202u)||
                (fresh_profile&&elf_type==2u&&elf_flags==0x05000200u);
            /* Check the exact MBN container, LOAD region, entry and erased
             * partition tail against this digest-bound candidate descriptor. */
            if(memcmp(bytes,"\177ELF\1\1\1",7)||!paired_header||
               word(bytes+24)!=r->entry||
               word(bytes+28)!=52u||bytes[42]!=32u||bytes[43]||bytes[44]!=4u||bytes[45]||
               word(bytes+52+64)!=1u||word(bytes+52+68)!=r->load_offset||
               word(bytes+52+72)!=r->entry||word(bytes+52+76)!=r->entry||
               word(bytes+52+80)!=r->load_bytes||word(bytes+0x1000)!=21u||
               word(bytes+0x1004)!=3u)return NULL;
            for(size_t j=(size_t)r->load_offset+r->load_bytes;j<n;j++)
                if(bytes[j]!=0xff)return NULL;
            if(!hash_is(bytes,(size_t)r->load_offset+r->load_bytes,r->mbn_sha256)||
               !hash_is(bytes+r->load_offset,r->load_bytes,r->load_sha256))return NULL;
            return r;
        }
    }
    return NULL;
}

const bfi_candidate_record *bfi_candidate_select_for_profile(
    const bfi_candidate_profile *p,const uint8_t *bytes,size_t n)
{
    return candidate_select_from(p,bytes,n);
}

const bfi_candidate_record *bfi_board_candidate_select(
    const char *board_class,const uint8_t *bytes,size_t n)
{
    return bfi_candidate_select_for_profile(bfi_board_candidate_record(board_class),bytes,n);
}

static int profile_descriptor_valid(const bfi_install_profile *p)
{
    if(!p||!p->profile_id||!p->profile_id[0]||!p->model||!p->model[0]||
       !valid_hash(p->sbl_sha256)||!valid_hash(p->active_oem_sha256)||
       !p->manufacturer||p->manufacturer>255u||!p->chip_id||p->chip_id>255u||
       !p->capacity_mib||!p->raw_spare||!p->mtd_oob||p->mtd_oob>p->raw_spare||!p->page||!p->erase||p->erase%p->page||
       (p->linux_ecc!=4u&&p->linux_ecc!=8u)||(p->load_ecc_bits!=4u&&p->load_ecc_bits!=8u)||
       (p->storage_ecc_bits!=4u&&p->storage_ecc_bits!=8u)||p->sbl_tail_ff>1u||
       (p->accepted_mtd_oob_mask&~BFI_MTD_OOB_MASK_ALL)!=0u||
       (p->accepted_mtd_oob_mask&&
        (!(p->accepted_mtd_oob_mask&mtd_oob_mask_for_bytes(p->mtd_oob))||
         ((p->accepted_mtd_oob_mask&BFI_MTD_OOB_128_MASK)&&p->raw_spare<128u)||
         ((p->accepted_mtd_oob_mask&BFI_MTD_OOB_256_MASK)&&p->raw_spare<256u)))||
       !p->sbl_partition_bytes||!p->sbl_bytes||p->sbl_bytes>=p->sbl_partition_bytes||p->bc_appsbl>1u||p->bc_current_rootfs>1u||
       p->bc_equal_valid!=1u||!p->target_size||
       !p->startup_bytes||!p->bridge_offset||!p->bridge_bytes||!p->oem_offset||
       p->oem_selector>1u||!p->oem_bytes||p->candidate_count==0||p->candidate_count>8u||!p->candidates)
        return 0;
    uint64_t capacity=(uint64_t)p->capacity_mib*1024ULL*1024ULL;
    if(!range_valid(0,p->startup_bytes,capacity,p->erase)||
       !range_valid(p->sbl_offset,p->sbl_partition_bytes,capacity,p->erase)||
       !range_valid(p->active_offset,p->target_size,capacity,p->erase)||
       !range_valid(p->target_offset,p->target_size,capacity,p->erase)||
       !range_valid(p->bridge_offset,p->bridge_bytes,capacity,p->erase)||
       !range_valid(p->oem_offset,p->oem_bytes,capacity,p->erase)||
       p->active_offset%p->page||p->target_offset%p->page||p->bridge_offset%p->page||
       p->oem_offset%p->page||p->sbl_offset%p->page||p->sbl_partition_bytes%p->page||p->startup_bytes%p->page||
       (uint64_t)p->sbl_offset+p->sbl_partition_bytes>p->startup_bytes||
       (uint64_t)p->active_offset+p->target_size>p->startup_bytes||
       (uint64_t)p->target_offset+p->target_size>p->startup_bytes||
       ranges_overlap(p->active_offset,p->target_size,p->target_offset,p->target_size)||
       ranges_overlap(p->sbl_offset,p->sbl_partition_bytes,p->active_offset,p->target_size)||
       ranges_overlap(p->sbl_offset,p->sbl_partition_bytes,p->target_offset,p->target_size)||
       ranges_overlap(p->sbl_offset,p->sbl_partition_bytes,p->bridge_offset,p->bridge_bytes)||
       ranges_overlap(p->sbl_offset,p->sbl_partition_bytes,p->oem_offset,p->oem_bytes)||
       ranges_overlap(0,p->startup_bytes,p->bridge_offset,p->bridge_bytes)||
       ranges_overlap(0,p->startup_bytes,p->oem_offset,p->oem_bytes)||
       ranges_overlap(p->active_offset,p->target_size,p->bridge_offset,p->bridge_bytes)||
       ranges_overlap(p->active_offset,p->target_size,p->oem_offset,p->oem_bytes)||
       ranges_overlap(p->target_offset,p->target_size,p->bridge_offset,p->bridge_bytes)||
       ranges_overlap(p->target_offset,p->target_size,p->oem_offset,p->oem_bytes)||
       ranges_overlap(p->bridge_offset,p->bridge_bytes,p->oem_offset,p->oem_bytes)||
       p->sbl_bytes>=0x80000u)return 0;
    for(size_t i=0;i<p->candidate_count;i++){
        const bfi_candidate_record *c=p->candidates[i];
        if(!candidate_descriptor_valid(c)||c->partition_bytes!=p->target_size||
           c->payload_ecc_bits!=p->load_ecc_bits)return 0;
        for(size_t j=0;j<i;j++)if(c==p->candidates[j]||
            !strcmp(c->name,p->candidates[j]->name)||
            !strcmp(c->partition_sha256,p->candidates[j]->partition_sha256))return 0;
    }
    return 1;
}

const char *bfi_match_install_profile(const bfi_install_profile *p,
                                      const bfi_candidate_record *c,const bfi_facts *f)
{
    if(!profile_descriptor_valid(p))return "profile";
    if(!candidate_descriptor_valid(c))return "candidate descriptor";
    int bound=0;for(size_t i=0;i<p->candidate_count;i++)if(p->candidates[i]==c){bound=1;break;}
    if(!bound)return "candidate binding";
    if(!f||!memchr(f->boot_before,0,sizeof(f->boot_before))||
       !memchr(f->boot_after,0,sizeof(f->boot_after))||
       strlen(f->boot_before)<16||strcmp(f->boot_before,f->boot_after))return "session";
    if(!memchr(f->model,0,sizeof(f->model))||strcmp(f->model,p->model)||
       f->manufacturer!=p->manufacturer||f->chip_id!=p->chip_id||
       f->capacity_mib!=p->capacity_mib||f->raw_spare!=p->raw_spare||
       f->linux_ecc!=p->linux_ecc||f->page!=p->page||f->erase!=p->erase||
       !bfi_install_profile_accepts_mtd_oob(p,f->observed_mtd_oob))return "NAND profile";
    if(!memchr(f->sbl_sha256,0,sizeof(f->sbl_sha256))||
       strcmp(f->sbl_sha256,p->sbl_sha256)||f->sbl_bytes!=p->sbl_bytes||
       f->sbl_tail_ff!=p->sbl_tail_ff)return "SBL";
    if(f->declared_load_ecc!=p->load_ecc_bits||f->declared_storage_ecc!=p->storage_ecc_bits)return "candidate ECC";
    if(f->bc_equal_valid!=p->bc_equal_valid||f->bc_appsbl!=p->bc_appsbl||
       f->bc_current_rootfs!=p->bc_current_rootfs||f->active_offset!=p->active_offset||
       f->target_offset!=p->target_offset||f->target_size!=p->target_size)return "BC/target";
    if(!memchr(f->active_sha256,0,sizeof(f->active_sha256))||
       strcmp(f->active_sha256,p->active_oem_sha256))return "active OEM";
    if(f->bridge_and_oem_verified!=1||f->bridge_offset!=p->bridge_offset||
       f->bridge_bytes!=p->bridge_bytes||
       f->oem_offset!=p->oem_offset||f->oem_selector!=p->oem_selector||
       f->oem_bytes!=p->oem_bytes)return "bridge/OEM slots";
    if(f->route_contract_verified!=1||f->unsigned_boot_allowed!=1||f->env_bootargs_verified!=1)return "route/auth/bootargs";
    if(!f->pages.observation_complete||!f->pages.binding_valid)return "page observation";
    /* Corrected bitflips are what ECC is for; ECCGETSTATS counts them
     * cumulatively since boot, so one weak bit would block a board on every
     * boot. Only uncorrectable reads or missing counters refuse the install. */
    for(unsigned i=0;i<2;i++){
        const struct bmo_partition_report *r=&f->pages.appsbl[i];
        if(!r->observation_complete||!r->binding_valid||
           r->pages_read!=p->target_size/p->page||r->covered_bytes!=p->target_size||
           r->eraseblocks_checked!=p->target_size/p->erase||
           !bfi_install_profile_accepts_mtd_oob(p,r->observed_oob_bytes)||
           r->ecc_stats_state!=BMO_ECC_STATS_SUPPORTED||
           r->ecc_uncorrected_before||r->ecc_uncorrected_after||r->badblocks)return "page observation";
    }
    return NULL;
}

static int same_profile_contract_except_direction(const bfi_install_profile *a,
                                                    const bfi_install_profile *b)
{
    return a&&b&&a->profile_id&&b->profile_id&&a->profile_id[0]&&b->profile_id[0]&&
        strcmp(a->profile_id,b->profile_id)&&a->model&&b->model&&a->sbl_sha256&&b->sbl_sha256&&
        a->active_oem_sha256&&b->active_oem_sha256&&
        !strcmp(a->model,b->model)&&a->manufacturer==b->manufacturer&&
        a->chip_id==b->chip_id&&a->capacity_mib==b->capacity_mib&&
        a->raw_spare==b->raw_spare&&a->mtd_oob==b->mtd_oob&&
        a->linux_ecc==b->linux_ecc&&a->page==b->page&&a->erase==b->erase&&
        a->sbl_offset==b->sbl_offset&&a->sbl_partition_bytes==b->sbl_partition_bytes&&
        !strcmp(a->sbl_sha256,b->sbl_sha256)&&a->sbl_bytes==b->sbl_bytes&&
        a->sbl_tail_ff==b->sbl_tail_ff&&
        !strcmp(a->active_oem_sha256,b->active_oem_sha256)&&
        a->bc_equal_valid==b->bc_equal_valid&&
        a->target_size==b->target_size&&a->startup_bytes==b->startup_bytes&&
        a->bridge_bytes==b->bridge_bytes&&a->oem_bytes==b->oem_bytes&&
        a->load_ecc_bits==b->load_ecc_bits&&a->storage_ecc_bits==b->storage_ecc_bits&&
        a->candidates==b->candidates&&a->candidate_count==b->candidate_count&&
        a->accepted_mtd_oob_mask==b->accepted_mtd_oob_mask;
}

const bfi_install_profile *bfi_select_install_profile(
    const bfi_install_profile *profiles,size_t count,unsigned bridge_offset,
    unsigned oem_offset,unsigned oem_selector,unsigned current_root_selector,
    unsigned current_appsbl_selector)
{
    if(!profiles||count!=4||current_root_selector>1||current_appsbl_selector>1)return NULL;
    const bfi_install_profile *base=&profiles[0],*selected=NULL;
    for(size_t i=0;i<count;i++){
        const bfi_install_profile *p=&profiles[i];
        unsigned app=i<2?1u:0u,root=(unsigned)i%2u;
        unsigned active=app?0x640000u:0x500000u,target=app?0x500000u:0x640000u;
        unsigned bridge=root?0x3100000u:0x900000u,oem=root?0x900000u:0x3100000u;
        if((i&&!same_profile_contract_except_direction(base,p))||
           !p->profile_id||!p->profile_id[0]||
           ((!p->candidates)!=(p->candidate_count==0))||p->candidate_count>8u||
           p->active_offset!=active||p->target_offset!=target||
           p->target_size!=0x140000u||p->startup_bytes!=0x900000u||
           p->bc_appsbl!=app||p->bc_current_rootfs!=root||p->bc_equal_valid!=1u||
           p->bridge_offset!=bridge||p->bridge_bytes!=0x2800000u||
           p->oem_offset!=oem||p->oem_bytes!=0x2800000u||p->oem_selector!=1u-root)return NULL;
        for(size_t j=0;j<i;j++)if(!strcmp(p->profile_id,profiles[j].profile_id))return NULL;
        for(size_t j=0;j<p->candidate_count;j++)
            if(!candidate_descriptor_valid(p->candidates[j])||p->candidates[j]!=base->candidates[j])return NULL;
        if(bridge_offset==p->bridge_offset&&oem_offset==p->oem_offset&&
           oem_selector==p->oem_selector&&current_root_selector==root&&current_appsbl_selector==app){
            if(selected)return NULL;
            selected=p;
        }
    }
    return selected;
}

const bfi_install_profile *bfi_d_install_profile(void)
{
    return &d_install_profile;
}

int bfi_d_candidate_format(const uint8_t *p,size_t n)
{
    const bfi_d_candidate_record *r=bfi_d_candidate_select(p,n);
    return r&&r->id==BFI_D_CANDIDATE_CANONICAL_5F;
}

const bfi_d_candidate_record *bfi_d_candidate_select(const uint8_t *p,size_t n)
{
    /* Frozen D APIs retain their original record semantics even in ABI2
     * builds whose generic profile is a freshly compiled candidate. */
    return candidate_select_from(&candidate_profiles[3],p,n);
}

const char *bfi_check_d_first_install(const bfi_facts *f,const uint8_t *p,size_t n)
{
    const bfi_d_candidate_record *selected=NULL;
    if(!bfi_d_candidate_format(p,n))return "candidate";
    return bfi_check_d_first_install_record(f,p,n,&selected);
}

const char *bfi_check_d_first_install_record(const bfi_facts *f,const uint8_t *p,size_t n,
                                             const bfi_d_candidate_record **selected)
{
    const bfi_d_candidate_record *r=bfi_d_candidate_select(p,n);
    if(selected)*selected=NULL;
    if(!r)return "candidate";
    if(!f)return "session";
    bfi_facts d_facts=*f;
    /* Legacy D caller passes the profile-specific route result; normalize it
     * into the profile-neutral facts field before invoking the generic core. */
    d_facts.route_contract_verified=f->r012_route_verified;
    const char *reason=bfi_match_install_profile(&d_install_profile,r,&d_facts);
    if(reason)return reason;
    if(selected)*selected=r;
    return NULL;
}
