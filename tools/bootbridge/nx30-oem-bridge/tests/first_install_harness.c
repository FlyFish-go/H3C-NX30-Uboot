/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bridge_first_install.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bfi_facts valid_facts(void){
    bfi_facts f;memset(&f,0,sizeof(f));
    strcpy(f.boot_before,"00000000-0000-4000-8000-000000000001");strcpy(f.boot_after,f.boot_before);
    strcpy(f.model,"W25N01GWZEIG");strcpy(f.sbl_sha256,BFI_D_SBL_SHA256);strcpy(f.active_sha256,BFI_D_OEM_SHA256);
    f.manufacturer=0xef;f.chip_id=0xba;f.capacity_mib=128;f.raw_spare=64;f.linux_ecc=4;
    f.page=2048;f.erase=131072;f.sbl_bytes=178176;f.sbl_tail_ff=1;
    f.active_offset=0x640000;f.target_offset=0x500000;f.target_size=0x140000;
    f.bc_appsbl=1;f.bc_current_rootfs=0;f.bc_equal_valid=1;
    f.bridge_offset=0x900000;f.bridge_bytes=0x2800000;f.oem_offset=0x3100000;f.oem_selector=1;f.oem_bytes=0x2800000;
    f.bridge_and_oem_verified=1;f.r012_route_verified=1;f.unsigned_boot_allowed=1;
    f.declared_load_ecc=4;f.declared_storage_ecc=4;f.env_bootargs_verified=1;
    f.pages.observation_complete=1;f.pages.binding_valid=1;f.pages.clean=1;
    for(unsigned i=0;i<2;i++){
        struct bmo_partition_report*r=&f.pages.appsbl[i];
        r->observation_complete=1;r->binding_valid=1;r->clean=1;r->pages_read=640;
        r->covered_bytes=0x140000;r->eraseblocks_checked=10;r->observed_oob_bytes=64;
        r->ecc_stats_state=BMO_ECC_STATS_SUPPORTED;
    }return f;
}
static bfi_facts profile_facts(const bfi_install_profile *p){
    bfi_facts f;memset(&f,0,sizeof(f));
    strcpy(f.boot_before,"11111111-1111-4111-8111-111111111111");strcpy(f.boot_after,f.boot_before);
    strcpy(f.model,p->model);strcpy(f.sbl_sha256,p->sbl_sha256);strcpy(f.active_sha256,p->active_oem_sha256);
    f.manufacturer=p->manufacturer;f.chip_id=p->chip_id;f.capacity_mib=p->capacity_mib;
    f.raw_spare=p->raw_spare;f.linux_ecc=p->linux_ecc;f.page=p->page;f.erase=p->erase;
    f.observed_mtd_oob=p->mtd_oob;
    f.sbl_bytes=p->sbl_bytes;f.sbl_tail_ff=p->sbl_tail_ff;
    f.active_offset=p->active_offset;f.target_offset=p->target_offset;f.target_size=p->target_size;
    f.bc_appsbl=p->bc_appsbl;f.bc_current_rootfs=p->bc_current_rootfs;f.bc_equal_valid=p->bc_equal_valid;
    f.bridge_offset=p->bridge_offset;f.bridge_bytes=p->bridge_bytes;f.oem_offset=p->oem_offset;f.oem_selector=p->oem_selector;f.oem_bytes=p->oem_bytes;
    f.bridge_and_oem_verified=1;f.route_contract_verified=1;f.unsigned_boot_allowed=1;
    f.declared_load_ecc=p->load_ecc_bits;f.declared_storage_ecc=p->storage_ecc_bits;f.env_bootargs_verified=1;
    f.pages.observation_complete=1;f.pages.binding_valid=1;f.pages.clean=1;
    for(unsigned i=0;i<2;i++){
        struct bmo_partition_report *r=&f.pages.appsbl[i];
        r->observation_complete=1;r->binding_valid=1;r->clean=1;
        r->pages_read=p->target_size/p->page;r->covered_bytes=p->target_size;
        r->eraseblocks_checked=p->target_size/p->erase;r->observed_oob_bytes=p->mtd_oob;
        r->ecc_stats_state=BMO_ECC_STATS_SUPPORTED;
    }
    return f;
}
static int test_generic_profiles(void){
    static const bfi_candidate_record ac={BFI_D_CANDIDATE_CANONICAL_5F,"synthetic-a-candidate",
        "3333333333333333333333333333333333333333333333333333333333333333",
        "4444444444444444444444444444444444444444444444444444444444444444",
        "5555555555555555555555555555555555555555555555555555555555555555",
        0x140000u,0x3000u,0x10000u,0x100000u,0x120000u,4u,1u};
    static const bfi_candidate_record bc={BFI_D_CANDIDATE_PUBLIC_91E9_TRIAL,"synthetic-b-ecc8-candidate",
        "6666666666666666666666666666666666666666666666666666666666666666",
        "7777777777777777777777777777777777777777777777777777777777777777",
        "8888888888888888888888888888888888888888888888888888888888888888",
        0x140000u,0x3000u,0x10000u,0x100000u,0x120000u,8u,0u};
    static const bfi_candidate_record *const a_list[]={&ac};
    static const bfi_candidate_record *const b_list[]={&bc};
    static const bfi_install_profile a={
        .profile_id="synthetic-a-like",.model="W25N01GWZEIG",.manufacturer=0xef,.chip_id=0xba,
        .capacity_mib=128,.raw_spare=64,.linux_ecc=4,.page=2048,.erase=131072,
        .mtd_oob=64,
        .sbl_offset=0,.sbl_partition_bytes=0x80000,
        .sbl_sha256="1111111111111111111111111111111111111111111111111111111111111111",
        .sbl_bytes=178176,.sbl_tail_ff=1,
        .active_oem_sha256="2222222222222222222222222222222222222222222222222222222222222222",
        .bc_appsbl=0,.bc_current_rootfs=1,.bc_equal_valid=1,
        .active_offset=0x500000,.target_offset=0x640000,.target_size=0x140000,
        .startup_bytes=0x900000,.bridge_offset=0x900000,.bridge_bytes=0x2800000,
        .oem_offset=0x3100000,.oem_selector=1,.oem_bytes=0x2800000,
        .load_ecc_bits=4,.storage_ecc_bits=4,.candidates=a_list,.candidate_count=1};
    static const bfi_install_profile b={
        .profile_id="synthetic-b-like-ecc8",.model="synthetic-GD5F1GQ5REYIG-like",.manufacturer=0xaa,.chip_id=0xbb,
        /* OEM Linux B profile exposes the table's 128-byte spare as MTD OOB;
         * U-Boot's QPIC BCH8 OOB=80 is a distinct observation path. */
        .capacity_mib=128,.raw_spare=128,.linux_ecc=8,.page=2048,.erase=131072,
        .mtd_oob=128,
        .sbl_offset=0,.sbl_partition_bytes=0x80000,
        .sbl_sha256="9999999999999999999999999999999999999999999999999999999999999999",
        .sbl_bytes=180000,.sbl_tail_ff=1,
        .active_oem_sha256="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        .bc_appsbl=1,.bc_current_rootfs=0,.bc_equal_valid=1,
        .active_offset=0x640000,.target_offset=0x500000,.target_size=0x140000,
        .startup_bytes=0x900000,.bridge_offset=0x900000,.bridge_bytes=0x2800000,
        .oem_offset=0x3100000,.oem_selector=0,.oem_bytes=0x2800000,
        .load_ecc_bits=8,.storage_ecc_bits=8,.candidates=b_list,.candidate_count=1};
    bfi_facts fa=profile_facts(&a),fb=profile_facts(&b),bad;
    unsigned cases=0;
    const char *why=bfi_match_install_profile(&a,&ac,&fa);if(why)fprintf(stderr,"synthetic A profile: %s\n",why);assert(!why);cases++;
    why=bfi_match_install_profile(&b,&bc,&fb);if(why)fprintf(stderr,"synthetic B profile: %s\n",why);assert(!why);cases++;
    bad=fb;bad.linux_ecc=4;assert(bfi_match_install_profile(&b,&bc,&bad));cases++;
    bad=fb;bad.declared_load_ecc=4;assert(bfi_match_install_profile(&b,&bc,&bad));cases++;
    bad=fb;bad.raw_spare=64;assert(bfi_match_install_profile(&b,&bc,&bad));cases++;
    bad=fb;bad.pages.appsbl[0].observed_oob_bytes=80;assert(bfi_match_install_profile(&b,&bc,&bad));cases++;
    bad=fa;bad.bridge_bytes=0x2000000;assert(bfi_match_install_profile(&a,&ac,&bad));cases++;
    bad=fa;bad.target_offset=0x500000;assert(bfi_match_install_profile(&a,&ac,&bad));cases++;
    bfi_candidate_record wrong=ac;assert(bfi_match_install_profile(&a,&wrong,&fa));cases++;
    bfi_install_profile invalid=a;invalid.candidate_count=0;assert(bfi_match_install_profile(&invalid,&ac,&fa));cases++;
    invalid=b;invalid.raw_spare=80;invalid.mtd_oob=128; /* historical reversed fixture */
    assert(bfi_match_install_profile(&invalid,&bc,&fb));cases++;
    invalid=a;invalid.bridge_offset=0x7f00000;assert(bfi_match_install_profile(&invalid,&ac,&fa));cases++;
    invalid=a;invalid.oem_offset=0x7f00000;assert(bfi_match_install_profile(&invalid,&ac,&fa));cases++;
    invalid=a;invalid.active_offset+=0x1000;assert(bfi_match_install_profile(&invalid,&ac,&fa));cases++;
    invalid=a;invalid.target_offset+=0x1000;assert(bfi_match_install_profile(&invalid,&ac,&fa));cases++;
    invalid=a;invalid.bridge_offset+=0x1000;assert(bfi_match_install_profile(&invalid,&ac,&fa));cases++;
    invalid=a;invalid.oem_offset+=0x1000;assert(bfi_match_install_profile(&invalid,&ac,&fa));cases++;
    invalid=a;invalid.target_offset=0x600000; /* erase-aligned partial overlap */
    assert(bfi_match_install_profile(&invalid,&ac,&fa));cases++;
    invalid=a;invalid.oem_offset=invalid.bridge_offset+invalid.bridge_bytes-invalid.erase;
    assert(bfi_match_install_profile(&invalid,&ac,&fa));cases++;
    invalid=a;invalid.bridge_offset=0x500000; /* bridge crosses startup/APPSBL range */
    assert(bfi_match_install_profile(&invalid,&ac,&fa));cases++;
    invalid=a;invalid.startup_bytes=0x700000; /* startup boundary cuts through active APPSBL */
    assert(bfi_match_install_profile(&invalid,&ac,&fa));cases++;
    bfi_candidate_record mismatched=ac;mismatched.native_qualified=0;
    const bfi_candidate_record *const mismatched_list[]={&mismatched};
    invalid=a;invalid.candidates=mismatched_list;
    assert(bfi_match_install_profile(&invalid,&mismatched,&fa));cases++;
    return (int)cases;
}
static int test_install_directions(void){
    const bfi_install_profile *base=bfi_d_install_profile();
    bfi_install_profile profiles[4],bad[4];
    const char *names[]={"app1-root0","app1-root1","app0-root0","app0-root1"};
    unsigned cases=0;
    assert(base);
    for(unsigned i=0;i<4;i++){
        profiles[i]=*base;unsigned app=i<2?1u:0u,root=i%2;
        profiles[i].profile_id=names[i];profiles[i].candidates=NULL;profiles[i].candidate_count=0;
        profiles[i].bc_appsbl=app;profiles[i].bc_current_rootfs=root;
        profiles[i].active_offset=app?0x640000u:0x500000u;
        profiles[i].target_offset=app?0x500000u:0x640000u;
        profiles[i].bridge_offset=root?0x3100000u:0x900000u;
        profiles[i].oem_offset=root?0x900000u:0x3100000u;profiles[i].oem_selector=1-root;
    }
    for(unsigned i=0;i<4;i++){
        const bfi_install_profile *p=&profiles[i];
        assert(bfi_select_install_profile(profiles,4,p->bridge_offset,p->oem_offset,p->oem_selector,
            p->bc_current_rootfs,p->bc_appsbl)==p);cases++;
        assert(!bfi_select_install_profile(profiles,4,p->bridge_offset,p->oem_offset,1-p->oem_selector,
            p->bc_current_rootfs,p->bc_appsbl));cases++;
        assert(!bfi_select_install_profile(profiles,4,p->bridge_offset,p->oem_offset,p->oem_selector,
            1-p->bc_current_rootfs,p->bc_appsbl));cases++;
        assert(!bfi_select_install_profile(profiles,4,p->bridge_offset,p->oem_offset,p->oem_selector,
            p->bc_current_rootfs,2));cases++;
    }
    for(unsigned count=0;count<4;count++){
        assert(!bfi_select_install_profile(profiles,count,0x3100000u,0x900000u,0,1,0));cases++;
    }
    memcpy(bad,profiles,sizeof(bad));bad[3].target_offset=bad[3].active_offset;
    assert(!bfi_select_install_profile(bad,4,0x3100000u,0x900000u,0,1,0));cases++;
    memcpy(bad,profiles,sizeof(bad));bad[3].active_oem_sha256="1111111111111111111111111111111111111111111111111111111111111111";
    assert(!bfi_select_install_profile(bad,4,0x3100000u,0x900000u,0,1,0));cases++;
    memcpy(bad,profiles,sizeof(bad));bad[3].bridge_offset=0x900000u;
    assert(!bfi_select_install_profile(bad,4,0x3100000u,0x900000u,0,1,0));cases++;
    memcpy(bad,profiles,sizeof(bad));bad[3].profile_id=bad[0].profile_id;
    assert(!bfi_select_install_profile(bad,4,0x3100000u,0x900000u,0,1,0));cases++;
    return (int)cases;
}
static int test_board_profiles(void){
    const bfi_board_record *a=bfi_board_candidate_record("A");
    const bfi_board_record *b=bfi_board_candidate_record("B");
    const bfi_board_record *c=bfi_board_candidate_record("C");
    const bfi_board_record *d=bfi_board_candidate_record("D");
    assert(a&&b&&c&&d);assert(!bfi_board_candidate_record(NULL));
    assert(!bfi_board_candidate_record(""));assert(!bfi_board_candidate_record("unknown"));
    assert(a->board_class==BFI_BOARD_A&&a->machid==0x08040001u&&a->candidate_count==1);
    assert(b->board_class==BFI_BOARD_B&&b->machid==0x08040000u&&b->candidate_count==1);
    assert(b->jedec_bytes_observed==0&&b->manufacturer==0&&b->chip_id==0);
    /* Source profile says physical spare=128; Linux/QPIC observation is OOB=80. */
    assert(b->raw_spare==128&&b->mtd_oob==128&&b->observed_linux_ecc==8);
    assert(c->board_class==BFI_BOARD_C&&c->machid==0x08040101u&&c->candidate_count==1);
    assert(a->historical_sbl_sha256&&c->historical_sbl_sha256);
    assert(!b->historical_sbl_sha256&&!b->historical_sbl_valid_bytes);
    assert(a->candidates[0]->id==BFI_CANDIDATE_CONTROLLED_TRIAL&&!a->candidates[0]->native_qualified);
    assert(b->candidates[0]->id==BFI_CANDIDATE_CONTROLLED_TRIAL&&!b->candidates[0]->native_qualified);
    assert(c->candidates[0]->id==BFI_CANDIDATE_CONTROLLED_TRIAL&&!c->candidates[0]->native_qualified);
    assert(bfi_candidate_profile_accepts(a,a->candidates[0]));
    assert(!bfi_candidate_profile_accepts(a,b->candidates[0]));
    assert(bfi_candidate_profile_select(BFI_BOARD_UNSPECIFIED)==NULL);
    return 20;
}
static void env_crc(uint8_t *p){
    uint32_t c=0xffffffffu;for(size_t i=4;i<0x40000;i++){c^=p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(int32_t)(c&1));}c=~c;
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(c>>(8*i));
}
static void test_env(void){
    const char text[]="bootargs=console=ttyMSM0,115200n8 cnss2.bdf_integrated=0x10 cnss2.bdf_pci0=0x60 cnss2.bdf_pci1=0x60 cnss2.skip_radio_bmap=4 loglevel=3";
    uint8_t*p=calloc(1,0x80000);assert(p);env_crc(p);assert(!bfi_d_env_bootargs(p,0x80000));
    memcpy(p+4,text,sizeof(text));env_crc(p);assert(bfi_d_env_bootargs(p,0x80000));
    p[0]^=1;assert(!bfi_d_env_bootargs(p,0x80000));p[0]^=1;
    p[40]='X';env_crc(p);assert(!bfi_d_env_bootargs(p,0x80000));memcpy(p+4,text,sizeof(text));
    memcpy(p+4+sizeof(text),text,sizeof(text));env_crc(p);assert(!bfi_d_env_bootargs(p,0x80000));
    assert(!bfi_d_env_bootargs(p,0x40000));free(p);
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--generic-only")){
        unsigned generic_cases=(unsigned)test_generic_profiles();assert(generic_cases==22);
        unsigned direction_cases=(unsigned)test_install_directions();assert(direction_cases==24);
        unsigned profiles=(unsigned)test_board_profiles();
        printf("PASS %u generic-profile, %u direction and %u board-profile cases (software fixtures, not hardware)\n",generic_cases,direction_cases,profiles);return 0;
    }
    test_env();
    unsigned generic_cases=(unsigned)test_generic_profiles();assert(generic_cases==22);
    unsigned direction_cases=(unsigned)test_install_directions();assert(direction_cases==24);
    unsigned board_profile_cases=(unsigned)test_board_profiles();
    const size_t n=0x140000;uint8_t*p=malloc(n);assert(p);
    if(argc==4&&!strcmp(argv[1],"--board")){
        FILE *board_file=fopen(argv[3],"rb");assert(board_file);
        assert(fread(p,1,n,board_file)==n&&fgetc(board_file)==EOF);assert(!fclose(board_file));
        const bfi_candidate_record *r=bfi_board_candidate_select(argv[2],p,n);
        if(!r){free(p);return 1;}
        assert(r->id==BFI_CANDIDATE_CONTROLLED_TRIAL&&!r->native_qualified);
        printf("PASS board %s exact controlled-trial candidate %s; %u profile cases\n",argv[2],r->name,board_profile_cases);
        free(p);return 0;
    }
    if(argc==3&&!strcmp(argv[1],"--public")){
        FILE *pub=fopen(argv[2],"rb");assert(pub);
        assert(fread(p,1,n,pub)==n&&fgetc(pub)==EOF);assert(!fclose(pub));
        bfi_facts good=valid_facts(),f;const bfi_d_candidate_record *selected=NULL;
        assert(!bfi_d_candidate_format(p,n));
        assert(!bfi_check_d_first_install_record(&good,p,n,&selected));
        assert(selected&&selected->id==BFI_D_CANDIDATE_PUBLIC_91E9_TRIAL&&!selected->native_qualified);
        assert(bfi_check_d_first_install(&good,p,n));unsigned cases=3;
#define REJECT_PUBLIC(change) do{f=good;change;selected=(const bfi_d_candidate_record*)1; \
        assert(bfi_check_d_first_install_record(&f,p,n,&selected)!=NULL);assert(selected==NULL);cases++;}while(0)
        REJECT_PUBLIC(f.boot_after[0]='1');
        REJECT_PUBLIC(f.manufacturer=0xc8);
        REJECT_PUBLIC(f.declared_load_ecc=8);
        REJECT_PUBLIC(f.target_offset=0x640000);
        REJECT_PUBLIC(f.bridge_bytes=0x2000000);
        REJECT_PUBLIC(f.pages.appsbl[1].ecc_corrected_after=1);
        p[0x3000]^=1;assert(!bfi_d_candidate_select(p,n));p[0x3000]^=1;cases++;
        p[n-1]=0;assert(!bfi_d_candidate_select(p,n));p[n-1]=0xff;cases++;
        assert(!bfi_d_candidate_select(p,n-1));cases++;
        free(p);printf("PASS %u public-trial plus %u generic-profile cases (software fixtures, not hardware)\n",cases,generic_cases);return 0;
    }
    if(argc!=2){free(p);return 64;}
    FILE *in=fopen(argv[1],"rb");assert(in);assert(fread(p,1,n,in)==n&&fgetc(in)==EOF);assert(!fclose(in));
    bfi_facts good=valid_facts(),f;assert(bfi_d_candidate_format(p,n));
    const bfi_d_candidate_record *selected=NULL;
    assert(!bfi_check_d_first_install(&good,p,n));
    assert(!bfi_check_d_first_install_record(&good,p,n,&selected));
    assert(selected&&selected->id==BFI_D_CANDIDATE_CANONICAL_5F&&selected->native_qualified);
    unsigned cases=2;
#define REJECT(change) do{f=good;change;assert(bfi_check_d_first_install(&f,p,n)!=NULL);cases++;}while(0)
    REJECT(f.boot_after[0]='1');REJECT(memset(f.boot_before,'x',sizeof(f.boot_before)));
    REJECT(f.manufacturer=0xc8);REJECT(f.chip_id=0xbb);REJECT(f.model[0]='X');
    REJECT(f.capacity_mib=256);REJECT(f.raw_spare=128);REJECT(f.linux_ecc=8);REJECT(f.page=4096);
    REJECT(f.sbl_sha256[0]='0');REJECT(f.sbl_bytes--);REJECT(f.sbl_tail_ff=0);
    REJECT(f.declared_storage_ecc=8);REJECT(f.declared_load_ecc=8);
    REJECT(f.bc_appsbl=0);REJECT(f.bc_current_rootfs=1);REJECT(f.bc_equal_valid=0);
    REJECT(f.target_offset=0x640000);REJECT(f.active_offset=0x500000);REJECT(f.active_sha256[0]='0');
    REJECT(f.bridge_offset=0x3100000);REJECT(f.oem_offset=0x900000);REJECT(f.oem_selector=0);
    REJECT(f.bridge_and_oem_verified=0);REJECT(f.r012_route_verified=0);REJECT(f.env_bootargs_verified=0);REJECT(f.unsigned_boot_allowed=0);
    REJECT(f.bridge_bytes=0x2000000);
    REJECT(f.pages.clean=0);REJECT(f.pages.appsbl[0].pages_read--);
    REJECT(f.pages.appsbl[1].ecc_corrected_after=1);REJECT(f.pages.appsbl[1].ecc_uncorrected_after=1);
    REJECT(f.pages.appsbl[1].ecc_stats_state=BMO_ECC_STATS_UNSUPPORTED);
    REJECT(f.pages.appsbl[1].observed_oob_bytes=128);
    p[0x3000]^=1;assert(!bfi_d_candidate_format(p,n));p[0x3000]^=1;cases++;
    p[n-1]=0;assert(!bfi_d_candidate_format(p,n));p[n-1]=0xff;cases++;
    assert(!bfi_d_candidate_format(p,n-1));cases++;
    assert(bfi_check_d_first_install(NULL,p,n));

    free(p);printf("PASS %u canonical-only plus %u generic-profile cases (software fixtures, not hardware)\n",cases,generic_cases);return 0;
}
